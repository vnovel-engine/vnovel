#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
// engine_kirikiri_cabi.cpp — C ABI wrapper for krkrsdl2 engine (dlopen approach)
//
// krkrsdl2 is built as a separate shared library (libkrkrsdl2.so) to avoid
// SDL 1.2/SDL2 symbol conflicts. This wrapper dlopen's it at runtime.
// RTLD_DEEPBIND ensures krkrsdl2 uses its own SDL2, not the core's SDL 1.2.
typedef unsigned int Uint32;
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
/* The core's single game clock (sdl-libretro/video.c); installed into the
 * engine via krkr_set_clock. C linkage — defined in C, consumed from C++. */
extern "C" long long ons_libretro_game_clock_ms(void);
/* libretro input device constants (from libretro.h) */
#define KRKR_RETRO_DEVICE_POINTER 6        /* RETRO_DEVICE_POINTER (was wrongly 0x104) */
#define KRKR_RETRO_DEVICE_JOYPAD  1
#define KRKR_RETRO_DEVICE_ID_JOYPAD_L 10
#define KRKR_RETRO_DEVICE_ID_JOYPAD_R 11
#define KRKR_RETRO_DEVICE_ID_POINTER_X 0
#define KRKR_RETRO_DEVICE_ID_POINTER_Y 1
#define KRKR_RETRO_DEVICE_ID_POINTER_PRESSED 2
typedef void (*krkrsdl2_pre_init_platform_t)();
typedef void (*krkrsdl2_run_main_loop_t)();
typedef void (*krkrsdl2_cleanup_t)();
typedef void (*krkr_set_flag_t)(int);
typedef void (*krkr_notify_poll_t)(long long);
typedef void (*krkr_set_resolution_t)(int, int);

// Global surface pointer — void* to avoid SDL 1.2/SDL2 type conflict
extern "C" void *SDL_libretro_surface = NULL;
// Audio capture is in krkrsdl2's FAudio_platform_sdl2.c (SDL_libretro_audio_* globals)

static pthread_t krkr_thread;
static volatile bool krkr_running = false;
static char krkr_game_path[4096] = "";
static void *krkr_dl = NULL;
static char krkr_personal_path[4096] = "";
static krkrsdl2_pre_init_platform_t p_pre_init = NULL;
static krkrsdl2_run_main_loop_t p_run_main_loop = NULL;
static krkrsdl2_cleanup_t p_cleanup = NULL;
static krkr_set_flag_t p_set_fastforward = NULL;
static krkr_notify_poll_t p_notify_poll = NULL;
static krkr_set_resolution_t p_set_resolution = NULL;
/* Engine restart hook (krkrsdl2_prepare_restart). Resolved and invoked inside
 * krkr_init, before the engine thread starts. */
static void (*p_prepare_restart)(void) = NULL;
/* Engine quit-request entry. Resolved while the handle is live (krkr_shutdown
 * drops krkr_dl), so krkr_shutdown can post the request without poking engine
 * state. */
static void (*p_request_quit)(void) = NULL;
/* Pending resolution request, applied once the symbol is resolved. */
static int krkr_req_res_w = 0, krkr_req_res_h = 0;
static void **krkr_surface_ptr = NULL;
static int *krkr_surface_updated_ptr = NULL;
static volatile int *krkr_auto_mode_ptr = NULL;
static volatile int *krkr_engine_stopped_ptr = NULL;

static void *krkr_main(void *data) {
    chdir(krkr_game_path);
    // Set command line args
    char *argv[] = {(char*)"krkrsdl2", (char*)"-debugwin", (char*)"-readencoding=Shift_JIS", (char*)"-log", (char*)"-evalcontext=global", (char*)"-wsfreq=48000", NULL};
    typedef void (*set_args_t)(int, char**);
    set_args_t p_set_args = (set_args_t)dlsym(krkr_dl, "_Z25krkrsdl2_convert_set_argsiPPc");
    if (p_set_args) p_set_args(6, argv);
    /* Save location: the engine resolves System.personalPath (used by the game
     * for its "Fate_Savedata" folder) from this env var, which the frontend
     * fills with RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY. */
    if (krkr_personal_path[0]) setenv("KRKRSDL2_PERSONAL_PATH", krkr_personal_path, 1);
    /* Polyfill data dir (system_polyfill/*.tjs + startup.tjs). On the dev
     * machine the launcher exports this; on a packaged install (e.g. Recalbox)
     * nothing does, so default to the standard system path the buildroot
     * package installs to. An explicit env var still wins. */
    if (!getenv("KRKRSDL2_DATA_PATH"))
        setenv("KRKRSDL2_DATA_PATH", "/usr/share/krkrsdl2", 1);
    char path_env[4096];
    snprintf(path_env, sizeof(path_env), "%s:%s/system/:%s/plugin/", krkr_game_path, krkr_game_path, krkr_game_path);
    if (p_pre_init) p_pre_init();
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    setenv("KRKRSDL2_LIBRETRO", "1", 1);
    typedef bool (*init_platform_t)();
    init_platform_t p_init = (init_platform_t)dlsym(krkr_dl, "_Z22krkrsdl2_init_platformv");
    fprintf(stderr, "krkr_main: init_platform=%p\n", p_init);
    if (p_init && p_init()) {
        fprintf(stderr, "krkr_main: init_platform failed\n");
        krkr_running = false;
        return NULL;
    }
    krkr_running = true;
    if (p_run_main_loop) p_run_main_loop();
    krkr_running = false;
    return NULL;
}

extern "C" {
typedef void* EngineHandle;

EngineHandle krkr_create(void) { return (EngineHandle)1; }


int krkr_init(EngineHandle engine) {
    if (krkr_running) return 0;
    fprintf(stderr, "krkr_init: starting\n");

    /* The engine is dlopened (not linked) because it needs SDL2 while this
     * core links SDL 1.2. On a packaged install libkrkrsdl2.so lives in
     * /usr/lib (the loader's default search path); a dev tree can point at a
     * local build via KRKRSDL2_LIB_PATH for convenience. */
    const char *override = getenv("KRKRSDL2_LIB_PATH");
    if (override && *override) {
        krkr_dl = dlopen(override, RTLD_NOW | RTLD_DEEPBIND);
        fprintf(stderr, "krkr_init: dlopen(%s)=%p\n", override, krkr_dl);
    }
    /* Fallback chain for the engine .so. A bare dlopen("libkrkrsdl2.so") only
     * works if the loader's search path happens to contain it; on this dev
     * setup it does not, so KiriKiri silently failed to initialise unless the
     * caller exported KRKRSDL2_LIB_PATH. Try, in order: the directory holding
     * the running core (where a dev/side-by-side install puts it), the
     * conventional libretro cores dir, then the system paths. */
    if (!krkr_dl) {
        static const char *cands[] = {
            "$COREDIR/libkrkrsdl2.so",          /* replaced below */
            "$HOME/.config/retroarch/cores/libkrkrsdl2.so", /* replaced below */
            "/usr/lib/libkrkrsdl2.so",
            "/usr/local/lib/libkrkrsdl2.so",
            "libkrkrsdl2.so",                   /* loader search path (last) */
        };
        char path[4096];
        /* The core's own directory: dladdr on a symbol in this .so. */
        char coredir[4096] = "";
        {
            Dl_info info;
            if (dladdr((void*)&krkr_init, &info) && info.dli_fname) {
                snprintf(coredir, sizeof(coredir), "%s", info.dli_fname);
                char *slash = strrchr(coredir, '/');
                if (slash) *slash = '\0'; else coredir[0] = '\0';
            }
        }
        const char *home = getenv("HOME");
        for (unsigned i = 0; i < sizeof(cands)/sizeof(cands[0]) && !krkr_dl; i++) {
            if (strncmp(cands[i], "$COREDIR/", 9) == 0) {
                if (!coredir[0]) continue;
                snprintf(path, sizeof(path), "%s/%s", coredir, cands[i] + 9);
            } else if (strncmp(cands[i], "$HOME/", 6) == 0) {
                if (!home || !*home) continue;
                snprintf(path, sizeof(path), "%s/%s", home, cands[i] + 6);
            } else {
                snprintf(path, sizeof(path), "%s", cands[i]);
            }
            krkr_dl = dlopen(path, RTLD_NOW | RTLD_DEEPBIND);
            if (krkr_dl) fprintf(stderr, "krkr_init: dlopen(%s)=%p\n", path, krkr_dl);
        }
    }
    if (!krkr_dl) {
        fprintf(stderr, "Failed to load libkrkrsdl2.so: %s\n", dlerror());
        return -1;
    }
    p_pre_init = (krkrsdl2_pre_init_platform_t)dlsym(krkr_dl, "_Z26krkrsdl2_pre_init_platformv");
    p_run_main_loop = (krkrsdl2_run_main_loop_t)dlsym(krkr_dl, "_Z22krkrsdl2_run_main_loopv");
    p_cleanup = (krkrsdl2_cleanup_t)dlsym(krkr_dl, "_Z16krkrsdl2_cleanupv");

    fprintf(stderr, "krkr_init: pre_init=%p run_main=%p cleanup=%p\n", p_pre_init, p_run_main_loop, p_cleanup);
    krkr_surface_ptr = (void**)dlsym(krkr_dl, "SDL_libretro_surface");
    krkr_surface_updated_ptr = (int*)dlsym(krkr_dl, "SDL_libretro_surface_updated");
    krkr_auto_mode_ptr = (volatile int*)dlsym(krkr_dl, "krkrsdl2_auto_mode");
    krkr_engine_stopped_ptr = (volatile int*)dlsym(krkr_dl, "krkrsdl2_engine_stopped");
    p_set_fastforward = (krkr_set_flag_t)dlsym(krkr_dl, "krkr_set_fastforward");
    p_notify_poll = (krkr_notify_poll_t)dlsym(krkr_dl, "krkr_notify_poll");
    p_set_resolution = (krkr_set_resolution_t)dlsym(krkr_dl, "krkr_set_resolution");
    p_prepare_restart = (void (*)(void))dlsym(krkr_dl, "krkrsdl2_prepare_restart");
    p_request_quit = (void (*)(void))dlsym(krkr_dl, "krkrsdl2_request_quit");
    /* Install the core's single game clock into the engine: every KiriKiri
     * timer/transition/script wait then reads the same dilated time the core
     * computes (fast-forward included), instead of the engine keeping its own
     * clock state. Must happen before the engine thread starts. */
    {
        void (*p_set_clock)(long long (*)(void)) =
            (void (*)(long long (*)(void)))dlsym(krkr_dl, "krkr_set_clock");
        if (p_set_clock) p_set_clock(ons_libretro_game_clock_ms);
        else fprintf(stderr, "krkr_init: engine lacks krkr_set_clock\n");
    }
    /* Re-arm the engine's process-global one-shot latches before the new thread
     * starts. On the very first load the engine is fresh and every reset below
     * is a no-op; on a restart (the .so stays mapped, so the previous run's
     * globals survive) this is what lets StartApplication run a second time.
     * The engine owns the individual flags; we call one entry point. */
    if (p_prepare_restart) p_prepare_restart();
    if (p_request_quit == NULL)
        fprintf(stderr, "krkr_init: engine lacks krkrsdl2_request_quit\n");
    /* Apply any resolution request made before the engine was loaded, now that
     * the symbol exists and before the engine thread creates its window. */
    if (p_set_resolution && (krkr_req_res_w || krkr_req_res_h))
        p_set_resolution(krkr_req_res_w, krkr_req_res_h);
    fprintf(stderr, "krkr_init: surface_ptr=%p set_ff=%p notify_poll=%p\n",
            krkr_surface_ptr, (void*)p_set_fastforward, (void*)p_notify_poll);
    if (!p_run_main_loop) {
        fprintf(stderr, "Failed to find krkrsdl2_run_main_loop\n");
        return -1;
    }
    setenv("KRKRSDL2_LIBRETRO", "1", 1);
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    setenv("SDL_AUDIODRIVER", "dummy", 1);

    if (pthread_create(&krkr_thread, NULL, krkr_main, NULL) != 0) {
        fprintf(stderr, "Failed to create krkrsdl2 thread\n");
        return -1;
    }
    return 0;
}

void krkr_executeLabel(EngineHandle engine) {}
void krkr_eventLoop(EngineHandle engine) {}
void krkr_resetCommand(EngineHandle engine) {}
void krkr_setArchivePath(EngineHandle engine, const char *path) {
    strncpy(krkr_game_path, path, sizeof(krkr_game_path) - 1);
}
extern "C" void krkr_set_save_path(const char *path);
void krkr_setSavePath(EngineHandle engine, const char *path) { krkr_set_save_path(path); }
void krkr_setKeyEXE(EngineHandle engine, const char *path) {}
void krkr_setDebugLevel(EngineHandle engine, int level) {}
void krkr_setTextSpeed(EngineHandle engine, int speed) {}
int krkr_getWidth(EngineHandle engine) { return 640; }
int krkr_getHeight(EngineHandle engine) { return 480; }
int krkr_isAutoMode(EngineHandle engine) { return krkr_auto_mode_ptr ? (*krkr_auto_mode_ptr == 2) : 0; }
int krkr_isSkipMode(EngineHandle engine) { return krkr_auto_mode_ptr ? (*krkr_auto_mode_ptr == 1) : 0; }
void krkr_dumpState(EngineHandle engine, void *file) {}

void **krkr_get_surface_ptr(void) { return krkr_surface_ptr; }
int *krkr_get_surface_updated_ptr(void) { return krkr_surface_updated_ptr; }
volatile int *krkr_get_auto_mode_ptr(void) { return krkr_auto_mode_ptr; }

/* Set by the engine when the game terminated itself (KAG `[close]`): the core
 * then blanks the video and parks instead of letting the frontend quit. */
extern "C" volatile int *krkr_get_engine_stopped_ptr(void) { return krkr_engine_stopped_ptr; }

/* libretro fast-forward: flip the engine's loop flag. Pause needs no explicit
 * call — the engine infers it from a stale krkr_notify_poll heartbeat. */
extern "C" void krkr_set_fastforward(int f) { if (p_set_fastforward) p_set_fastforward(f); }
extern "C" void krkr_notify_poll(long long ms) { if (p_notify_poll) p_notify_poll(ms); }

/* Force the engine's internal resolution (0,0 clears it). The symbol is only
 * resolved once krkr_init has dlopened the engine, so stash the request here
 * and let krkr_init apply it (before the engine thread creates its window). */
extern "C" void krkr_set_resolution(int w, int h) {
    krkr_req_res_w = w;
    krkr_req_res_h = h;
    if (p_set_resolution) p_set_resolution(w, h);
}

/* Frontend save directory (RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY), exported to
 * the engine as KRKRSDL2_PERSONAL_PATH so System.personalPath resolves there. */
extern "C" void krkr_set_save_path(const char *path) {
    if (!path) { krkr_personal_path[0] = 0; return; }
    strncpy(krkr_personal_path, path, sizeof(krkr_personal_path) - 1);
    krkr_personal_path[sizeof(krkr_personal_path) - 1] = 0;
}

/* Last mouse position (engine 800x600 space), for the software cursor overlay. */
int krkr_mouse_x = -1, krkr_mouse_y = -1;
extern "C" void krkr_get_mouse_pos(int *x, int *y) { if (x) *x = krkr_mouse_x; if (y) *y = krkr_mouse_y; }

/* --- Reset as a two-phase, engine-owned handshake -------------------------
 * The core posts a REQUEST to return the engine to initial conditions and
 * polls for acknowledgement; it never tears engine state down itself.
 *
 *   Phase 1 (stop): krkr_request_stop() asks the engine thread to unwind;
 *     krkr_finish_stop() waits (bounded) for that acknowledgement, then joins
 *     and tears the application down — only after the thread has exited, so it
 *     is safe.
 *   Phase 2 (start): krkr_start() re-arms the engine's process-global latches
 *     (the .so is never unmapped — glibc retains libkrkrsdl2, a relocation
 *     dependency of libz/libpng/libogg/libstdc++) and re-creates the thread.
 *
 * krkr_shutdown() is the blocking form for permanent teardown (unload/deinit),
 * kept separate so a frontend exit still completes in one call.
 */
extern "C" void krkr_request_stop(void) {
    if (!krkr_dl) return;
    /* Ask the engine to quit even if it is paused/idle (frontend stopped
     * polling): the engine loop checks this before its pause gate, so it can
     * always reach the terminate path. Do NOT call Application::Terminate()
     * from this thread — the engine thread reads the request and tears itself
     * down (TVPSystemUninit / delete TVPSystemControl); a concurrent Terminate
     * here races that teardown and can deadlock. The flag itself is engine
     * state; we only post the request. */
    if (p_request_quit) p_request_quit();
}

/* Complete phase 1 if the engine thread has exited. Returns 1 when the engine
 * is fully stopped (joined + torn down + unpublished), 0 while it is still
 * unwinding. Idempotent-ish: only acts while an engine is loaded. */
extern "C" int krkr_finish_stop(void) {
    if (!krkr_dl) return 1;
    if (krkr_running) {
        /* Bounded wait for the engine thread to acknowledge and unwind. */
        struct timespec ts = {0, 5 * 1000 * 1000};
        for (int i = 0; i < 400 && krkr_running; i++) nanosleep(&ts, NULL);
    }
    if (krkr_running) {
        fprintf(stderr, "krkr_finish_stop: engine thread did not exit; not joining\n");
        return 0;
    }
    if (p_cleanup) { try { p_cleanup(); } catch(...) {} }
    void *ret = NULL;
    pthread_join(krkr_thread, &ret);
    krkr_dl = NULL;
    /* Unpublish the engine's pointers: they point into the (still-mapped)
     * engine, but the surface/application behind them is being torn down and
     * rebuilt. retro_run() reads them from the frontend thread while the next
     * run rebuilds the engine, so NULL makes it skip the KiriKiri video path
     * until krkr_start() republishes them. Function pointers are cleared too so
     * pump/notify/fastforward calls in that window are no-ops. */
    krkr_surface_ptr = NULL;
    krkr_surface_updated_ptr = NULL;
    krkr_auto_mode_ptr = NULL;
    krkr_engine_stopped_ptr = NULL;
    p_set_fastforward = NULL;
    p_notify_poll = NULL;
    p_prepare_restart = NULL;
    p_request_quit = NULL;
    return 1;
}

/* Permanent teardown (unload/deinit): request, bounded-wait, then finish. */
extern "C" void krkr_shutdown(void) {
    if (!krkr_dl) return;
    krkr_request_stop();
    if (krkr_running) {
        struct timespec ts = {0, 5 * 1000 * 1000};
        for (int i = 0; i < 400 && krkr_running; i++) nanosleep(&ts, NULL);
    }
    if (!krkr_running) krkr_finish_stop();
    else fprintf(stderr, "krkr_shutdown: engine thread did not exit; returning\n");
    /* finish_stop() already cleared krkr_dl; ensure it here too for the
     * timeout path so a later load re-inits cleanly. */
    krkr_dl = NULL;
}

/* Phase 2: begin a fresh run from initial conditions. krkr_init re-arms the
 * engine's one-shot latches (krkrsdl2 keeps its .so mapped across a reset, so
 * the previous run's process globals survive) and re-creates the thread. */
extern "C" void krkr_start(void) {
    if (!krkr_game_path[0]) {
        fprintf(stderr, "krkr_start: no game path; ignoring\n");
        return;
    }
    krkr_init((EngineHandle)1);
}

/* Input state callback from libretro (set by libretro.cpp) */
typedef int16_t (*input_state_t)(unsigned, unsigned, unsigned, unsigned);
static input_state_t krkr_input_state = NULL;
static void (*krkr_input_poll)(void) = NULL;

extern "C" void krkr_set_input_callbacks(input_state_t state_cb, void (*poll_cb)(void)) {
    krkr_input_state = state_cb;
    krkr_input_poll = poll_cb;
}

/* Keyboard input forwarded from RetroArch's keyboard callback.
 * The SDL2 dummy video driver never delivers SDL_KEYDOWN/UP, and the joypad
 * path only reaches the retropad-mapped keys. Real keysyms (matched to
 * libretro RETROK_* which equal SDL2 SDLK_* for ASCII) are queued here and
 * drained in krkr_pump_events() as SDL key events. */
#define KRKR_KEYQ_SIZE 128
static int krkr_keyq_sym[KRKR_KEYQ_SIZE];
static unsigned char krkr_keyq_down[KRKR_KEYQ_SIZE];
static volatile int krkr_keyq_head = 0, krkr_keyq_tail = 0;

extern "C" void krkr_push_key(int sym, int down) {
    int n = (krkr_keyq_head + 1) % KRKR_KEYQ_SIZE;
    if (n == krkr_keyq_tail) return; /* full: drop */
    krkr_keyq_sym[krkr_keyq_head] = sym;
    krkr_keyq_down[krkr_keyq_head] = (unsigned char)(down ? 1 : 0);
    krkr_keyq_head = n;
}

/* SDL2 event type constants */
#define KRKR_SDL_MOUSEMOTION     0x400
#define KRKR_SDL_MOUSEBUTTONDOWN 0x401
#define KRKR_SDL_MOUSEBUTTONUP   0x402
#define KRKR_SDL_KEYDOWN         0x300
#define KRKR_SDL_KEYUP           0x301
/* SDL_CONTROLLERBUTTONDOWN/UP (SDL_events.h enum, after the touch/gesture
 * block). Used to deliver the D-pad as KiriKiri *virtual pad* keys: see the
 * PAD table note below. */
#define KRKR_SDL_CONTROLLERBUTTONDOWN 0x651
#define KRKR_SDL_CONTROLLERBUTTONUP   0x652

/* SDL2 keysyms the pad dispatch emits. This file does not include the SDL2
 * headers (it dlopen()s the engine's own SDL2; see the RTLD_DEEPBIND note at
 * the top), so spell the ABI values as named constants instead of guessing
 * them inline. In SDL2's keysym layout, keys below 0x40000000 are effectively
 * ASCII, and keys with no character carry bit 30 over their scancode:
 * SDLK_UP/DOWN/LEFT/RIGHT = 0x40000000 | SDL_SCANCODE_{UP,DOWN,LEFT,RIGHT}
 * (= SDL_SCANCODE_TO_KEYCODE). */
#define KRKR_SDLK_RETURN  0x0d                      /* SDLK_RETURN */
#define KRKR_SDLK_ESCAPE  0x1b                      /* SDLK_ESCAPE */
#define KRKR_SDLK_a       0x61                      /* SDLK_a */
#define KRKR_SDLK_s       0x73                      /* SDLK_s */
#define KRKR_SDLK_f       0x66                      /* SDLK_f */
#define KRKR_SDLK_UP      (int)(0x40000000u | 82u)  /* SDL_SCANCODE_UP */
#define KRKR_SDLK_DOWN    (int)(0x40000000u | 81u)  /* SDL_SCANCODE_DOWN */
#define KRKR_SDLK_LEFT    (int)(0x40000000u | 80u)  /* SDL_SCANCODE_LEFT */
#define KRKR_SDLK_RIGHT   (int)(0x40000000u | 79u)  /* SDL_SCANCODE_RIGHT */

/* Called from retro_run each frame to keep engine alive and forward input.
 * The dummy video driver doesn't generate events, so we inject SDL2 events. */
void krkr_pump_events(void) {
    if (!krkr_dl || !krkr_running) return;
    { static int sb = 0; if (!sb) { setvbuf(stderr, NULL, _IONBF, 0); sb = 1; } }
    static int (*p_SDL_PushEvent)(void*) = NULL;
    if (!p_SDL_PushEvent) {
        p_SDL_PushEvent = (int(*)(void*))dlsym(RTLD_DEFAULT, "SDL_PushEvent");
        if (!p_SDL_PushEvent) {
            void *sdl2 = dlopen("libSDL2-2.0.so.0", RTLD_NOW | RTLD_NOLOAD);
            if (!sdl2) sdl2 = dlopen("libSDL2-2.0.so.0", RTLD_LAZY);
            if (sdl2) p_SDL_PushEvent = (int(*)(void*))dlsym(sdl2, "SDL_PushEvent");
        }
    }
    { static int pe_dbg = 0; if (pe_dbg < 3) { fprintf(stderr, "krkr_pump: SDL_PushEvent=%p\n", (void*)p_SDL_PushEvent); pe_dbg++; } }
    if (!p_SDL_PushEvent) return;

    /* Drain queued keyboard events (from RetroArch's keyboard callback).
     * Independent of the pointer/joypad path below. */
    {
        while (krkr_keyq_tail != krkr_keyq_head) {
            int sym = krkr_keyq_sym[krkr_keyq_tail];
            int down = krkr_keyq_down[krkr_keyq_tail];
            krkr_keyq_tail = (krkr_keyq_tail + 1) % KRKR_KEYQ_SIZE;
            char ev[64];
            memset(ev, 0, sizeof(ev));
            *(unsigned int*)(ev+0)  = down ? KRKR_SDL_KEYDOWN : KRKR_SDL_KEYUP;
            *(unsigned char*)(ev+12) = down ? 1 : 0;   /* state */
            *(unsigned char*)(ev+13) = 0;              /* repeat */
            *(int*)(ev+20)           = sym;            /* keysym.sym */
            p_SDL_PushEvent(ev);
        }
    }

    /* NOTE: do NOT push a bare SDL_USEREVENT here. krkrsdl2 registers its
     * own custom event type via SDL_RegisterEvents(1), which returns
     * SDL_USEREVENT (0x8000) on a fresh event pool. A zeroed 0x8000 event
     * collides with it and sdl_process_events() dereferences
     * event.user.data2 == NULL -> segfault. The engine's SDL_PollEvent loop
     * runs every frame regardless, so no wake event is needed. */

    /* Forward mouse input from RetroArch */
    if (!krkr_input_state) return;

    /* RetroArch delivers an on-screen pointer as EITHER:
     *  - RETRO_DEVICE_POINTER absolute [-32768..32767] (lightgun/pointer device), or
     *  - RETRO_DEVICE_MOUSE relative deltas (plain mouse device).
     * The core's own events.c integrates MOUSE deltas; use both here so the
     * engine sees an accurate position regardless of which RA reports. */
#define KRKR_RETRO_DEVICE_MOUSE  0x002
#define KRKR_RETRO_DEVICE_ID_MOUSE_X 0
#define KRKR_RETRO_DEVICE_ID_MOUSE_Y 1
#define KRKR_RETRO_DEVICE_ID_MOUSE_LEFT  2
#define KRKR_RETRO_DEVICE_ID_MOUSE_RIGHT 3

    /* Current engine surface size. The internal rendering resolution is chosen
     * by the game (or forced via the core option), so it must not be assumed:
     * read w/h from the live SDL_Surface. Layout on x86-64:
     * flags(4)+pad(4)+format(8)+w(4)+h(4). Falls back to 800x600 until the
     * engine has published a surface. */
    int surf_w = 800, surf_h = 600;
    if (krkr_surface_ptr && *krkr_surface_ptr) {
        char *s = (char*)*krkr_surface_ptr;
        int w = *(int*)(s + 16);
        int h = *(int*)(s + 20);
        if (w > 0 && h > 0) { surf_w = w; surf_h = h; }
    }

    int16_t px = krkr_input_state(0, KRKR_RETRO_DEVICE_POINTER, 0, KRKR_RETRO_DEVICE_ID_POINTER_X);
    int16_t py = krkr_input_state(0, KRKR_RETRO_DEVICE_POINTER, 0, KRKR_RETRO_DEVICE_ID_POINTER_Y);
    int16_t rmx = krkr_input_state(0, KRKR_RETRO_DEVICE_MOUSE, 0, KRKR_RETRO_DEVICE_ID_MOUSE_X);
    int16_t rmy = krkr_input_state(0, KRKR_RETRO_DEVICE_MOUSE, 0, KRKR_RETRO_DEVICE_ID_MOUSE_Y);

    /* Pointer position: starts "unset" (negative) rather than at the centre of
     * the screen — with no real pointer input the core must not park the
     * software cursor dead-centre of the game. It becomes visible only once
     * the frontend actually reports pointer/mouse input (or the stick moves,
     * via the core's RetroPad->pointer synthesis). */
    static int mouse_ax = -1, mouse_ay = -1;   /* integrated position; -1 = unset */
    if (rmx || rmy) {
        if (mouse_ax < 0) { mouse_ax = surf_w / 2; mouse_ay = surf_h / 2; }
        mouse_ax += rmx; mouse_ay += rmy;
    }
    if (px || py) {
        mouse_ax = (int)((int)(px + 32768) * surf_w / 65536);
        mouse_ay = (int)((int)(py + 32768) * surf_h / 65536);
    }
    int mx = mouse_ax, my = mouse_ay;
    if (mx >= 0 && my >= 0) {
        if (mx > surf_w - 1) mx = surf_w - 1;
        if (my > surf_h - 1) my = surf_h - 1;
        mouse_ax = mx; mouse_ay = my;
        krkr_mouse_x = mx; krkr_mouse_y = my;
    }

    /* pressed: pointer-pressed, or the mouse LEFT/right buttons */
    int16_t pressed = krkr_input_state(0, KRKR_RETRO_DEVICE_POINTER, 0, KRKR_RETRO_DEVICE_ID_POINTER_PRESSED);
    int16_t mleft  = krkr_input_state(0, KRKR_RETRO_DEVICE_MOUSE, 0, KRKR_RETRO_DEVICE_ID_MOUSE_LEFT);
    int16_t mright = krkr_input_state(0, KRKR_RETRO_DEVICE_MOUSE, 0, KRKR_RETRO_DEVICE_ID_MOUSE_RIGHT) ||
                     krkr_input_state(0, KRKR_RETRO_DEVICE_JOYPAD, 0, KRKR_RETRO_DEVICE_ID_JOYPAD_L);

    static int prev_mx = -1, prev_my = -1;
    static int prev_left = 0, prev_right = 0;

    /* Push mouse motion event if position changed (only once the pointer has
     * been set by real input — mx/my are -1 until then). */
    if (mx >= 0 && my >= 0 && (mx != prev_mx || my != prev_my)) {
        /* SDL_MouseMotionEvent: type(4) ts(4) win(4) which(4) state(4) x(4) y(4) xrel(4) yrel(4) = 36 bytes */
        char ev[64];
        memset(ev, 0, sizeof(ev));
        *(unsigned int*)(ev+0) = KRKR_SDL_MOUSEMOTION;
        *(int*)(ev+20) = mx;   /* x */
        *(int*)(ev+24) = my;   /* y */
        if (prev_mx >= 0) {
            *(int*)(ev+28) = mx - prev_mx;  /* xrel */
            *(int*)(ev+32) = my - prev_my;  /* yrel */
        }
        int ret = p_SDL_PushEvent(ev);
        (void)ret;
        prev_mx = mx;
        prev_my = my;
    }

    /* Push mouse button event on click state change.
     * Right button (SDL button 3) opens the KAG right-click menu. RetroArch can
     * deliver it as the mouse RIGHT button, the pointer-pressed state, or a
     * RetroPad shoulder button.
     *
     * L = LEFT click (the primary act: confirm/advance, click a dialog button);
     * R = RIGHT click (cancel/back, the KAG right-click menu). This matches the
     * RetroPad convention where the left shoulder is the affirmative action and
     * the right shoulder is the secondary/back action — the mapping was
     * previously inverted (L emitted a right-click and R a left-click). */
    {
        int lbtn = krkr_input_state(0, KRKR_RETRO_DEVICE_JOYPAD, 0, KRKR_RETRO_DEVICE_ID_JOYPAD_L);
        int rbtn = krkr_input_state(0, KRKR_RETRO_DEVICE_JOYPAD, 0, KRKR_RETRO_DEVICE_ID_JOYPAD_R);
        /* L -> left click, R -> right click. A real mouse's right button also
         * means right-click. */
        int right = mright || rbtn;
        /* RetroArch reports POINTER_PRESSED for ANY button, so a right-click also
         * sets `pressed`. Without the !right guard this emitted a spurious LEFT
         * press alongside the right, and KAG consumed it as a left click
         * (advance/hide) — defeating the right-click KAG menu. */
        int left  = mleft || lbtn || (pressed && !right);
        int btn = 0, is_down = 0;
        if (left != prev_left)        { btn = 1; is_down = left;  prev_left  = left; }
        else if (right != prev_right) { btn = 3; is_down = right; prev_right = right; }
        if (btn && mx >= 0 && my >= 0) {
            /* mx/my are set only after real pointer input; never inject a
             * click at the unset (-1) position. */
            char ev[64];
            memset(ev, 0, sizeof(ev));
            *(unsigned int*)(ev+0) = is_down ? KRKR_SDL_MOUSEBUTTONDOWN : KRKR_SDL_MOUSEBUTTONUP;
            *(unsigned char*)(ev+16) = (unsigned char)btn;   /* SDL button: 1=left, 3=right */
            *(unsigned char*)(ev+17) = is_down ? 1 : 0;      /* SDL_PRESSED/RELEASED */
            *(unsigned char*)(ev+18) = 1;                    /* clicks */
            *(int*)(ev+20) = mx;
            *(int*)(ev+24) = my;
#ifdef KRKR_DEBUG_PROBES
            { static int n=0; if(n<40){ fprintf(stderr, "krkr_pump: PUSH btn=%d down=%d at (%d,%d)\n", btn, is_down, mx, my); n++; } }
#endif
            p_SDL_PushEvent(ev);
        }
    }

    /* Forward keyboard / joypad input as synthetic SDL events.
     * The engine's window handler consumes SDL_KEYDOWN/UP and maps
     * keysym.sym through sdl_key_to_vk_key(); without this the port can only
     * be advanced with the mouse, which KAG scenarios generally ignore.
     *
     * Canonical pad mapping (kept consistent with the ONScripter engine):
     *   Start = auto toggle      (KAG accelerator 'A')
     *   Select= skip toggle      (KAG accelerator 'F')
     *   A     = advance/confirm  (Return)
     *   B     = cancel/menu      (Escape)
     *   X     = hide graphics    (right-click in KAG)
     *   Y     = save             (KAG accelerator 'S')
     *   L/R/L2/R2 = scroll up/down (mouse wheel)
     *   R3    = pointer click    (handled by the core's pad->pointer synth)
     *   D-pad = KAG menu navigation (UP/DOWN/LEFT/RIGHT keys) */
    {
        /* One row per RetroPad button id (RETRO_DEVICE_ID_JOYPAD_*): what to
         * emit when it is pressed. A single table keeps the three emission
         * styles together instead of a key map, a wheel map and an inline
         * special case that had to agree with each other. Keys must use the
         * lower-case SDL letter codes: sdl_key_to_vk_key maps those to
         * VK_A/VK_S/F, which is what KAG's accelerators compare against.
         * D-pad is translated here too — the engine no longer reads the
         * controller natively (KRKRSDL2_LIBRETRO), so this is the only route
         * pad arrows take to KAG menu navigation. */
        enum { EMIT_NONE, EMIT_KEY, EMIT_WHEEL, EMIT_RIGHTCLICK, EMIT_PAD };
        struct { int mode; int code; } const PAD[16] = {
            /*  id            mode            code (SDL keysym, mouse button, or SDL_CONTROLLER_BUTTON_*) */
            /*  B      */ { EMIT_KEY,         KRKR_SDLK_ESCAPE }, /* cancel/menu   */
            /*  Y      */ { EMIT_KEY,         KRKR_SDLK_s },      /* KAG accel 'S' */
            /*  SELECT */ { EMIT_KEY,         KRKR_SDLK_f },      /* KAG accel 'F' */
            /*  START  */ { EMIT_KEY,         KRKR_SDLK_a },      /* KAG accel 'A' */
            /*  UP     */ { EMIT_PAD,         11 },               /* SDL_CONTROLLER_BUTTON_DPAD_UP    */
            /*  DOWN   */ { EMIT_PAD,         12 },               /* SDL_CONTROLLER_BUTTON_DPAD_DOWN  */
            /*  LEFT   */ { EMIT_PAD,         13 },               /* SDL_CONTROLLER_BUTTON_DPAD_LEFT  */
            /*  RIGHT  */ { EMIT_PAD,         14 },               /* SDL_CONTROLLER_BUTTON_DPAD_RIGHT */
            /*  A      */ { EMIT_KEY,         KRKR_SDLK_RETURN }, /* advance       */
            /*  X      */ { EMIT_RIGHTCLICK, 3 },                 /* hide graphics */
            /*  L      */ { EMIT_WHEEL,       4 },                 /* scroll up     */
            /*  R      */ { EMIT_WHEEL,       5 },                 /* scroll down   */
            /*  L2     */ { EMIT_WHEEL,       4 },
            /*  R2     */ { EMIT_WHEEL,       5 },
            /*  L3     */ { EMIT_NONE,        0 },
            /*  R3     */ { EMIT_NONE,        0 },                 /* pointer click */
        };
        /* Why the D-pad is a CONTROLLER event, not arrow KEY events:
         *
         * KAG games distinguish the arrow *keys* from the D-pad via KiriKiri's
         * virtual-pad VKs (VK_PADUP/PADDOWN/PADLEFT/PADRIGHT). Fata Morgana's
         * MainWindow.tjs createPadKeyMap() maps exactly VK_PADLEFT->VK_LEFT,
         * VK_PADDOWN->VK_DOWN, ... to drive menu focus; it has no branch for
         * bare VK_LEFT/etc. So injecting arrow key events navigates the
         * load/save list (plain KAG arrow handling) but leaves the title/menu
         * screen un-navigable. The engine already accepts SDL_CONTROLLERBUTTON*
         * and converts it to VK_PAD* (sdl_gamecontrollerbutton_to_vk_key,
         * SDLApplication.cpp:216) — the shim was simply not using that route.
         * Sending a controller event restores the VK_PAD path without the
         * double-delivery that made us stop opening the controller natively
         * (56529ca): the engine stays unopened, the shim is the only source. */
        static int prev[16];
        for (int i = 0; i < 16; i++) {
            int st = krkr_input_state(0, KRKR_RETRO_DEVICE_JOYPAD, 0, (unsigned)i);
            if (PAD[i].mode == EMIT_NONE) { prev[i] = st; continue; }
            if (st == prev[i]) continue;
            char ev[64];
            memset(ev, 0, sizeof(ev));
            switch (PAD[i].mode) {
            case EMIT_KEY:
                *(unsigned int*)(ev+0)  = st ? KRKR_SDL_KEYDOWN : KRKR_SDL_KEYUP;
                *(unsigned char*)(ev+12) = st ? 1 : 0;
                *(int*)(ev+20)          = PAD[i].code;
                break;
            case EMIT_PAD:
                /* SDL_ControllerButtonEvent: type(4) ts(4) which(8) button(1)
                 * state(1) pad(2). which=0 (the shim has no joystick id; the
                 * engine's handler ignores it). */
                *(unsigned int*)(ev+0)  = st ? KRKR_SDL_CONTROLLERBUTTONDOWN
                                             : KRKR_SDL_CONTROLLERBUTTONUP;
                *(unsigned char*)(ev+12) = (unsigned char)PAD[i].code; /* button */
                *(unsigned char*)(ev+13) = st ? 1 : 0;                 /* state  */
                break;
            case EMIT_WHEEL:
                *(unsigned int*)(ev+0)  = st ? KRKR_SDL_MOUSEBUTTONDOWN : KRKR_SDL_MOUSEBUTTONUP;
                *(unsigned char*)(ev+16) = (unsigned char)PAD[i].code;  /* 4 = up, 5 = down */
                *(unsigned char*)(ev+17) = st ? 1 : 0;
                *(unsigned char*)(ev+18) = 1;
                break;
            case EMIT_RIGHTCLICK:
                *(unsigned int*)(ev+0)  = st ? KRKR_SDL_MOUSEBUTTONDOWN : KRKR_SDL_MOUSEBUTTONUP;
                *(unsigned char*)(ev+16) = 3;  /* SDL button: right */
                *(unsigned char*)(ev+17) = st ? 1 : 0;
                *(unsigned char*)(ev+18) = 1;
                if (mx >= 0) { *(int*)(ev+20) = mx; *(int*)(ev+24) = my; }
                break;
            }
            p_SDL_PushEvent(ev);
            prev[i] = st;
        }
    }
}
/* Pull audio from krkrsdl2's FAudio capture SPSC ring buffer.
 * Writer: the SDL audio thread in FAudio_platform_sdl2.c.
 * Reader: this function, called from retro_run. */
static float **krkr_audio_buf_pp = NULL;  /* addr of float* var (lazy alloc) */
static volatile int *krkr_audio_w_ptr = NULL;  /* monotonic frames written */
static volatile int *krkr_audio_r_ptr = NULL;  /* monotonic frames read */
static int *krkr_audio_cap_ptr = NULL;
static int *krkr_audio_freq_ptr = NULL;
static int krkr_audio_resolved = 0;

extern "C" int krkr_get_audio(int16_t *out_buf, int max_frames) {
    if (!krkr_audio_resolved) {
        krkr_audio_buf_pp = (float**)dlsym(krkr_dl, "SDL_libretro_audio_buf");
        krkr_audio_w_ptr = (volatile int*)dlsym(krkr_dl, "SDL_libretro_audio_write");
        krkr_audio_r_ptr = (volatile int*)dlsym(krkr_dl, "SDL_libretro_audio_read");
        krkr_audio_cap_ptr = (int*)dlsym(krkr_dl, "SDL_libretro_audio_capacity");
        krkr_audio_freq_ptr = (int*)dlsym(krkr_dl, "SDL_libretro_audio_freq");
        krkr_audio_resolved = 1;
        if (krkr_audio_buf_pp)
            fprintf(stderr, "krkr_get_audio: buf_pp=%p w=%p r=%p cap=%p freq=%p\n",
                (void*)krkr_audio_buf_pp, (void*)krkr_audio_w_ptr, (void*)krkr_audio_r_ptr,
                (void*)krkr_audio_cap_ptr, (void*)krkr_audio_freq_ptr);
    }
    if (!krkr_audio_buf_pp || !krkr_audio_w_ptr || !krkr_audio_r_ptr || !krkr_audio_cap_ptr)
        return 0;
    float *krkr_audio_buf_ptr = *krkr_audio_buf_pp;  /* allocated after FAudio init */
    if (!krkr_audio_buf_ptr) return 0;
    int cap = *krkr_audio_cap_ptr;
    if (cap <= 0) return 0;
    int w = *krkr_audio_w_ptr;
    int r = *krkr_audio_r_ptr;
    int avail = w - r;
    if (avail <= 0) return 0;
    /* If the writer lapped us (engine paused then burst), drop stale frames. */
    if (avail > cap) {
        r = w - cap;
        avail = cap;
    }
    int n = avail > max_frames ? max_frames : avail;
    for (int i = 0; i < n; i++) {
        int idx = (r + i) % cap;
        float l = krkr_audio_buf_ptr[idx * 2];
        float rr = krkr_audio_buf_ptr[idx * 2 + 1];
        if (l > 1.0f) l = 1.0f; if (l < -1.0f) l = -1.0f;
        if (rr > 1.0f) rr = 1.0f; if (rr < -1.0f) rr = -1.0f;
        out_buf[i * 2] = (int16_t)(l * 32767.0f);
        out_buf[i * 2 + 1] = (int16_t)(rr * 32767.0f);
    }
    *krkr_audio_r_ptr = r + n;
    return n;
}
} // extern "C"
