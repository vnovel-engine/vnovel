#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
// engine_kirikiri_cabi.cpp — C ABI wrapper for the krkrsdl2 engine
//
// krkrsdl2 is linked into this core, bringing its own vendored SDL2, so there
// is no runtime dlopen. This file drives the engine's thread and installs the
// core state the engine publishes through.
//
// This TU is compiled with the vendored-SDL rename in force (Makefile.common
// passes -DRENAME_SDL_SYMBOLS -include sdl-vendored.h to every C/C++ file), so
// by default `SDL_PushEvent` and `SDL_GetError` here would name vendored SDL
// 1.2's symbols — PrivateSDL_PushEvent / PrivateSDL_GetError — and the events
// this shim injects would go into SDL 1.2's queue, which nothing in the KiriKiri
// runtime ever polls. That is a silent failure: SDL_PushEvent returns -1, the
// engine's own poll loop keeps draining only its 0x8000 userevent, and input
// simply never arrives. The engine is driven through SDL2 (krkrsdl2 vendors
// SDL2 and the frontend loads SDL2/SDL3 globally), so undo the rename for the
// two SDL2 entry points this file calls and let them bind to the real SDL2.
#ifdef RENAME_SDL_SYMBOLS
#undef SDL_PushEvent
#undef SDL_GetError
#endif
typedef unsigned int Uint32;
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
/* setenv/unsetenv are POSIX; the Windows CRT spells the same job _putenv_s. */
#ifdef _WIN32
#include <windows.h>
static inline int setenv(const char *name, const char *value, int overwrite)
{
   if (!overwrite && getenv(name))
      return 0;
   return _putenv_s(name, value);
}
static inline int unsetenv(const char *name)
{
   return _putenv_s(name, "");
}
#endif
#include <retro_miscellaneous.h> /* PATH_MAX_LENGTH */
#include <libretro.h>            /* retro_environment_t, RETRO_ENVIRONMENT_* */

/* The engine's host-facing presentation contract. This is a pure-C header with
 * no engine types in it (stdbool/stdint only), which is exactly why the core can
 * include it directly while declaring every other engine symbol by hand. */
#include "Present.h"
#include "core_debug.h"
/* The text-plane splice switch, engine side (visual/PresentIntf.h). Declared by
 * hand rather than including that header: it is the engine-internal half of the
 * seam and pulls in engine types this file deliberately avoids. C++ linkage, as
 * defined in Present.cpp. */
extern void TVPSetTextPlaneSplice(bool enabled);
extern void TVPSetSplitViewArtworkOnly(bool enabled);
/* The core's single game clock (sdl-libretro/video.c); installed into the
 * engine via krkr_set_clock. C linkage — defined in C, consumed from C++. */
extern "C" long long ons_libretro_game_clock_ms(void);
/* The frontend callback table, exported by libretro.cpp. Used to read the
 * frontend's system directory, exported to the engine as its override path. */
extern "C" retro_environment_t environ_cb_global;
/* libretro input device constants (from libretro.h) */
#define KRKR_RETRO_DEVICE_POINTER                                              \
   6 /* RETRO_DEVICE_POINTER (was wrongly 0x104) */
#define KRKR_RETRO_DEVICE_JOYPAD 1
#define KRKR_RETRO_DEVICE_ID_JOYPAD_L 10
#define KRKR_RETRO_DEVICE_ID_JOYPAD_R 11
#define KRKR_RETRO_DEVICE_ID_POINTER_X 0
#define KRKR_RETRO_DEVICE_ID_POINTER_Y 1
#define KRKR_RETRO_DEVICE_ID_POINTER_PRESSED 2
/* The engine, as this core sees it: these are the linked-in definitions the
 * core's exported control entry points forward to, plus the hooks krkr_init
 * installs into it. C++ entry points (the krkrsdl2_* five) use the engine's own
 * C++ linkage. */
extern "C"
{
   extern volatile int krkrsdl2_auto_mode;
   extern volatile int krkrsdl2_engine_stopped;
   extern void krkr_set_fastforward(int f);
   extern void krkr_notify_poll(long long ms);
   extern void krkr_set_audio_suspended(int suspended);
   extern void krkr_set_resolution(int w, int h);
   extern void krkr_set_clock(long long (*fn)(void));
   extern void krkrsdl2_prepare_restart(void);
   /* The presentation seam's engine-side entry points. The core installs its
    * callbacks through the second one and consumes frames through the first, so
    * the core never has to include an engine header and never learns what the
    * engine's own types are. See visual/Present.h for the host contract. */
   extern void krkr_install_present_callbacks(void);
   extern void krkr_set_present_size(int w, int h);
   extern int krkr_take_frame(const unsigned char **pixels, int *w, int *h,
                              int *pitch, unsigned long long *serial);
   extern void krkrsdl2_request_quit(void);
   extern volatile int krkrsdl2_pending_select;

   /* The engine-neutral text plane (engine_bridge.h). Declared by hand rather
    * than by including that header, because it pulls in SDL 1.2's SDL.h whose
    * SDL_PushEvent/SDL_GetError clash with the real SDL2 ones this file
    * deliberately binds to (see the RENAME_SDL_SYMBOLS block above). The layout
    * must match VNovelTextPlane exactly - the same vein as every other engine
    * symbol this file declares for itself. */
   typedef struct VNovelTextPlane
   {
      const void *pixels;
      int width;
      int height;
      int pitch;
      int changed_x, changed_y, changed_w, changed_h;
      int origin_x, origin_y;
      /* Placement context (engine_bridge.h): where this surface sits in the
       * window, plus whether the shell asked for it. Only KiriKiri fills these
       * meaningfully - its publications are line-box fragments. */
      int plane_x, plane_y;
      /* Window size, sent with each publication so the shell can size its page
       * before it has presented a frame (see engine_bridge.h). */
      int window_w, window_h;
      int splice;
      int restart;
      unsigned long long serial;
   } VNovelTextPlane;

   extern void vnovel_publish_text_plane(const VNovelTextPlane *plane);

   /* The engine-neutral text RUN stream (engine_bridge.h). Same hand-declared
    * shape and the same reason as VNovelTextPlane above; the layout must match
    * VNovelTextRun exactly. */
   typedef struct VNovelTextRun
   {
      const unsigned short *text;
      int text_length;
      int x, y;
      unsigned int color;
      int opacity;
      int antialias;
      int shadow_level;
      unsigned int shadow_color;
      int shadow_offset_x, shadow_offset_y;
      /* Producer kind (engine_bridge.h): this path DRAWS, so it is
       * VNOVEL_RUN_DRAW. The look-ahead shim sets VNOVEL_RUN_PARSE. */
      int kind;
      unsigned long long serial;
   } VNovelTextRun;

   extern void vnovel_publish_text_run(const VNovelTextRun *run);

   /* The shell's splice request, routed through the bridge (engine_bridge.h). */
   typedef void (*vnovel_text_plane_splice_fn)(int enabled);
   extern vnovel_text_plane_splice_fn vnovel_text_plane_splice_cb;
   /* The artwork-only request (engine_bridge.h). Same hand-declared shape. */
   typedef void (*vnovel_split_view_fn)(int enabled);
   extern vnovel_split_view_fn vnovel_split_view_cb;
   /* Apply requests made before these callbacks existed (engine_bridge.h). */
   extern void vnovel_apply_pending_requests(void);
   extern float *SDL_libretro_audio_buf;
   extern volatile int SDL_libretro_audio_write;
   extern volatile int SDL_libretro_audio_read;
   extern int SDL_libretro_audio_capacity;
   extern int SDL_libretro_audio_freq;
   extern int SDL_libretro_audio_channels;
   extern int SDL_PushEvent(void *ev);
   extern const char *SDL_GetError(void);
}
extern void krkrsdl2_pre_init_platform(void);
extern void krkrsdl2_run_main_loop(void);
extern void krkrsdl2_cleanup(void);
extern void krkrsdl2_convert_set_args(int argc, char **argv);
extern bool krkrsdl2_init_platform(void);

/* Whether the engine is available to call. krkr_init records that it has bound
 * the linked engine; guards read this so a torn-down engine is not poked. */
static bool krkr_static_loaded = false;
static inline bool krkr_engine_present(void)
{
   return krkr_static_loaded;
}

// Audio capture is in krkrsdl2's FAudio_platform_sdl2.c (SDL_libretro_audio_* globals)

static pthread_t krkr_thread;
static volatile bool krkr_running               = false;
static char krkr_game_path[PATH_MAX_LENGTH]     = "";
/* Optional ARCHIVE FILE, when the game is a single executable carrying an
 * embedded XP3. Kept apart from krkr_game_path because that one is chdir'd to
 * and MUST be a directory; this one is handed to the engine as a storage
 * argument instead. See krkr_set_archive_file. */
static char krkr_archive_file[PATH_MAX_LENGTH]  = "";
static char krkr_personal_path[PATH_MAX_LENGTH] = "";

/* Engine era for the KiriKiri path, set from the frontend's core option before
 * the engine thread starts. "auto"/unset means let the engine decide: it reads
 * the game's own .cf (enginecompat) and falls back to this build's defaults. A
 * named era is exported as KRKRSDL2_COMPAT (see krkr_set_engine_compat). */
static volatile int *krkr_auto_mode_ptr         = NULL;
static volatile int *krkr_engine_stopped_ptr    = NULL;

/* The presented frame, owned by this file
 *
 * The engine hands its finished composite to the callback below, which copies it
 * here. kiri_frame() then passes this to retro_video_refresh_t. The copy exists
 * because the engine's buffer is only valid for the duration of the callback and
 * the two run on different threads; it is one memcpy per present, replacing the
 * previous arrangement where the core read an SDL_Surface out of the window by
 * struct offset and re-uploaded a dirty rectangle through SDL_UpdateTexture.
 *
 * krkr_frame_lock guards all four fields. Held only for a memcpy on the engine
 * side and a memcpy out on the core side. */
static pthread_mutex_t krkr_frame_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned char *krkr_frame_pixels = NULL;  /* owned copy, BGRA */
static size_t         krkr_frame_capacity = 0;   /* bytes allocated */
static int            krkr_frame_w = 0, krkr_frame_h = 0, krkr_frame_pitch = 0;
static unsigned long long krkr_frame_serial = 0;     /* last presented serial */
static int            krkr_frame_dirty = 0;      /* 1 when a new frame is waiting */

/* No plane statics here: the shell's sink (vnovel_text_plane_sink, reached
 * through vnovel_publish_text_plane) owns the captured text plane. The engine's
 * callback translates the plane into the bridge's shape and publishes it, so
 * there is one copy and one path. */

/* The size the core wants the engine to render at, stated before the engine
 * builds its primary layer. 0,0 means the engine chooses. Guarded by
 * krkr_frame_lock alongside the frame, since both are read from the engine
 * thread and written from the core's. */
static int            krkr_present_req_w = 0, krkr_present_req_h = 0;

/* Normalise a filesystem path in place to the engine's convention.
 *
 * One internal representation, converted only at the boundary: the engine's
 * storage layer and every path helper it uses are written against forward
 * slashes and accept both forms on Windows (Win32 opens either separators), so
 * emitting a single form keeps the strings the engine and its TJS scripts see
 * identical across platforms. That matters here because these paths travel into
 * TJS and come back as storage names, where a stray backslash would not match a
 * forward-slash name — the classic case being a name that silently fails to
 * resolve rather than erroring.
 *
 * Call this on anything built in C++ that is about to be handed to the engine.
 */
static void krkr_normalize_path(char *path)
{
   if (!path)
      return;
   for (char *p = path; *p; ++p)
      if (*p == '\\')
         *p = '/';
}

/* Whether a directory exists. Used to decide between the two data directories
 * without guessing: a path that exists but lacks the polyfill is not a usable
 * engine data dir, so the loader would fail later with a much less clear
 * message. stat() is the portable check; <sys/stat.h> is available on both
 * targets through the CRT/mingw. */
static bool krkr_dir_exists(const char *path)
{
   struct stat st;
   return path && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}
/* Export one env var from a frontend option value. When `value` is NULL, empty,
 * or one of the comma/space-separated keywords in `empty_words`, the var is
 * unsetenv'd; otherwise it is setenv'd verbatim. An unset leaves a value the
 * launcher exported in place where the engine reads it (KRKRSDL2_* are read on
 * the engine thread during start-up, after the option setter returns), so a
 * developer driving the core through the environment still steers the build. */
static void krkr_export_env(const char *name, const char *value,
                            const char *empty_words)
{
   if (!value || !value[0])
      goto unset;
   if (empty_words)
   {
      const char *p = empty_words;
      for (;;)
      {
         const char *comma = strchr(p, ',');
         const char *space = strchr(p, ' ');
         const char *end;
         if (comma && (!space || comma < space))
            end = comma;
         else if (space)
            end = space;
         else
            end = p + strlen(p);
         if ((size_t)(end - p) == strlen(value) &&
             strncmp(p, value, end - p) == 0)
            goto unset;
         if (!*end)
            break;
         p = end + 1;
      }
   }
   setenv(name, value, 1);
   return;
unset:
   unsetenv(name);
}

/* The dummy SDL drivers and the libretro-target marker. Set before the engine
 * thread starts (krkr_init) and again inside krkr_main before init_platform: the
 * engine's SDL2 reads them once at video/audio init. */
static void krkr_export_dummy_drivers(void)
{
   setenv("SDL_VIDEODRIVER", "dummy", 1);
   setenv("SDL_AUDIODRIVER", "dummy", 1);
   setenv("KRKRSDL2_LIBRETRO", "1", 1);
}

/* Copy a path into a fixed buffer and normalise it to the engine's forward-slash
 * convention. Truncates to n-1 and NUL-terminates. The engine's storage layer and
 * TJS path helpers are written against forward slashes and accept both forms on
 * Windows (Win32 opens either separator), so emitting one form keeps the strings
 * the engine and its scripts see identical across platforms — a stray backslash
 * in a name that travels into TJS and back as a storage name would silently fail
 * to resolve rather than erroring. */
static void krkr_copy_path(char *dst, size_t n, const char *src)
{
   if (!dst || n == 0)
      return;
   if (!src)
      src = "";
   strncpy(dst, src, n - 1);
   dst[n - 1] = 0;
   krkr_normalize_path(dst);
}

/* The options the engine is ALWAYS started with, each with the reason it is
 * mandatory. A table rather than an inline literal at the call site, so the set
 * is one reviewable list and an entry cannot be dropped by accident: what
 * matters is not "does the engine read this" but "does anything read this".
 *
 * `-debugwin` is read by GAME SCRIPTS. The stock startup.tjs every KiriKiri
 * title ships opens with
 *
 *     if(System.getArgument("-debugwin") == void) System.exit();
 *
 * - a console check that quits unless the option is present, because the
 * original engine always passed it. `System.getArgument` reads the engine's
 * option table, so a bare name is enough and the value is never inspected; for
 * anything else the parser appends "=yes".
 *
 * `-forcelog=clear` makes TVPSetLogLocation() call TVPStartLogToFile(), which
 * drains the accumulated important-log queue into <datapath>/krkr.console.log as
 * its first act. Without it every TVPAddImportantLog line (the per-game compat
 * mask's provenance, each plugin's override disposition, whether a resource
 * override directory registered and how many files it found) stays in an
 * in-memory deque this tree never drains, so a run says nothing about any of it.
 * `clear` starts a fresh file each run rather than appending.
 *
 * `-readencoding` and `-wsfreq` state what the engine cannot infer: the
 * scenarios are Shift_JIS, and the mixer runs at 48 kHz.
 *
 * NOT here: `-evalcontext=global`, which used to be hardcoded. The engine
 * consults its option stock before the compatibility mask is applied and the
 * mask yields to an explicit option, so a value here would decide the post-!
 * operator mode for every game while the switch read as configurable; the era
 * switches belong solely to the mask
 * (deps/krkrsdl2/src/core/base/sdl2/CompatProfile.cpp). */
#define KRKR_MANDATORY_ARGS                                                      \
   "-debugwin",           /* read by the game's startup.tjs; it exits without it */\
   "-forcelog=clear",     /* the important-log queue -> krkr.console.log */         \
   "-readencoding=Shift_JIS",                                                      \
   "-wsfreq=48000"

static void *krkr_main(void *data)
{
   chdir(krkr_game_path);
   /* Slots: name/args, then the optional appends (-datapath, the embedded-XP3
    * file), and the terminating NULL. */
   char *argv[12] = {(char *)"krkrsdl2", KRKR_MANDATORY_ARGS,
                     NULL, NULL, NULL, NULL, NULL};
   int argc = 1 + 4; /* name + KRKR_MANDATORY_ARGS */
   /* The engine's data path: without this it defaults on POSIX to
    * SDL_GetPrefPath(NULL, "krkrsdl2"), a single machine-wide directory shared
    * by every title, so two games (or two builds of one) read and write each
    * other's system variables and a half-written file makes loadSystemVariables
    * fail with "the system-variable data and its backup are both corrupt". The
    * frontend's per-game save directory is handed to us through
    * RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY. */
   char datapath_arg[PATH_MAX_LENGTH + 10];
   if (krkr_personal_path[0])
   {
      snprintf(datapath_arg, sizeof(datapath_arg), "-datapath=%s", krkr_personal_path);
      argv[argc++] = datapath_arg;
      argv[argc] = NULL;
   }
   /* No -enginecompat argument: the era reaches the engine through
    * KRKRSDL2_COMPAT, which is what the compatibility mask reads (see
    * krkr_set_engine_compat). An argument here would be a second mechanism for
    * the same thing, and the two would disagree the moment one changed. */
   /* An embedded-XP3 title: name the FILE. The engine appends the archive
    * delimiter itself and opens it as storage; its XP3 reader then finds the
    * mark at a paragraph boundary and reads in place (see
    * TVPGetXP3ArchiveOffset). Last, so it wins over the directory scan. */
   if (krkr_archive_file[0])
   {
      argv[argc++] = krkr_archive_file;
      argv[argc] = NULL;
   }
   krkrsdl2_convert_set_args(argc, argv);
   /* Save location: the engine resolves System.personalPath (used by the game
     * for its "Fate_Savedata" folder) from this env var, which the frontend
     * fills with RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY. */
   if (krkr_personal_path[0])
      setenv("KRKRSDL2_PERSONAL_PATH", krkr_personal_path, 1);
   /* Polyfill data dir (system_polyfill/*.tjs + startup.tjs). Only set when
     * unset: an explicit env var is a launcher override and wins. Defaults to
     * /usr/share/krkrsdl2 on POSIX; Windows has no /usr/share, so a system/
     * directory beside the game directory (already chdir'd to) is the natural
     * package location. Separator normalised to '/' — see krkr_copy_path. */
   if (!getenv("KRKRSDL2_DATA_PATH"))
   {
#ifdef _WIN32
      char cwd[PATH_MAX_LENGTH];
      if (getcwd(cwd, sizeof(cwd)))
      {
         char data_path[PATH_MAX_LENGTH];
         snprintf(data_path, sizeof(data_path), "%s/system", cwd);
         krkr_normalize_path(data_path);
         setenv("KRKRSDL2_DATA_PATH", data_path, 1);
      }
#else
      setenv("KRKRSDL2_DATA_PATH", "/usr/share/krkrsdl2", 1);
#endif
   }
   /* The plugin search path is assembled by the engine from ExePath() (see
    * TVPLoadPluigins), not from an environment variable, so nothing is set
    * here. */
   if (krkr_engine_present())
      krkrsdl2_pre_init_platform();
   krkr_export_dummy_drivers();
   if (krkrsdl2_init_platform())
   {
      fprintf(stderr, "krkr_main: init_platform failed\n");
      krkr_running = false;
      return NULL;
   }
   krkr_running = true;
   krkrsdl2_run_main_loop();
   krkr_running = false;
   return NULL;
}

extern "C"
{
   typedef void *EngineHandle;

   EngineHandle krkr_create(void)
   {
      return (EngineHandle)1;
   }

   int krkr_init(EngineHandle engine)
   {
      if (krkr_running)
         return 0;
#ifdef _WIN32
      /* Module base under ASLR: a PE is not loaded at its link-time base, so
       * this is what turns a crash address from the OS or a debugger into a
       * file offset that can be resolved against the map file. One line, and
       * the only way to symbolise a Windows crash report. */
      {
         HMODULE mod = NULL;
         if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                (LPCSTR)&krkr_init, &mod))
            fprintf(stderr, "MODULE-BASE vnovel=%p\n", (void *)mod);
      }
#endif

      /* The engine is linked into this core: nothing to load, nothing to look
       * up. Mark it present; every reference below binds to the linked symbol
       * directly. */
      krkr_static_loaded = true;

      /* Export the frontend's system ("BIOS") directory so the engine looks
       * for overrides.txt there first (KRKRSDL2_OVERRIDE_PATH). Single-file
       * deployment is why: a core that must be one file cannot ship the table
       * beside itself, so the user-editable copy lives where the frontend
       * already keeps per-system files, and the packaged one stays the
       * fallback. Only set when unset: an environment value is an explicit
       * override and must not be replaced by the default.
       *
       * Deliberately NOT applied to KRKRSDL2_DATA_PATH. That is the root the
       * polyfill is loaded from, and the distribution installs the polyfill to
       * a fixed location (/usr/share/krkrsdl2) while placing only the override
       * table in the BIOS directory. Pointing DATA_PATH at the BIOS directory
       * would make the engine look for <bios>/system_polyfill/, which nothing
       * creates, and the scenario parser would go missing on a packaged
       * install. A launcher that ships its own copy sets DATA_PATH itself. */
      {
         const char *sysdir = NULL;
         if (environ_cb_global &&
             !environ_cb_global(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY,
                                &sysdir))
            sysdir = NULL;
         if (sysdir && *sysdir && !getenv("KRKRSDL2_OVERRIDE_PATH"))
            setenv("KRKRSDL2_OVERRIDE_PATH", sysdir, 1);
         /* The engine's own data dir. Two locations are possible and both are
          * supported, so a distribution and a hand-placed copy can coexist:
          *
          *   <frontend system dir>/vnovel   the distributable copy. It sits with
          *                                  the frontend's per-system files, so
          *                                  it travels with the share and needs
          *                                  no per-machine setup. Preferred when
          *                                  present.
          *   the compiled-in default        where a packaged install puts it
          *                                  (/usr/share/krkrsdl2 on Linux).
          *
          * Checked by looking for the polyfill, because that is what the loader
          * needs: an overrides.txt alone is not enough to run a KiriKiri game.
          * Only set when unset — an explicit environment value is the user's
          * decision and wins. */
         if (sysdir && *sysdir && !getenv("KRKRSDL2_DATA_PATH"))
         {
            char cand[PATH_MAX_LENGTH];
            snprintf(cand, sizeof(cand), "%s/vnovel/system_polyfill", sysdir);
            krkr_normalize_path(cand);
            if (krkr_dir_exists(cand))
            {
               char dpath[PATH_MAX_LENGTH];
               snprintf(dpath, sizeof(dpath), "%s/vnovel", sysdir);
               krkr_normalize_path(dpath);
               setenv("KRKRSDL2_DATA_PATH", dpath, 1);
            }
         }
      }

      krkr_auto_mode_ptr       = &krkrsdl2_auto_mode;
      krkr_engine_stopped_ptr  = &krkrsdl2_engine_stopped;

      /* Install the host side of the presentation seam. The engine hands its
       * finished composite directly, with geometry and pitch stated explicitly.
       * The callback runs on the engine thread, inside its composite, so all it
       * does is copy into the core's own frame buffer under the lock; the
       * retro_video_refresh_t call happens on the core's thread in kiri_frame(). */
      {
         extern void krkr_install_present_callbacks(void);
         krkr_install_present_callbacks();
      }

      /* Install the core's single game clock into the engine: every KiriKiri
       * timer/transition/script wait then reads the same dilated time the core
       * computes (fast-forward included), instead of the engine keeping its own
       * clock state. Must happen before the engine thread starts. */
      krkr_set_clock(ons_libretro_game_clock_ms);
      /* Re-arm the engine's process-global one-shot latches before the new thread
     * starts. On the very first load the engine is fresh and every reset below
     * is a no-op; on a restart (the .so stays mapped, so the previous run's
     * globals survive) this is what lets StartApplication run a second time.
     * The engine owns the individual flags; we call one entry point. */
      krkrsdl2_prepare_restart();
      krkr_export_dummy_drivers();

      if (pthread_create(&krkr_thread, NULL, krkr_main, NULL) != 0)
      {
         fprintf(stderr, "Failed to create krkrsdl2 thread\n");
         return -1;
      }
      return 0;
   }

   void krkr_executeLabel(EngineHandle engine)
   {
   }
   void krkr_eventLoop(EngineHandle engine)
   {
   }
   void krkr_resetCommand(EngineHandle engine)
   {
   }
   void krkr_setArchivePath(EngineHandle engine, const char *path)
   {
      krkr_copy_path(krkr_game_path, sizeof(krkr_game_path), path);
   }

   /* The game is THIS FILE: a single executable with an embedded XP3 archive.
    *
    * Handed to the engine as a storage argument (see krkr_main), which its
    * project-directory code turns into `path>` -- "open this as an archive".
    * Deliberately NOT the channel krkr_setArchivePath uses: that one is chdir'd
    * to, and a file there breaks the working directory (measured: chdir fails,
    * no project directory is selected, the run publishes nothing). */
   void krkr_set_archive_file(const char *path)
   {
      if (!path || !path[0])
      {
         krkr_archive_file[0] = 0;
         return;
      }
      krkr_copy_path(krkr_archive_file, sizeof(krkr_archive_file), path);
   }
   extern "C" void krkr_set_save_path(const char *path);
   void krkr_setSavePath(EngineHandle engine, const char *path)
   {
      krkr_set_save_path(path);
   }
   void krkr_setKeyEXE(EngineHandle engine, const char *path)
   {
   }
   void krkr_setDebugLevel(EngineHandle engine, int level)
   {
   }
   void krkr_setTextSpeed(EngineHandle engine, int speed)
   {
   }
   int krkr_getWidth(EngineHandle engine)
   {
      return 640;
   }
   int krkr_getHeight(EngineHandle engine)
   {
      return 480;
   }
   int krkr_isAutoMode(EngineHandle engine)
   {
      return krkr_auto_mode_ptr ? (*krkr_auto_mode_ptr == 2) : 0;
   }
   int krkr_isSkipMode(EngineHandle engine)
   {
      return krkr_auto_mode_ptr ? (*krkr_auto_mode_ptr == 1) : 0;
   }
   void krkr_dumpState(EngineHandle engine, void *file)
   {
   }

   volatile int *krkr_get_auto_mode_ptr(void)
   {
      return krkr_auto_mode_ptr;
   }

   /* Set by the engine when the game terminated itself (KAG `[close]`): the core
 * then blanks the video and parks instead of letting the frontend quit. */
   extern "C" volatile int *krkr_get_engine_stopped_ptr(void)
   {
      return krkr_engine_stopped_ptr;
   }

   /* krkr_set_fastforward / krkr_notify_poll / krkr_set_audio_suspended /
    * krkr_set_resolution are the engine's own exported control entry points
    * (SDLApplication.cpp). libretro.cpp declares and calls them directly; with
    * the engine linked in, the linker binds those calls to the engine, so no
    * dispatcher belongs here. */

   /* Frontend save directory (RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY), exported to
 * the engine as KRKRSDL2_PERSONAL_PATH so System.personalPath resolves there. */
   extern "C" void krkr_set_save_path(const char *path)
   {
      if (!path)
      {
         krkr_personal_path[0] = 0;
         return;
      }
      krkr_copy_path(krkr_personal_path, sizeof(krkr_personal_path), path);
   }

   /* The host side of the presentation seam (visual/Present.h). The engine calls
    * this once per finished composite, on its own thread, with a buffer valid
    * only for the duration of the call. This takes a copy for kiri_frame() to
    * hand the frontend and records the geometry: the frontend's reported video
    * size comes from it, so the engine's real resolution (800x600 for this title)
    * reaches RetroArch instead of the 640x480 guessed before the engine exists.
    * The engine describes a buffer; nothing here knows it is SDL, or that a
    * window exists. */
   static bool krkr_present_cb(const tTVPPresentBuffer *buffer)
   {
      if (!buffer || !buffer->Pixels || buffer->Width <= 0 || buffer->Height <= 0 ||
          buffer->Pitch <= 0)
         return false;

      /* Bound the geometry: this comes from the engine thread, and a bad pitch
       * would turn the copy below into a heap overrun. A genuine widescreen mode
       * is wide and short; garbage is negative or gigantic. */
      if (buffer->Width > 8192 || buffer->Height > 2160 ||
          buffer->Pitch < buffer->Width * 4 || buffer->Pitch > buffer->Width * 8)
         return false;

      size_t needed = (size_t)buffer->Pitch * (size_t)buffer->Height;

      pthread_mutex_lock(&krkr_frame_lock);
      if (needed > krkr_frame_capacity)
      {
         unsigned char *np = (unsigned char *)realloc(krkr_frame_pixels, needed);
         if (!np)
         {
            /* Keep the previous frame rather than publishing a torn one. The
             * engine keeps running; the next present retries. */
            pthread_mutex_unlock(&krkr_frame_lock);
            return false;
         }
         krkr_frame_pixels   = np;
         krkr_frame_capacity = needed;
      }
      memcpy(krkr_frame_pixels, buffer->Pixels, needed);
      krkr_frame_w      = buffer->Width;
      krkr_frame_h      = buffer->Height;
      krkr_frame_pitch  = buffer->Pitch;
      krkr_frame_serial = (uint64_t)buffer->FrameSerial;
      krkr_frame_dirty  = 1;
      pthread_mutex_unlock(&krkr_frame_lock);
      return true;
   }

   /* Asks the frontend for the size it wants the engine to render at, before
    * the engine builds its primary layer, so the game's own
    * System.screenWidth/screenHeight are right from the first frame rather than
    * corrected after it. The value is stashed by the core through
    * krkr_set_present_size() during load. */
   static void krkr_query_size_cb(int *w, int *h, void *hostdata)
   {
      (void)hostdata;
      if (!w || !h)
         return;
      pthread_mutex_lock(&krkr_frame_lock);
      *w = krkr_present_req_w;
      *h = krkr_present_req_h;
      pthread_mutex_unlock(&krkr_frame_lock);
   }

   /* The core's way of stating the size it wants, called before the engine
    * starts. A zero pair means "the engine chooses". */
   extern "C" void krkr_set_present_size(int w, int h)
   {
      pthread_mutex_lock(&krkr_frame_lock);
      if (w > 0 && h > 0 && w <= 8192 && h <= 2160)
      {
         krkr_present_req_w = w;
         krkr_present_req_h = h;
      }
      else
      {
         krkr_present_req_w = 0;
         krkr_present_req_h = 0;
      }
      pthread_mutex_unlock(&krkr_frame_lock);
   }

   /* Frame pacing. The engine calls this when it has nothing to do until the
    * frontend consumes a frame. The libretro core paces from kiri_frame(), which
    * runs on the core's thread and returns once per frontend frame, so this side
    * does not block at all: the engine's own composite is throttled by the
    * frame it is about to hand over.
    *
    * A no-op callback rather than NULL, so the engine does not fall into its
    * "run as fast as possible" mode and spin a core's worth of CPU between
    * frames. */
   static void krkr_wait_present_cb(void *hostdata)
   {
      (void)hostdata;
   }

   /* The text plane (visual/Present.h). The engine publishes a surface holding
    * only the text it drew, separately from the frame, when a host asks for it.
    *
    * Nothing consumes it yet: this installs the callback so the plumbing is
    * exercised, and copies the plane the way the frame is copied, so the two
    * outputs can be compared. A reader/window mode is what will actually use
    * it (see the text-plane design note); until then the core compositing it
    * into the frame is a no-op by construction, because the frame already has
    * the text on it.
    *
    * The plane is a page and stays valid after this returns, unlike the frame
    * buffer, so it is copied under its own lock rather than held by pointer. */
   /* Splice mode: whether the shell wants the placement context (plane_x/
    * plane_y). Kept here, not read from the bridge per publication, so the
    * per-glyph path is a single bool load; the same flag is handed to the
    * engine so its own per-publication walk is skipped entirely when off. */
   static volatile int kiri_text_plane_splice = 0;

   /* The C ABI entry point for the splice switch, reached by the bridge's
    * callback below. Kept here so the core never names an engine symbol: every
    * crossing uses the krkr_* ABI, as the rest of this file does. */
   extern "C" void krkr_set_text_plane_splice(int enabled)
   {
      kiri_text_plane_splice = enabled ? 1 : 0;
      /* The engine's own half: gates the per-publication parent-chain walk that
       * produces PlaneX/PlaneY (C++ linkage, matching PresentIntf.h). */
      TVPSetTextPlaneSplice(enabled ? true : false);
   }

   /* Split view: compose the frame from artwork only, so the core can put the
    * text plane beside it at a different resolution. Same shape as the splice
    * switch above - one C ABI entry, one engine-side flag. */
   extern "C" void krkr_set_split_view(int enabled)
   {
      TVPSetSplitViewArtworkOnly(enabled ? true : false);
   }

   static bool krkr_text_plane_cb(const tTVPTextPlane *plane)
   {
      /* pitch KEEPS ITS SIGN. The engine's bitmaps are bottom-up, so a negative
       * stride is the row direction, not an error: it publishes a real 808x48
       * text surface with pitch -3232. Bound the MAGNITUDE only, below. */
      if (!plane || !plane->Pixels || plane->Width <= 0 || plane->Height <= 0 ||
          plane->Pitch == 0)
         return false;

      /* Same bounds as the frame, on the MAGNITUDE: garbage geometry would turn
       * a copy into a heap overrun, and this comes from the engine thread. */
      {
         const int ap = plane->Pitch < 0 ? -plane->Pitch : plane->Pitch;
         if (plane->Width > 8192 || plane->Height > 8192 ||
             ap < plane->Width * 4 || ap > plane->Width * 8)
            return false;
      }

      /* Translate into the engine-neutral shape and hand it to the shell
       * through the bridge, which owns the copy (vnovel_text_plane_sink in
       * src/libretro.cpp, reached only through vnovel_publish_text_plane). */
      VNovelTextPlane vp;
      vp.pixels     = plane->Pixels;
      vp.width      = plane->Width;
      vp.height     = plane->Height;
      vp.pitch      = plane->Pitch;
      vp.changed_x  = plane->ChangedX;
      vp.changed_y  = plane->ChangedY;
      vp.changed_w  = plane->ChangedWidth;
      vp.changed_h  = plane->ChangedHeight;
      vp.origin_x   = plane->OriginX;
      vp.origin_y   = plane->OriginY;
      /* Where this fragment (a line box) sits in the window. Meaningful only
       * while splice mode is on, which is what turns the engine's own walk on;
       * see krkr_set_text_plane_splice below. */
      vp.plane_x    = plane->PlaneX;
      vp.plane_y    = plane->PlaneY;
      vp.splice     = kiri_text_plane_splice;
      /* The window this fragment lives in, so the shell can size its page
       * without waiting for a frame of its own. The engine publishes its opening
       * burst of text during start-up, before the shell has presented anything,
       * so the size is reported here on the same thread and in the same message:
       * always available when a fragment is. 0 until the engine has composited a
       * frame; the shell then still has the frame path as a fallback. */
      vp.window_w   = krkr_frame_w;
      vp.window_h   = krkr_frame_h;
      vp.restart    = plane->Restart ? 1 : 0;
      vp.serial     = (unsigned long long)plane->PlaneSerial;

      vnovel_publish_text_plane(&vp);
      return true;
   }

   /* The engine's text-run callback -> the shell's. One direction, no logic
    * beyond the type translation the bridge exists for: the engine's
    * tTVPTextRun and the bridge's VNovelTextRun carry the same fields, and
    * keeping the translation here means the core never includes an engine
    * header (the same rule every other crossing in this file follows).
    *
    * The text pointer is NOT copied: both sides document it as valid for the
    * duration of the call only, and the shell copies what it keeps. */
   static bool krkr_text_run_cb(const tTVPTextRun *run)
   {
      if (!run || !run->Text || run->TextLength <= 0)
         return false;

      VNovelTextRun vr;
      vr.text            = (const unsigned short *)run->Text;
      vr.text_length     = run->TextLength;
      vr.x               = run->X;
      vr.y               = run->Y;
      vr.color           = run->Color;
      vr.opacity         = run->Opacity;
      vr.antialias       = run->Antialias ? 1 : 0;
      vr.shadow_level    = run->ShadowLevel;
      vr.shadow_color    = run->ShadowColor;
      vr.shadow_offset_x = run->ShadowOffsetX;
      vr.shadow_offset_y = run->ShadowOffsetY;
      /* Drawn, not parsed: this callback fires when the engine renders the
       * characters, so the run is paced and carries real position and style. */
      vr.kind            = 0; /* VNOVEL_RUN_DRAW */
      vr.serial          = (unsigned long long)run->RunSerial;

      vnovel_publish_text_run(&vr);
      return true;
   }

   extern "C" void krkr_install_present_callbacks(void)
   {
      TVPSetPresentCallback(krkr_present_cb, NULL);
      TVPSetQuerySurfaceSizeCallback(krkr_query_size_cb, NULL);
      TVPSetWaitForPresentCallback(krkr_wait_present_cb, NULL);
      TVPSetTextPlaneCallback(krkr_text_plane_cb, NULL);
      TVPSetTextRunCallback(krkr_text_run_cb, NULL);
      /* Only KiriKiri has a placement to give (its publications are line-box
       * fragments), so it is the only engine that fills this in; the shell's
       * request is routed to krkr_set_text_plane_splice (see above). */
      vnovel_text_plane_splice_cb = krkr_set_text_plane_splice;
      /* Likewise the split view: only KiriKiri composites frame and text in one
       * pass, so only it has an artwork-only mode to switch on. */
      vnovel_split_view_cb = krkr_set_split_view;
      /* The core requested these BEFORE it started the engine thread (it has
       * to: the engine draws its opening burst during start-up), which is
       * before these callbacks existed. Apply whatever it asked for now that
       * they do - without this the request is silently dropped and the engine
       * runs with both switches off. */
      vnovel_apply_pending_requests();
   }

   /* Accessors for kiri_frame(). The serial is spelled identically to the
    * declaration above: uint64_t is unsigned long on this target, not unsigned
    * long long, and mixing the two is a conflicting C declaration. */
   extern "C" int krkr_take_frame(const unsigned char **pixels, int *w, int *h,
                                  int *pitch, unsigned long long *serial)
   {
      int have = 0;
      pthread_mutex_lock(&krkr_frame_lock);
      if (krkr_frame_dirty && krkr_frame_pixels)
      {
         *pixels  = krkr_frame_pixels;
         *w       = krkr_frame_w;
         *h       = krkr_frame_h;
         *pitch   = krkr_frame_pitch;
         *serial  = krkr_frame_serial;
         krkr_frame_dirty = 0;
         have = 1;
      }
      pthread_mutex_unlock(&krkr_frame_lock);
      return have;
   }

   /* Diagnostic probes, from the frontend's core option or from the shared
    * debug mask's KiriKiri slice.
    *
    * VNOVEL_DEBUG carries every module's bits in one word (see
    * src/vnovel_debug.h); the KiriKiri slice is bits 24-39. The engine's own
    * DebugFlags table is self-contained - that engine builds standalone with its
    * own CMake and cannot include the core's headers - so the core hands it the
    * slice as a raw word through the channel it already reads: KRKRSDL2_DEBUG,
    * whose parser accepts a hex word. The core option stays, and a frontend
    * option wins over the environment, because a frontend cannot set the
    * environment and the option is what a RetroArch user can reach.
    *
    * The option's value is still passed through unparsed, so the engine owns its
    * name table - the core must not carry a second copy of it. */
   extern "C" void krkr_set_debug_flags(const char *spec)
   {
      if (spec && spec[0] && strcmp(spec, "none") != 0)
      {
         setenv("KRKRSDL2_DEBUG", spec, 1);
         return;
      }
      /* No option: export this mask's KiriKiri slice as a hex word, if any bit
       * is set. A value inherited from the shell still applies for a developer
       * driving the core directly. */
      char word[32];
      uint64_t slice =
            (vnovel_debug_word() >> VNOVEL_DEBUG_SHIFT_KRKR) &
            ((1ULL << VNOVEL_DEBUG_WIDTH_KRKR) - 1);
      if (!slice)
         return;
      snprintf(word, sizeof(word), "0x%llx", (unsigned long long)slice);
      setenv("KRKRSDL2_DEBUG", word, 1);
   }

   /* Engine-compatibility selection from the frontend's core option. Applied by
    * exporting the value on the channel the compatibility mask reads
    * (KRKRSDL2_COMPAT), not as an engine argument. The mask is the single owner
    * of the era switches, so the host feeds it through its own input rather than
    * through a second, competing mechanism. setenv (not a stored string)
    * because the mask reads the environment during engine start-up, on the
    * engine thread, after this returns. */
   extern "C" void krkr_set_engine_compat(const char *era)
   {
      /* "auto" or unset: do not export a spec. The engine then applies its own
       * default era (see TVP_COMPAT_DEFAULT_LEVEL) rather than an empty mask. A
       * launcher that exported KRKRSDL2_COMPAT itself still steers the build,
       * because nothing here overwrites it. */
      if (!era || !era[0] || strcmp(era, "auto") == 0)
         return;
      setenv("KRKRSDL2_COMPAT", era, 1);
   }

   /* Host resource override directory: a directory laid out like the game's
    * resource tree whose files replace the game's own for the names they match.
    * Exported (not passed as an argument) for the same reason as the compat
    * spec: the engine reads it during start-up, on its own thread, after this
    * returns. An empty/unset value unexports the directory. */
   extern "C" void krkr_set_override_dir(const char *dir)
   {
      krkr_export_env("KRKRSDL2_OVERRIDE_DIR", dir, NULL);
   }

   /* The loaded game's identity, so the engine's per-game compat table can key
    * on it. Same channel and same reason as the era above: the mask reads the
    * environment during engine start-up, on the engine's own thread.
    *
    * The value is the crc32 of the game's data file (data.xp3), as eight
    * upper-case hex digits - exactly what tools/hash-game.py fingerprints and
    * dat/vnovel.dat records, so one game has one id across the database, the
    * tools and the engine's table. The core computes it in retro_load_game,
    * and passes "" or NULL when the file could not be hashed: the engine then
    * falls through to its documented default rather than guessing. An empty
    * value unexports the id. */
   extern "C" void krkr_set_game_id(const char *id)
   {
      krkr_export_env("KRKRSDL2_GAME_ID", id, NULL);
   }

   /* Last mouse position (engine 800x600 space), for the software cursor overlay. */
   int krkr_mouse_x = -1, krkr_mouse_y = -1;
   extern "C" void krkr_get_mouse_pos(int *x, int *y)
   {
      if (x)
         *x = krkr_mouse_x;
      if (y)
         *y = krkr_mouse_y;
   }

   /* Reset as a two-phase, engine-owned handshake
 * The core posts a REQUEST to return the engine to initial conditions and
 * polls for acknowledgement; it never tears engine state down itself.
 *
 *   Phase 1 (stop): krkr_request_stop() asks the engine thread to unwind;
 *     krkr_finish_stop() waits (bounded) for that acknowledgement, then joins
 *     and tears the application down — only after the thread has exited, so it
 *     is safe.
 *   Phase 2 (start): krkr_start() re-arms the engine's process-global latches
 *     (the module is never unmapped — glibc retains it, a relocation
 *     dependency of libz/libpng/libogg/libstdc++) and re-creates the thread.
 *
 * krkr_shutdown() is the blocking form for permanent teardown (unload/deinit),
 * kept separate so a frontend exit still completes in one call.
 */
   extern "C" void krkr_request_stop(void)
   {
      if (!krkr_engine_present())
         return;
      /* Ask the engine to quit even if it is paused/idle (frontend stopped
     * polling): the engine loop checks this before its pause gate, so it can
     * always reach the terminate path. Do NOT call Application::Terminate()
     * from this thread — the engine thread reads the request and tears itself
     * down (TVPSystemUninit / delete TVPSystemControl); a concurrent Terminate
     * here races that teardown and can deadlock. The flag itself is engine
     * state; we only post the request. */
      krkrsdl2_request_quit();
   }

   /* Complete phase 1 if the engine thread has exited. Returns 1 when the engine
 * is fully stopped (joined + torn down + unpublished), 0 while it is still
 * unwinding. Idempotent-ish: only acts while an engine is loaded. */
   extern "C" int krkr_finish_stop(void)
   {
      if (!krkr_engine_present())
         return 1;
      if (krkr_running)
      {
         /* Bounded wait for the engine thread to acknowledge and unwind. */
         struct timespec ts = {0, 5 * 1000 * 1000};
         for (int i = 0; i < 400 && krkr_running; i++)
            nanosleep(&ts, NULL);
      }
      if (krkr_running)
      {
         fprintf(stderr,
                 "krkr_finish_stop: engine thread did not exit; not joining\n");
         return 0;
      }
      try
      {
         krkrsdl2_cleanup();
      }
      catch (...)
      {
      }
      void *ret = NULL;
      pthread_join(krkr_thread, &ret);
      /* The engine stays linked in; "down" is recorded by the flag the guards
       * and the next krkr_init read. */
      krkr_static_loaded = false;
      /* Unpublish the engine's pointers: they point into the (still-mapped)
     * engine, but the surface/application behind them is being torn down and
     * rebuilt. retro_run() reads them from the frontend thread while the next
     * run rebuilds the engine, so NULL makes it skip the KiriKiri video path
     * until krkr_start() republishes them. */
      krkr_auto_mode_ptr       = NULL;
      krkr_engine_stopped_ptr  = NULL;
      return 1;
   }

   /* Permanent teardown (unload/deinit): request, bounded-wait, then finish. */
   extern "C" void krkr_shutdown(void)
   {
      if (!krkr_engine_present())
         return;
      krkr_request_stop();
      if (krkr_running)
      {
         struct timespec ts = {0, 5 * 1000 * 1000};
         for (int i = 0; i < 400 && krkr_running; i++)
            nanosleep(&ts, NULL);
      }
      if (!krkr_running)
         krkr_finish_stop();
      else
         fprintf(stderr,
                 "krkr_shutdown: engine thread did not exit; returning\n");
   }

   /* Phase 2: begin a fresh run from initial conditions. krkr_init re-arms the
 * engine's one-shot latches (krkrsdl2 keeps its .so mapped across a reset, so
 * the previous run's process globals survive) and re-creates the thread. */
   extern "C" void krkr_start(void)
   {
      if (!krkr_game_path[0])
      {
         fprintf(stderr, "krkr_start: no game path; ignoring\n");
         return;
      }
      krkr_init((EngineHandle)1);
   }

   /* Input state callback from libretro (set by libretro.cpp) */
   typedef int16_t (*input_state_t)(unsigned, unsigned, unsigned, unsigned);
   static input_state_t krkr_input_state = NULL;
   static void (*krkr_input_poll)(void)  = NULL;

   extern "C" void krkr_set_input_callbacks(input_state_t state_cb,
                                            void (*poll_cb)(void))
   {
      krkr_input_state = state_cb;
      krkr_input_poll  = poll_cb;
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

   extern "C" void krkr_push_key(int sym, int down)
   {
      int n = (krkr_keyq_head + 1) % KRKR_KEYQ_SIZE;
      if (n == krkr_keyq_tail)
         return; /* full: drop */
      krkr_keyq_sym[krkr_keyq_head]  = sym;
      krkr_keyq_down[krkr_keyq_head] = (unsigned char)(down ? 1 : 0);
      krkr_keyq_head                 = n;
   }

   /* Inbound navigation intent from the development socket (select N). Called
    * on the frontend thread (retro_run), which is NOT the engine thread: TJS
    * is not thread-safe, so this only hands the ordinal to the engine, which
    * takes it in its per-frame path (SDLApplication.cpp, next to the autoMode
    * eval) and reports the verdict through vnovel_publish_nav_result. A newer
    * intent replaces an older one — the socket has one consumer, so the latest
    * select is what the user meant. KiriKiri has no EngineVTable (it is driven
    * by direct krkr_* calls), so the core calls this directly when vtable is
    * NULL rather than through vtable->pushSelect. */
   extern "C" void krkr_push_select(int ordinal)
   {
      if (ordinal <= 0)
         return;
      krkrsdl2_pending_select = ordinal;
   }

/* Input diagnostics. The core's 'input' bit (VNOVEL_DEBUG=input) traces the
 * whole input path: what the shim hands to SDL, and what the push was accepted
 * as. Each stage is a different failure mode, so both are worth keeping - a
 * push the queue refuses looks the same as a poll that never saw the press,
 * and only the return code tells them apart. Off by default. */
static bool krkr_input_debug(void)
{
   return CORE_DEBUG_ON(DEBUG_FLAG_INPUT);
}


#define KRKR_SDL_MOUSEMOTION 0x400
#define KRKR_SDL_MOUSEBUTTONDOWN 0x401
#define KRKR_SDL_MOUSEBUTTONUP 0x402
#define KRKR_SDL_KEYDOWN 0x300
#define KRKR_SDL_KEYUP 0x301
/* SDL_CONTROLLERBUTTONDOWN/UP (SDL_events.h enum, after the touch/gesture
 * block). Used to deliver the D-pad as KiriKiri *virtual pad* keys: see the
 * PAD table note below. */
#define KRKR_SDL_CONTROLLERBUTTONDOWN 0x651
#define KRKR_SDL_CONTROLLERBUTTONUP 0x652

/* SDL2 keysyms the pad dispatch emits. This file does not include the SDL2
 * headers, so spell the ABI values as named constants instead of guessing them
 * inline. In SDL2's keysym layout, keys below 0x40000000 are effectively
 * ASCII, and keys with no character carry bit 30 over their scancode:
 * SDLK_UP/DOWN/LEFT/RIGHT = 0x40000000 | SDL_SCANCODE_{UP,DOWN,LEFT,RIGHT}
 * (= SDL_SCANCODE_TO_KEYCODE). */
#define KRKR_SDLK_RETURN 0x0d                    /* SDLK_RETURN */
#define KRKR_SDLK_ESCAPE 0x1b                    /* SDLK_ESCAPE */
#define KRKR_SDLK_a 0x61                         /* SDLK_a */
#define KRKR_SDLK_s 0x73                         /* SDLK_s */
#define KRKR_SDLK_f 0x66                         /* SDLK_f */
#define KRKR_SDLK_UP (int)(0x40000000u | 82u)    /* SDL_SCANCODE_UP */
#define KRKR_SDLK_DOWN (int)(0x40000000u | 81u)  /* SDL_SCANCODE_DOWN */
#define KRKR_SDLK_LEFT (int)(0x40000000u | 80u)  /* SDL_SCANCODE_LEFT */
#define KRKR_SDLK_RIGHT (int)(0x40000000u | 79u) /* SDL_SCANCODE_RIGHT */

   /* Called from retro_run each frame to keep engine alive and forward input.
 * The dummy video driver doesn't generate events, so we inject SDL2 events. */
   void krkr_pump_events(void)
   {
      if (!krkr_engine_present() || !krkr_running)
         return;
      {
         static int sb = 0;
         if (!sb)
         {
            setvbuf(stderr, NULL, _IONBF, 0);
            sb = 1;
         }
      }
      static int (*p_SDL_PushEvent)(void *) = NULL;
      if (!p_SDL_PushEvent)
      {
         /* The event must go to the *engine's* SDL2 — the one whose event queue
         * the engine's loop blocks on. With the engine linked in, that is the
         * SDL_PushEvent the core itself links against, so bind it directly. */
         p_SDL_PushEvent = SDL_PushEvent;
      }
      if (!p_SDL_PushEvent)
         return;

      /* Drain queued keyboard events (from RetroArch's keyboard callback).
     * Independent of the pointer/joypad path below. */
      {
         while (krkr_keyq_tail != krkr_keyq_head)
         {
            int sym        = krkr_keyq_sym[krkr_keyq_tail];
            int down       = krkr_keyq_down[krkr_keyq_tail];
            krkr_keyq_tail = (krkr_keyq_tail + 1) % KRKR_KEYQ_SIZE;
            char ev[64];
            memset(ev, 0, sizeof(ev));
            *(unsigned int *)(ev + 0) =
                  down ? KRKR_SDL_KEYDOWN : KRKR_SDL_KEYUP;
            *(unsigned char *)(ev + 12) = down ? 1 : 0; /* state */
            *(unsigned char *)(ev + 13) = 0;            /* repeat */
            *(int *)(ev + 20)           = sym;          /* keysym.sym */
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
      if (!krkr_input_state)
         return;

      /* RetroArch delivers an on-screen pointer as EITHER:
     *  - RETRO_DEVICE_POINTER absolute [-32768..32767] (lightgun/pointer device), or
     *  - RETRO_DEVICE_MOUSE relative deltas (plain mouse device).
     * The core's own events.c integrates MOUSE deltas; use both here so the
     * engine sees an accurate position regardless of which RA reports. */
#define KRKR_RETRO_DEVICE_MOUSE 0x002
#define KRKR_RETRO_DEVICE_ID_MOUSE_X 0
#define KRKR_RETRO_DEVICE_ID_MOUSE_Y 1
#define KRKR_RETRO_DEVICE_ID_MOUSE_LEFT 2
#define KRKR_RETRO_DEVICE_ID_MOUSE_RIGHT 3

      /* Current engine surface size, for mapping pointer coordinates into engine
       * space. The internal rendering resolution is chosen by the game (or forced
       * via the core option), so it cannot be assumed: take it from the frame the
       * presentation seam published, which is the geometry the engine itself
       * reports. Falls back to 800x600 until the engine has published its first
       * frame. The seam keeps the host free of SDL struct layout. */
      int surf_w = 800, surf_h = 600;
      pthread_mutex_lock(&krkr_frame_lock);
      if (krkr_frame_w > 0 && krkr_frame_h > 0)
      {
         surf_w = krkr_frame_w;
         surf_h = krkr_frame_h;
      }
      pthread_mutex_unlock(&krkr_frame_lock);

      int16_t px = krkr_input_state(
            0, KRKR_RETRO_DEVICE_POINTER, 0, KRKR_RETRO_DEVICE_ID_POINTER_X);
      int16_t py = krkr_input_state(
            0, KRKR_RETRO_DEVICE_POINTER, 0, KRKR_RETRO_DEVICE_ID_POINTER_Y);
      int16_t rmx = krkr_input_state(
            0, KRKR_RETRO_DEVICE_MOUSE, 0, KRKR_RETRO_DEVICE_ID_MOUSE_X);
      int16_t rmy = krkr_input_state(
            0, KRKR_RETRO_DEVICE_MOUSE, 0, KRKR_RETRO_DEVICE_ID_MOUSE_Y);

      /* Pointer position: starts "unset" (negative) rather than at the centre of
     * the screen — with no real pointer input the core must not park the
     * software cursor dead-centre of the game. It becomes visible only once
     * the frontend actually reports pointer/mouse input (or the stick moves,
     * via the core's RetroPad->pointer synthesis). */
      static int mouse_ax = -1,
                 mouse_ay = -1; /* integrated position; -1 = unset */
      if (rmx || rmy)
      {
         if (mouse_ax < 0)
         {
            mouse_ax = surf_w / 2;
            mouse_ay = surf_h / 2;
         }
         mouse_ax += rmx;
         mouse_ay += rmy;
      }
      if (px || py)
      {
         mouse_ax = (int)((int)(px + 32768) * surf_w / 65536);
         mouse_ay = (int)((int)(py + 32768) * surf_h / 65536);
      }
      int mx = mouse_ax, my = mouse_ay;
      if (mx >= 0 && my >= 0)
      {
         if (mx > surf_w - 1)
            mx = surf_w - 1;
         if (my > surf_h - 1)
            my = surf_h - 1;
         mouse_ax     = mx;
         mouse_ay     = my;
         krkr_mouse_x = mx;
         krkr_mouse_y = my;
      }

      /* pressed: pointer-pressed, or the mouse LEFT/right buttons */
      int16_t pressed = krkr_input_state(0,
                                         KRKR_RETRO_DEVICE_POINTER,
                                         0,
                                         KRKR_RETRO_DEVICE_ID_POINTER_PRESSED);
      int16_t mleft   = krkr_input_state(
            0, KRKR_RETRO_DEVICE_MOUSE, 0, KRKR_RETRO_DEVICE_ID_MOUSE_LEFT);
      int16_t mright = krkr_input_state(0,
                                        KRKR_RETRO_DEVICE_MOUSE,
                                        0,
                                        KRKR_RETRO_DEVICE_ID_MOUSE_RIGHT) ||
                       krkr_input_state(0,
                                        KRKR_RETRO_DEVICE_JOYPAD,
                                        0,
                                        KRKR_RETRO_DEVICE_ID_JOYPAD_L);
      /* Input probe (VNOVEL_DEBUG=input). Logs the whole RetroPad/MOUSE state
       * the engine sees, but only when something is non-zero — a quiet line on
       * a quiet frame still proves the poll ran, because one heartbeat is
       * emitted every ~2 s. This distinguishes "RetroArch delivered nothing"
       * (all zeros) from "delivered but the engine ignored it". */
      if (krkr_input_debug())
      {
         {
            static int beat = 0;
            int a    = krkr_input_state(0, KRKR_RETRO_DEVICE_JOYPAD, 0, 8);
            int b    = krkr_input_state(0, KRKR_RETRO_DEVICE_JOYPAD, 0, 0);
            int st   = krkr_input_state(0, KRKR_RETRO_DEVICE_JOYPAD, 0, 3);
            int sel  = krkr_input_state(0, KRKR_RETRO_DEVICE_JOYPAD, 0, 2);
            int any = a || b || st || sel || pressed || mleft || mright;
            if (any || (++beat % 120) == 0)
               fprintf(stderr,
                       "[in] poll sw=%d sh=%d a=%d b=%d start=%d sel=%d ptr=%d "
                       "l=%d r=%d mx=%d my=%d px=%d py=%d rmx=%d rmy=%d\n",
                       surf_w, surf_h,
                       a, b, st, sel, pressed, mleft, mright, mx, my,
                       px, py, rmx, rmy);
         }
      }

      static int prev_mx = -1, prev_my = -1;
      static int prev_left = 0, prev_right = 0;

      /* Push mouse motion event if position changed (only once the pointer has
     * been set by real input — mx/my are -1 until then). */
      if (mx >= 0 && my >= 0 && (mx != prev_mx || my != prev_my))
      {
         /* SDL_MouseMotionEvent: type(4) ts(4) win(4) which(4) state(4) x(4) y(4) xrel(4) yrel(4) = 36 bytes */
         char ev[64];
         memset(ev, 0, sizeof(ev));
         *(unsigned int *)(ev + 0) = KRKR_SDL_MOUSEMOTION;
         *(int *)(ev + 20)         = mx; /* x */
         *(int *)(ev + 24)         = my; /* y */
         if (prev_mx >= 0)
         {
            *(int *)(ev + 28) = mx - prev_mx; /* xrel */
            *(int *)(ev + 32) = my - prev_my; /* yrel */
         }
         int ret = p_SDL_PushEvent(ev);
         (void)ret;
         prev_mx = mx;
         prev_my = my;
      }

      /* Push mouse button event on click state change. Right button (SDL button
     * 3) opens the KAG right-click menu; RetroArch can deliver it as the mouse
     * RIGHT button, the pointer-pressed state, or a RetroPad shoulder button.
     * L = LEFT click (confirm/advance, click a dialog button); R = RIGHT click
     * (cancel/back, the KAG right-click menu), matching the RetroPad convention
     * where the left shoulder is the affirmative action. */
      {
         int lbtn = krkr_input_state(
               0, KRKR_RETRO_DEVICE_JOYPAD, 0, KRKR_RETRO_DEVICE_ID_JOYPAD_L);
         int rbtn = krkr_input_state(
               0, KRKR_RETRO_DEVICE_JOYPAD, 0, KRKR_RETRO_DEVICE_ID_JOYPAD_R);
         /* L -> left click, R -> right click. A real mouse's right button also
         * means right-click. */
         int right = mright || rbtn;
         /* RetroArch reports POINTER_PRESSED for ANY button, so a right-click
         * also sets `pressed`. The !right guard keeps a right-click from
         * emitting a spurious LEFT press that KAG would consume as
         * advance/hide. */
         int left = mleft || lbtn || (pressed && !right);
         int btn = 0, is_down = 0;
         if (left != prev_left)
         {
            btn       = 1;
            is_down   = left;
            prev_left = left;
         }
         else if (right != prev_right)
         {
            btn        = 3;
            is_down    = right;
            prev_right = right;
         }
         if (btn && mx >= 0 && my >= 0)
         {
            /* mx/my are set only after real pointer input; never inject a
             * click at the unset (-1) position. */
            char ev[64];
            memset(ev, 0, sizeof(ev));
            *(unsigned int *)(ev + 0) =
                  is_down ? KRKR_SDL_MOUSEBUTTONDOWN : KRKR_SDL_MOUSEBUTTONUP;
            *(unsigned char *)(ev + 16) =
                  (unsigned char)btn; /* SDL button: 1=left, 3=right */
            *(unsigned char *)(ev + 17) =
                  is_down ? 1 : 0;           /* SDL_PRESSED/RELEASED */
            *(unsigned char *)(ev + 18) = 1; /* clicks */
            *(int *)(ev + 20)           = mx;
            *(int *)(ev + 24)           = my;
            if (krkr_input_debug())
            {
               static int n = 0;
               if (n < 40)
               {
                  fprintf(stderr, "krkr_pump: PUSH btn=%d down=%d at (%d,%d)\n",
                          btn, is_down, mx, my);
                  n++;
               }
            }
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
         enum
         {
            EMIT_NONE,
            EMIT_KEY,
            EMIT_WHEEL,
            EMIT_RIGHTCLICK,
            EMIT_PAD
         };
         struct
         {
            int mode;
            int code;
         } const PAD[16] = {
               /*  id            mode            code (SDL keysym, mouse button, or SDL_CONTROLLER_BUTTON_*) */
               /*  B      */ {EMIT_KEY, KRKR_SDLK_ESCAPE}, /* cancel/menu   */
               /*  Y      */ {EMIT_KEY, KRKR_SDLK_s},      /* KAG accel 'S' */
               /*  SELECT */ {EMIT_KEY, KRKR_SDLK_f},      /* KAG accel 'F' */
               /*  START  */ {EMIT_KEY, KRKR_SDLK_a},      /* KAG accel 'A' */
                                                           /*  UP     */
               {EMIT_PAD, 11}, /* SDL_CONTROLLER_BUTTON_DPAD_UP    */
                               /*  DOWN   */
               {EMIT_PAD, 12}, /* SDL_CONTROLLER_BUTTON_DPAD_DOWN  */
                               /*  LEFT   */
               {EMIT_PAD, 13}, /* SDL_CONTROLLER_BUTTON_DPAD_LEFT  */
                               /*  RIGHT  */
               {EMIT_PAD, 14}, /* SDL_CONTROLLER_BUTTON_DPAD_RIGHT */
               /*  A      */ {EMIT_KEY, KRKR_SDLK_RETURN}, /* advance       */
               /*  X      */ {EMIT_RIGHTCLICK, 3},         /* hide graphics */
               /*  L      */ {EMIT_WHEEL, 4},              /* scroll up     */
               /*  R      */ {EMIT_WHEEL, 5},              /* scroll down   */
               /*  L2     */ {EMIT_WHEEL, 4},
               /*  R2     */ {EMIT_WHEEL, 5},
               /*  L3     */ {EMIT_NONE, 0},
               /*  R3     */ {EMIT_NONE, 0}, /* pointer click */
         };
         /* Why the D-pad is a CONTROLLER event, not arrow KEY events: KAG games
         * distinguish the arrow *keys* from the D-pad via KiriKiri's virtual-pad
         * VKs (VK_PADUP/PADDOWN/PADLEFT/PADRIGHT). Fata Morgana's MainWindow.tjs
         * createPadKeyMap() maps exactly VK_PADLEFT->VK_LEFT, VK_PADDOWN->VK_DOWN,
         * ... to drive menu focus; it has no branch for bare VK_LEFT/etc. Arrow
         * key events navigate the load/save list (plain KAG arrow handling) but
         * leave the title/menu screen un-navigable. The engine accepts
         * SDL_CONTROLLERBUTTON* and converts it to VK_PAD*
         * (sdl_gamecontrollerbutton_to_vk_key, SDLApplication.cpp:216). The
         * engine is left unopened (KRKRSDL2_LIBRETRO); the shim is the only
         * source of pad events, so there is no double-delivery. */
         static int prev[16];
         for (int i = 0; i < 16; i++)
         {
            int st =
                  krkr_input_state(0, KRKR_RETRO_DEVICE_JOYPAD, 0, (unsigned)i);
            if (PAD[i].mode == EMIT_NONE)
            {
               prev[i] = st;
               continue;
            }
            if (st == prev[i])
               continue;
            char ev[64];
            memset(ev, 0, sizeof(ev));
            switch (PAD[i].mode)
            {
            case EMIT_KEY:
               *(unsigned int *)(ev + 0) =
                     st ? KRKR_SDL_KEYDOWN : KRKR_SDL_KEYUP;
               *(unsigned char *)(ev + 12) = st ? 1 : 0;
               *(int *)(ev + 20)           = PAD[i].code;
               break;
            case EMIT_PAD:
               /* SDL_ControllerButtonEvent: type(4) ts(4) which(8) button(1)
                 * state(1) pad(2). which=0 (the shim has no joystick id; the
                 * engine's handler ignores it). */
               *(unsigned int *)(ev + 0) = st ? KRKR_SDL_CONTROLLERBUTTONDOWN
                                              : KRKR_SDL_CONTROLLERBUTTONUP;
               *(unsigned char *)(ev + 12) =
                     (unsigned char)PAD[i].code;         /* button */
               *(unsigned char *)(ev + 13) = st ? 1 : 0; /* state  */
               break;
            case EMIT_WHEEL:
               *(unsigned int *)(ev + 0) =
                     st ? KRKR_SDL_MOUSEBUTTONDOWN : KRKR_SDL_MOUSEBUTTONUP;
               *(unsigned char *)(ev + 16) =
                     (unsigned char)PAD[i].code; /* 4 = up, 5 = down */
               *(unsigned char *)(ev + 17) = st ? 1 : 0;
               *(unsigned char *)(ev + 18) = 1;
               break;
            case EMIT_RIGHTCLICK:
               *(unsigned int *)(ev + 0) =
                     st ? KRKR_SDL_MOUSEBUTTONDOWN : KRKR_SDL_MOUSEBUTTONUP;
               *(unsigned char *)(ev + 16) = 3; /* SDL button: right */
               *(unsigned char *)(ev + 17) = st ? 1 : 0;
               *(unsigned char *)(ev + 18) = 1;
               if (mx >= 0)
               {
                  *(int *)(ev + 20) = mx;
                  *(int *)(ev + 24) = my;
               }
               break;
            }
            int push_ret = p_SDL_PushEvent(ev);
            prev[i] = st;
            /* Probe the last untested link: the synthetic SDL event is built
             * from a real pad press, so log what was pushed, whether the queue
             * accepted it, and SDL's own reason when it did not. A ret of -1
             * means the event never enters the queue, which is why an engine
             * that polls fine can still never see the key. */
            if (krkr_input_debug())
            {
               {
                  unsigned type = *(unsigned int *)ev;
                  fprintf(stderr,
                          "[in] push id=%d st=%d type=0x%x code=%d ret=%d "
                          "err=%s\n",
                          i, st, type, PAD[i].code, push_ret,
                          push_ret < 0 ? SDL_GetError() : "-");
               }
            }
         }
      }
   }
   /* Pull audio from krkrsdl2's FAudio capture SPSC ring buffer.
 * Writer: the SDL audio thread in FAudio_platform_sdl2.c.
 * Reader: this function, called from retro_run. */
   static float **krkr_audio_buf_pp =
         NULL; /* addr of float* var (lazy alloc) */
   static volatile int *krkr_audio_w_ptr = NULL; /* monotonic frames written */
   static volatile int *krkr_audio_r_ptr = NULL; /* monotonic frames read */
   static int *krkr_audio_cap_ptr        = NULL;
   static int *krkr_audio_freq_ptr       = NULL;
   static int krkr_audio_resolved        = 0;

   /* Sample rate the engine's ring runs at, for the core to size its submit.
 * Resolved lazily alongside the ring; 0 until the engine has published it. */
   extern "C" int krkr_audio_rate(void)
   {
      return (krkr_audio_resolved && krkr_audio_freq_ptr) ? *krkr_audio_freq_ptr
                                                          : 0;
   }

   /* Channel count the engine's ring is interleaved at, and the writer's monotonic
 * frame counter — both for diagnostics. */
   static int *krkr_audio_ch_ptr = NULL;
   extern "C" int krkr_audio_channels(void)
   {
      if (!krkr_audio_resolved)
         return 0;
      if (!krkr_audio_ch_ptr)
         krkr_audio_ch_ptr = (int *)&SDL_libretro_audio_channels;
      return krkr_audio_ch_ptr ? *krkr_audio_ch_ptr : 0;
   }
   extern "C" int krkr_audio_written(void)
   {
      return (krkr_audio_resolved && krkr_audio_w_ptr) ? *krkr_audio_w_ptr : 0;
   }

   extern "C" int krkr_get_audio(int16_t *out_buf, int max_frames)
   {
      if (!krkr_audio_resolved)
      {
         krkr_audio_buf_pp   = &SDL_libretro_audio_buf;
         krkr_audio_w_ptr    = &SDL_libretro_audio_write;
         krkr_audio_r_ptr    = &SDL_libretro_audio_read;
         krkr_audio_cap_ptr  = &SDL_libretro_audio_capacity;
         krkr_audio_freq_ptr = &SDL_libretro_audio_freq;
         krkr_audio_resolved = 1;
      }
      if (!krkr_audio_buf_pp || !krkr_audio_w_ptr || !krkr_audio_r_ptr ||
          !krkr_audio_cap_ptr)
         return 0;
      float *krkr_audio_buf_ptr =
            *krkr_audio_buf_pp; /* allocated after FAudio init */
      if (!krkr_audio_buf_ptr)
         return 0;
      int cap = *krkr_audio_cap_ptr;
      if (cap <= 0)
         return 0;
      int w     = *krkr_audio_w_ptr;
      int r     = *krkr_audio_r_ptr;
      int avail = w - r;
      if (avail <= 0)
         return 0;
      /* If the writer lapped us (engine paused then burst), drop stale frames. */
      if (avail > cap)
      {
         r     = w - cap;
         avail = cap;
      }
      int n = avail > max_frames ? max_frames : avail;
      for (int i = 0; i < n; i++)
      {
         int idx  = (r + i) % cap;
         float l  = krkr_audio_buf_ptr[idx * 2];
         float rr = krkr_audio_buf_ptr[idx * 2 + 1];
         if (l > 1.0f)
            l = 1.0f;
         if (l < -1.0f)
            l = -1.0f;
         if (rr > 1.0f)
            rr = 1.0f;
         if (rr < -1.0f)
            rr = -1.0f;
         out_buf[i * 2]     = (int16_t)(l * 32767.0f);
         out_buf[i * 2 + 1] = (int16_t)(rr * 32767.0f);
      }
      *krkr_audio_r_ptr = r + n;
      return n;
   }
} // extern "C"
