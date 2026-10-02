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
/* libretro input device constants (from libretro.h) */
#define KRKR_RETRO_DEVICE_POINTER 6        /* RETRO_DEVICE_POINTER (was wrongly 0x104) */
#define KRKR_RETRO_DEVICE_JOYPAD  1
#define KRKR_RETRO_DEVICE_ID_JOYPAD_L 10
#define KRKR_RETRO_DEVICE_ID_POINTER_X 0
#define KRKR_RETRO_DEVICE_ID_POINTER_Y 1
#define KRKR_RETRO_DEVICE_ID_POINTER_PRESSED 2
typedef void (*krkrsdl2_pre_init_platform_t)();
typedef void (*krkrsdl2_run_main_loop_t)();
typedef void (*krkrsdl2_cleanup_t)();
typedef void (*krkr_set_flag_t)(int);
typedef void (*krkr_notify_poll_t)(long long);

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
static void **krkr_surface_ptr = NULL;
static int *krkr_surface_updated_ptr = NULL;
static volatile int *krkr_auto_mode_ptr = NULL;

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

    const char *home = getenv("HOME");
    char path[4096];
    snprintf(path, sizeof(path), "%s/dev/onscripter-en-libretro/deps/krkrsdl2/build/libkrkrsdl2.so", home);

    krkr_dl = dlopen(path, RTLD_NOW | RTLD_DEEPBIND);
    fprintf(stderr, "krkr_init: dlopen=%p path=%s\n", krkr_dl, path);
    if (!krkr_dl) {
        krkr_dl = dlopen("libkrkrsdl2.so", RTLD_NOW | RTLD_DEEPBIND);
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
    p_set_fastforward = (krkr_set_flag_t)dlsym(krkr_dl, "krkr_set_fastforward");
    p_notify_poll = (krkr_notify_poll_t)dlsym(krkr_dl, "krkr_notify_poll");
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

/* libretro fast-forward: flip the engine's loop flag. Pause needs no explicit
 * call — the engine infers it from a stale krkr_notify_poll heartbeat. */
extern "C" void krkr_set_fastforward(int f) { if (p_set_fastforward) p_set_fastforward(f); }
extern "C" void krkr_notify_poll(long long ms) { if (p_notify_poll) p_notify_poll(ms); }

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

extern "C" void krkr_shutdown(void) {
    if (!krkr_dl) return;
    fprintf(stderr, "krkr_shutdown: terminating\n");
    typedef void (*terminate_t)();
    terminate_t p_terminate = (terminate_t)dlsym(krkr_dl, "_ZN15tTVPApplication9TerminateEv");
    if (p_terminate) { try { p_terminate(); } catch(...) {} }
    /* Wait for the engine thread's run loop to exit before cleanup. */
    if (krkr_running) {
        struct timespec ts = {0, 10 * 1000 * 1000};
        for (int i = 0; i < 500 && krkr_running; i++) nanosleep(&ts, NULL);
    }
    if (p_cleanup) { try { p_cleanup(); } catch(...) {} }
    void *ret = NULL;
    pthread_join(krkr_thread, &ret);
    krkr_running = false;
    krkr_dl = NULL;
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

    static int mouse_ax = 400, mouse_ay = 300;   /* integrated position */
    if (rmx || rmy) { mouse_ax += rmx; mouse_ay += rmy; }
    if (px || py) {
        mouse_ax = (int)((int)(px + 32768) * surf_w / 65536);
        mouse_ay = (int)((int)(py + 32768) * surf_h / 65536);
    }
    if (mouse_ax < 0) mouse_ax = 0; else if (mouse_ax > surf_w - 1) mouse_ax = surf_w - 1;
    if (mouse_ay < 0) mouse_ay = 0; else if (mouse_ay > surf_h - 1) mouse_ay = surf_h - 1;
    int mx = mouse_ax, my = mouse_ay;
    krkr_mouse_x = mx; krkr_mouse_y = my;

    /* pressed: pointer-pressed, or the mouse LEFT/right buttons */
    int16_t pressed = krkr_input_state(0, KRKR_RETRO_DEVICE_POINTER, 0, KRKR_RETRO_DEVICE_ID_POINTER_PRESSED);
    int16_t mleft  = krkr_input_state(0, KRKR_RETRO_DEVICE_MOUSE, 0, KRKR_RETRO_DEVICE_ID_MOUSE_LEFT);
    int16_t mright = krkr_input_state(0, KRKR_RETRO_DEVICE_MOUSE, 0, KRKR_RETRO_DEVICE_ID_MOUSE_RIGHT) ||
                     krkr_input_state(0, KRKR_RETRO_DEVICE_JOYPAD, 0, KRKR_RETRO_DEVICE_ID_JOYPAD_L);

    static int prev_mx = -1, prev_my = -1;
    static int prev_left = 0, prev_right = 0;

    /* Push mouse motion event if position changed */
    if (mx != prev_mx || my != prev_my) {
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
     * RetroPad button mapped to right-click (SDL_libretro_rightclick_button). */
    {
        int right = mright;
        if (krkr_input_state(0, KRKR_RETRO_DEVICE_JOYPAD, 0, KRKR_RETRO_DEVICE_ID_JOYPAD_L)) right = 1;
        /* RetroArch reports POINTER_PRESSED for ANY button, so a right-click also
         * sets `pressed`. Without the !right guard this emitted a spurious LEFT
         * press alongside the right, and KAG consumed it as a left click
         * (advance/hide) — defeating the right-click KAG menu. */
        int left  = mleft || (pressed && !right);
        int btn = 0, is_down = 0;
        if (left != prev_left)        { btn = 1; is_down = left;  prev_left  = left; }
        else if (right != prev_right) { btn = 3; is_down = right; prev_right = right; }
        if (btn) {
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

    /* Forward keyboard / joypad-confirm input as SDL key events.
     * The engine's window handler consumes SDL_KEYDOWN / SDL_KEYUP and maps
     * keysym.sym through sdl_key_to_vk_key(). Without this the port can only
     * be advanced with the mouse, which KAG scenarios generally ignore. */
    {
        /* RetroArch joypad ids (RETRO_DEVICE_JOYPAD): B=0, START=3, A=8 */
        static const unsigned CONFIRM_IDS[] = { 8 /*A*/, 0 /*B*/, 3 /*START*/ };
        static const unsigned CANCEL_IDS[]  = { 1 /*Y*/, 2 /*SELECT*/ };
        int confirm = 0, cancel = 0;
        for (unsigned k = 0; k < sizeof(CONFIRM_IDS)/sizeof(CONFIRM_IDS[0]); k++)
            if (krkr_input_state(0, KRKR_RETRO_DEVICE_JOYPAD, 0, CONFIRM_IDS[k])) { confirm = 1; break; }
        for (unsigned k = 0; k < sizeof(CANCEL_IDS)/sizeof(CANCEL_IDS[0]); k++)
            if (krkr_input_state(0, KRKR_RETRO_DEVICE_JOYPAD, 0, CANCEL_IDS[k]))  { cancel  = 1; break; }

        /* SDLK_RETURN=13, SDLK_ESCAPE=27 */
        static int prev_confirm = 0, prev_cancel = 0;
        static long key_ev_count = 0;

        if (confirm != prev_confirm) {
            char ev[64]; memset(ev, 0, sizeof(ev));
            *(unsigned int*)(ev+0)  = confirm ? KRKR_SDL_KEYDOWN : KRKR_SDL_KEYUP;
            *(unsigned char*)(ev+12)  = confirm ? 1 : 0;      /* state: SDL_PRESSED/RELEASED */
            *(int*)(ev+20)    = 13;                   /* keysym.sym = SDLK_RETURN */
            p_SDL_PushEvent(ev);
            if (key_ev_count < 20) { fprintf(stderr, "krkr_pump: KEY %s RETURN\n", confirm?"DOWN":"UP"); key_ev_count++; }
            prev_confirm = confirm;
        }
        if (cancel != prev_cancel) {
            char ev[64]; memset(ev, 0, sizeof(ev));
            *(unsigned int*)(ev+0)  = cancel ? KRKR_SDL_KEYDOWN : KRKR_SDL_KEYUP;
            *(unsigned char*)(ev+12)  = cancel ? 1 : 0;
            *(int*)(ev+20)    = 27;                   /* keysym.sym = SDLK_ESCAPE */
            p_SDL_PushEvent(ev);
            if (key_ev_count < 20) { fprintf(stderr, "krkr_pump: KEY %s ESCAPE\n", cancel?"DOWN":"UP"); key_ev_count++; }
            prev_cancel = cancel;
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
