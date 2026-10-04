#include <SDL_libretro.h>
#include <retro_miscellaneous.h>
#include <file/file_path.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
/* backtrace()/backtrace_symbols_fd() are glibc extensions: unavailable on
 * Windows, and macOS spells its equivalent differently. Guard the whole
 * symbolication path so the core builds and runs everywhere; without it the
 * handler still logs the signal and fault address, which is the part that
 * matters most. */
#if defined(__GLIBC__)
#  include <execinfo.h>
#  define ONS_HAVE_BACKTRACE 1
#else
#  define ONS_HAVE_BACKTRACE 0
#endif
/* Vendored fallback font, generated from resources/fonts/ by the Makefile;
 * registered in retro_init for glyphs the game fonts lack. */
#include "fallback_font.h"
#include "extra_fallback_font.h"

/* Crash diagnostics: on SIGSEGV/SIGBUS/SIGABRT dump the fault address and a
 * symbolicated backtrace to the crash log, then re-raise so the default
 * action still kills the process. Core crashes otherwise surface only as
 * RetroArch's "Process exitcode: -11" with no address; this makes them
 * diagnosable (`aarch64-…-addr2line -f -C -e onscripter-en_libretro.so`). */
/* Portable crash-log path. Hardcoding "/tmp/..." is not portable (Windows and
 * some sandboxed frontends have no /tmp); prefer the core's writable save
 * directory, which the frontend provides and which exists on every platform,
 * and keep a fixed relative name as a last-resort fallback. Computed once at
 * load time so the handler itself does no work beyond formatting and writing.
 *
 * Signal-safety caveat: format/write/backtrace_symbols_fd are not
 * async-signal-safe (they may allocate), so this is best-effort; a fault
 * inside the handler loses the dump by design, which is acceptable for a
 * diagnostic path. */
static char ons_crash_log_path[PATH_MAX_LENGTH] = "ons_crash.log";
static void ons_set_crash_log_path(const char *dir)
{
  if (dir && dir[0])
    fill_pathname_join(ons_crash_log_path, dir, "ons_crash.log", sizeof(ons_crash_log_path));
}

#if !defined(_WIN32)
static void ons_crash_handler(int sig, siginfo_t *si, void *ctx)
{
  (void)ctx;
  static volatile int reentry = 0;
  if (reentry) { signal(sig, SIG_DFL); raise(sig); return; }
  reentry = 1;
#else
/* signal() on Windows passes only the signal number. */
static void ons_crash_handler(int sig)
{
  static volatile int reentry = 0;
  if (reentry) { signal(sig, SIG_DFL); raise(sig); return; }
  reentry = 1;
  const void *si_addr = NULL;
#endif

  int fd = open(ons_crash_log_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd >= 0) {
    char buf[256];
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
#if !defined(_WIN32)
    const void *crash_addr = si ? si->si_addr : NULL;
#else
    const void *crash_addr = si_addr;
#endif
    int n = snprintf(buf, sizeof buf,
                     "=== signal %d (%s) addr=%p at %lld.%03lds ===\n",
                     sig,
                     sig == SIGSEGV ? "SIGSEGV" :
#ifdef SIGBUS
                     sig == SIGBUS ? "SIGBUS" :
#endif
                     sig == SIGABRT ? "SIGABRT" : "signal",
                     crash_addr,
                     (long long)ts.tv_sec, ts.tv_nsec / 1000000);
    if (n > 0) write(fd, buf, n);
#if ONS_HAVE_BACKTRACE
    void *bt[64];
    int frames = backtrace(bt, 64);
    backtrace_symbols_fd(bt, frames, fd);
#else
    /* No symbolication on this platform; the address above is still the
     * most useful single fact for a crash report. */
    const char *note = "(no backtrace: platform lacks backtrace())\n";
    write(fd, note, strlen(note));
#endif
    close(fd);
  }
  signal(sig, SIG_DFL);
  raise(sig);
}

#include "libretro_core_options.h"
#include "engine_cabi.h"
#include "engine_bridge.h"

#ifdef _WIN32
#define ons_mkdir(path) mkdir(path)
#else
#define ons_mkdir(path) mkdir(path, 0755)
#endif

// Both engines call exit() on script errors and SDL init failures.
// We catch these via longjmp. errorAndExit() calls the engine-specific
// longjmp function which jumps back to ons_main's setjmp checkpoint.
static void                        fallback_log(enum retro_log_level level, const char *fmt, ...);
static retro_log_printf_t          log_cb = fallback_log;
/* The setjmp/longjmp storage lives in the bridge module (static storage, one
 * copy per process). The ENGINE runs both setjmp and longjmp on its own thread
 * — longjmp must not cross threads — so the core only provides the storage and
 * observes the valid flag. See engine_bridge.c. */

/* ---- Snapshot & state dump mechanism ----
 * Engine scripts call snapshotsave("name") / dumpstate("name") which
 * set a pending request. retro_run() checks these in the frame-sync gap
 * (engine thread blocked in DUMMY_UpdateRects) — no race condition.
 */
#define MAX_REQ_NAME 256
static char pending_snapshot[MAX_REQ_NAME] = {0};
static char pending_dumpstate[MAX_REQ_NAME] = {0};
static const char *test_output_dir = ".";  /* set in retro_load_game */

extern "C" void retro_request_snapshot(const char *name)
{
  if (name) {
    strncpy(pending_snapshot, name, MAX_REQ_NAME - 1);
    pending_snapshot[MAX_REQ_NAME - 1] = '\0';
    log_cb(RETRO_LOG_INFO, "Snapshot requested: %s\n", name);
  }
}

extern "C" void retro_request_dumpstate(const char *name)
{
  if (name) {
    strncpy(pending_dumpstate, name, MAX_REQ_NAME - 1);
    pending_dumpstate[MAX_REQ_NAME - 1] = '\0';
    log_cb(RETRO_LOG_INFO, "State dump requested: %s\n", name);
  }
}

//don't use #include <onscripter/ONScripter.h>, use #include <ONScripterLabel.h> instead (stop including wrong header file)  
#include <ONScripterLabel.h>

/* SDL_libretro_input_state_cb is defined in the bridge module (engine_bridge.c)
 * so the engine modules and the glue all resolve it to one object. */
static retro_input_state_t frontend_input_state_cb;   /* the real frontend callback */
bool SDL_libretro_input_ready = false;

/* Virtual pointer (pad->pointer synthesis) options. Declared before the
 * synthesizer that reads them: enabled by default so pointer-only UIs stay
 * reachable out of the box; the stick follows the usual right-stick
 * convention. */
enum { POINTER_SOURCE_RIGHT = 0, POINTER_SOURCE_LEFT = 1 };
static bool opt_pointer_enable = true;
static int  opt_pointer_source = POINTER_SOURCE_RIGHT;

/* Engine-agnostic RetroPad -> pointer synthesis.
 *
 * RetroArch has no built-in pad-to-pointer mapping (its per-port "Mouse Index"
 * only binds real mouse devices, and its only synthesized pointer source is
 * touch -> vmouse), and some visual-novel UIs — modal Yes/No dialogs in
 * particular — are effectively pointer-only. Rather than patch each engine,
 * synthesize one here, in the core. Both the ONScripter path (via the SDL1.2
 * overlay) and the KiriKiri path (via krkr_set_input_callbacks) call
 * SDL_libretro_input_state_cb, so both get it. The synthesizer reports MOUSE
 * deltas (which both engines already integrate) and a MOUSE_LEFT press for the
 * stick click, so no engine-side change is needed.
 *
 * Whether the pointer is offered, and which stick drives it, are core options
 * (onscripter_en_pointer_enable / _pointer_source): the core cannot know the
 * user's pad, so refusing to guess is the point. */
static int vlx_pos = -1, vly_pos = -1;   /* virtual pointer accumulator, -1 = unset */

/* Synthesize pointer/mouse reads from the stick chosen by opt_pointer_source.
 * Returns the value for (device,id), or defers to the frontend. Clicking uses
 * the matching stick button (L3 for the left stick, R3 for the right), so the
 * "click" input follows the stick the user picked. */
static int16_t pointer_synth_input_state(unsigned port, unsigned device,
                                          unsigned index, unsigned id)
{
  if (!opt_pointer_enable || port != 0 ||
      (device != RETRO_DEVICE_MOUSE && device != RETRO_DEVICE_POINTER))
    return frontend_input_state_cb(port, device, index, id);

  bool left_stick = (opt_pointer_source == POINTER_SOURCE_LEFT);
  unsigned analog_index = left_stick ? RETRO_DEVICE_INDEX_ANALOG_LEFT
                                     : RETRO_DEVICE_INDEX_ANALOG_RIGHT;
  unsigned stick_btn    = left_stick ? RETRO_DEVICE_ID_JOYPAD_L3
                                     : RETRO_DEVICE_ID_JOYPAD_R3;

  int16_t sx = frontend_input_state_cb(0, RETRO_DEVICE_ANALOG, analog_index,
                     RETRO_DEVICE_ID_ANALOG_X);
  int16_t sy = frontend_input_state_cb(0, RETRO_DEVICE_ANALOG, analog_index,
                     RETRO_DEVICE_ID_ANALOG_Y);
  int16_t click = frontend_input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, stick_btn);
  int moved = 0;
  if (sx > 4000 || sx < -4000) { vlx_pos = (vlx_pos < 0 ? 0 : vlx_pos) + sx / 4096; moved = 1; }
  if (sy > 4000 || sy < -4000) { vly_pos = (vly_pos < 0 ? 0 : vly_pos) + sy / 4096; moved = 1; }

  if (device == RETRO_DEVICE_MOUSE)
  {
    if (id == RETRO_DEVICE_ID_MOUSE_X) {
      int16_t r = frontend_input_state_cb(port, device, index, id);
      return r ? r : (moved ? (int16_t)(sx / 4096) : 0);
    }
    if (id == RETRO_DEVICE_ID_MOUSE_Y) {
      int16_t r = frontend_input_state_cb(port, device, index, id);
      return r ? r : (moved ? (int16_t)(sy / 4096) : 0);
    }
    if (id == RETRO_DEVICE_ID_MOUSE_LEFT)
      return frontend_input_state_cb(port, device, index, id) || click;
  }
  /* A frontend with no pointer device reports 0,0 for the absolute pointer,
   * which the engines would read as the screen centre; treat that as "no
   * pointer input". */
  if (device == RETRO_DEVICE_POINTER &&
      (id == RETRO_DEVICE_ID_POINTER_X || id == RETRO_DEVICE_ID_POINTER_Y) &&
      frontend_input_state_cb(0, RETRO_DEVICE_POINTER, 0,
                              RETRO_DEVICE_ID_POINTER_COUNT) == 0)
    return 0;

  return frontend_input_state_cb(port, device, index, id);
}

/* The one callback installed into every input path. Keeps the synthesizer
 * optional and clearly separated from plain forwarding: this dispatcher owns
 * "how the core answers an input read", and pointer_synth_input_state owns the
 * one synthesized source. Future sources (e.g. a keyboard-to-pad shim) belong
 * next to it, composed here, not by growing the synthesizer. */
static int16_t core_input_state_cb(unsigned port, unsigned device,
                                   unsigned index, unsigned id)
{
  return pointer_synth_input_state(port, device, index, id);
}
/* SDL_libretro_audio_spec is defined in the bridge module: the engine's SDL
 * publishes it, the core reads it. */
SDL_sem             *SDL_libretro_event_sem  = NULL;
/* Frame sync: when enabled, DUMMY_UpdateRects blocks on this semaphore
   after each screen flush. retro_run() posts it once per frame, giving
   1:1 frame synchronization — every flush() is displayed.
   Controlled by -DLIBRETRO_FRAME_SYNC at build time. */
#ifdef LIBRETRO_FRAME_SYNC
extern SDL_sem *SDL_libretro_wait_sem;
static SDL_sem *fs_sem = NULL;
#endif

/* Core options (set via retro_set_environment, read in retro_run) */
static bool opt_frame_sync = true;
static bool opt_indicators = true;
static int  opt_debug_level = 0;
static bool opt_software_cursor = true;
static int  opt_text_speed = -1;
/* Fast-forward rate for the single game clock (1.0 = off, N = "Nx"). */
static double opt_ff_scale = 1.0;
/* Rate currently installed in the clock; cached so retro_run only calls
 * ons_libretro_clock_set() when it actually changes. */
static double clock_scale_applied = -1.0;
/* opt_mouse_sensitivity and remap_keys are defined in the bridge module; the
 * core writes them from frontend options, the glue reads them. */
/* Key remap state */
#include "key_remap.h"

static retro_video_refresh_t       video_cb;
static retro_input_poll_t          input_poll_cb;
static retro_audio_sample_batch_t  audio_batch_cb;
static retro_environment_t         environ_cb;
/* environ_cb_global is defined in the bridge module so the SDL events glue
 * reads core options without a separate plumbing path. The core sets it in
 * retro_set_environment. */
static EngineHandle engine = nullptr;
static const EngineVTable *vtable = nullptr;
static volatile bool engine_running = false;
/* Wrapper called from retro_run to dump engine state to a FILE* */
static void engine_dump_state(EngineHandle h, FILE *f)
{
  if (vtable && vtable->dumpState)
    vtable->dumpState(h, f);
}
static SDL_Thread                 *ons_thread;
static uint32_t                    delta;
static char last_archive_path[PATH_MAX_LENGTH];
static char last_save_path[PATH_MAX_LENGTH];

/* Dedicated audio diagnostic log. The launcher pipes the core's stderr into a
 * buffer that is only flushed to es_launch_stderr.log when the game *exits*,
 * so live diagnostics are invisible during a session. Write to a fixed file
 * instead (tmpfs -> always writable) so tests can read it live:
 * /tmp/ons_audio.log */
static FILE *audio_log = NULL;
static void audio_logf(const char *fmt, ...)
{
  if (!audio_log) {
    audio_log = fopen("/tmp/ons_audio.log", "w");
    if (!audio_log) return;
  }
  va_list ap;
  va_start(ap, fmt);
  vfprintf(audio_log, fmt, ap);
  va_end(ap);
  fflush(audio_log);   /* live-readable */
}

/* Recursive mkdir (all components). Needed because per-game save dirs are
 * nested: <savedir>/<game>/ and ons_mkdir() is a plain single-level mkdir. */
static void mkdir_p(const char *path)
{
  char tmp[PATH_MAX_LENGTH];
  size_t len = strlen(path);
  if (len == 0 || len >= sizeof(tmp)) return;
  memcpy(tmp, path, len + 1);
  for (char *p = tmp + 1; *p; p++) {
    if (*p == '/') { *p = '\0'; ons_mkdir(tmp); *p = '/'; }
  }
  ons_mkdir(tmp);
}


static void show_message(const char *msg)
{
  struct retro_message message = { .msg = msg, .frames = 120 };
  environ_cb(RETRO_ENVIRONMENT_SET_MESSAGE, &message);
}

static void fallback_log(enum retro_log_level level, const char *fmt, ...)
{
  (void)level;
  va_list va;
  va_start(va, fmt);
  vfprintf(stderr, fmt, va);
  va_end(va);
}

unsigned retro_api_version(void)
{
  return RETRO_API_VERSION;
}

/* RetroPad -> semantic action. This is the contract the user sees in
 * RetroArch's Controls screen and it must read the same for every engine; the
 * per-engine key/mouse translation lives in sdl-libretro/events.c (ONScripter)
 * and engine_kirikiri_cabi.cpp (KiriKiri). See CONTROLS.md. Users remap at the
 * RetroArch Controls level, which maps physical buttons to these RetroPad ids;
 * the core always reads the ids the same way. */
static struct retro_input_descriptor input_descs[] = {
  { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,     "Focus Previous" },
  { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,   "Focus Next" },
  { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,   "Focus Previous" },
  { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT,  "Focus Next" },
  { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,      "Advance / Confirm" },
  { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,      "Cancel / Back" },
  { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,      "Hide Text Window" },
  { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,      "Save" },
  { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Skip (toggle)" },
  { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,  "Auto Mode (toggle)" },
  { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,      "History Back" },
  { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,      "History Forward" },
  { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,     "History Back" },
  { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,     "History Forward" },
  { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3,     "Pointer Click (Left)" },
  { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3,     "Pointer Click (Right)" },
  { 0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X,      "Mouse X" },
  { 0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y,      "Mouse Y" },
  { 0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT,   "Click" },
  { 0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_RIGHT,  "Menu" },
  { 0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_X,   "Touch X" },
  { 0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_Y,   "Touch Y" },
  { 0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_PRESSED, "Touch" },
  { 0, 0, 0, 0, NULL },
};

static bool kiri_input_wired = false;  /* reset clears this so a new engine re-wires */
static void krkr_keyboard_cb(bool down, unsigned keycode, uint32_t, uint16_t);

void retro_set_environment(retro_environment_t cb)
{
  static struct retro_log_callback log;
  environ_cb = cb;
  environ_cb_global = cb;
  /* libretro_script_error_longjmp() and the setjmp storage it uses are both in
   * the bridge module now; nothing to register here. */
  if (environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log))
    log_cb = log.log;
  environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, input_descs);

  // Real keyboard events (Enter/Space/arrow keys) must reach the engine:
  // RetroArch's default retropad map does not carry Space.
  static struct retro_keyboard_callback kb_cb = { krkr_keyboard_cb };
  environ_cb(RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK, &kb_cb);

  // Core options: frame sync, indicators, debug level
  libretro_set_core_options(environ_cb, &opt_frame_sync, &opt_indicators, &opt_debug_level);

  // Callback to let RetroArch know we support updating options at runtime
  struct retro_core_options_update_display_callback update_cb = {
    .callback = []() -> bool {
      // Options are re-read via retro_variable at each retro_run
      return true;
    }
  };
  environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK, &update_cb);

  // Shoulder buttons as mouse clicks, matching the KiriKiri shim and the
  // RetroPad convention: L = left click (the primary act: confirm/advance),
  // R = right click (cancel/back, and ONScripter's right-click menu). This was
  // previously inverted (L emitted the right-click / rmenu).
  SDL_libretro_leftclick_button  = RETRO_DEVICE_ID_JOYPAD_L;
  SDL_libretro_rightclick_button = RETRO_DEVICE_ID_JOYPAD_R;
  // L button no longer sends SDLK_o (one-page mode)
  SDL_libretro_pad_mapping[RETRO_DEVICE_ID_JOYPAD_L] = ONS_KEY_NONE;
}

void retro_set_video_refresh(retro_video_refresh_t cb)
{
  video_cb = cb;
}

void retro_set_audio_sample(retro_audio_sample_t cb)
{
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
  audio_batch_cb = cb;
}

void retro_set_input_poll(retro_input_poll_t cb)
{
  input_poll_cb = cb;
}

void retro_set_input_state(retro_input_state_t cb)
{
  /* Capture the real frontend callback and route all engine input reads
   * through the pad->pointer synthesizer (installed in retro_load_game). */
  frontend_input_state_cb = cb;
}

/* Deployment-identification stamp. A fixed-size NUL-terminated string that the
 * build bakes in (see BUILD_VERSION in the Makefile), so `strings` on the
 * deployed .so identifies exactly which build is running — no md5 comparison,
 * no guessing. Reported to the frontend and greppable from the binary. */
#ifndef BUILD_VERSION
#  define BUILD_VERSION "dev"
#endif
/* A git abbreviation is not a fixed width (it grows when ambiguous) and
 * "-dirty" adds six more, so the literal could outgrow the buffer. The Makefile
 * truncates to fit; this fails the build loudly rather than overflow if some
 * other caller sets BUILD_VERSION too long. */
static_assert(sizeof(BUILD_VERSION) <= 16,
              "BUILD_VERSION must fit ONSCRIPTER_EN_LIBRETRO_VERSION[16]");
static const char ONSCRIPTER_EN_LIBRETRO_VERSION[16] = BUILD_VERSION;

void retro_get_system_info(struct retro_system_info *info)
{
  info->need_fullpath = true;
  info->valid_extensions = "txt|dat|___|ons";
  info->library_version = ONSCRIPTER_EN_LIBRETRO_VERSION;
  info->library_name = "onscripter";
  info->block_extract = false;
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
  // Always report 640x480 — the engine thread hasn't initialized yet when
  // this is called, so getWidth/getHeight return garbage. The actual
  // framebuffer is passed via video_cb each frame with correct dimensions.
  int width = 640;
  int height = 480;
  {
    const char *tw = getenv("ONS_TEST_WIDTH");
    const char *th = getenv("ONS_TEST_HEIGHT");
    if (tw) width = atoi(tw);
    if (th) height = atoi(th);
  }
  info->geometry.base_width = width;
  info->geometry.base_height = height;
  info->geometry.max_width = width;
  info->geometry.max_height = height;
  info->geometry.aspect_ratio = 4.0 / 3.0;
  info->timing.fps = 60.0;
  /* Guess only: the engine isn't created yet (retro_get_system_av_info runs
   * before retro_load_game). ONS/PONScripter default to 44100; the KiriKiri
   * path re-emits 48000 via SET_SYSTEM_AV_INFO once its surface is up, and the
   * ONS/PONS path re-emits the real spec->freq in retro_run. So this value is
   * only in effect for the first frame or two. */
  info->timing.sample_rate = 44100.0;
  fprintf(stderr, "ons_audio: get_system_av_info: initial sample_rate=44100 (engine not up yet)\n");
  audio_logf("get_system_av_info: initial sample_rate=44100 (engine not up yet)\n");
}

static void frame_cb(retro_usec_t usec)
{
  delta = usec / 1000;
}


void retro_init(void)
{
  { /* Install crash diagnostics so a core fault leaves a backtrace instead of
     * only RetroArch's bare "Process exitcode: -11". Resolve the portable log
     * location first: unlike retro_init, retro_load_game has the save
     * directory, and it runs after this per load — but RETRO_ENVIRONMENT_GET_
     * SAVE_DIRECTORY is valid as soon as the environment is set, so ask here
     * and let retro_load_game refresh it if the directory changes. */
    const char *save_dir = NULL;
    if (environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &save_dir))
      ons_set_crash_log_path(save_dir);
#if !defined(_WIN32)
    /* sigaction/SA_SIGINFO/SIGBUS are POSIX; Windows has no equivalent
     * (SetUnhandledExceptionFilter is a different mechanism, and the address
     * it gives is already reported by the frontend). Skip the install there. */
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = ons_crash_handler;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
#else
    signal(SIGABRT, ons_crash_handler);
    signal(SIGSEGV, ons_crash_handler);
#endif
  }
  enum retro_pixel_format pixfmt = RETRO_PIXEL_FORMAT_XRGB8888;
  struct retro_frame_time_callback frametime = {
    .callback = frame_cb,
    .reference = 1000000 / 60,
  };
  environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &pixfmt);
  environ_cb(RETRO_ENVIRONMENT_SET_FRAME_TIME_CALLBACK, &frametime);

  /* The game fonts used by ONScripter have no U+2026, and the bundled
   * stb_truetype glyph layer has no fontconfig fallback, so a substituted
   * ellipsis rendered as .notdef. Register the vendored fallback fonts
   * (resources/fonts/, embedded by the Makefile) for any codepoint the game
   * font lacks.
   *
   * Two fonts, because neither covers everything these games use: Mona is a
   * CJK / MS PGothic clone (excellent CJK, no Latin-1 accents), FiraSans is a
   * Latin face (no CJK). Mona is primary so Japanese and ellipsis resolve; the
   * Latin font is the secondary, used for accented Latin a game font lacks. */
  {
    extern int TTF_SetFallbackFontMem(const unsigned char *data, int size);
    extern int TTF_AddFallbackFontMem(const unsigned char *data, int size);
    TTF_SetFallbackFontMem(fallback_font_ttf, fallback_font_ttf_len);
    TTF_AddFallbackFontMem(extra_fallback_font_ttf, extra_fallback_font_ttf_len);
  }
}

static int ons_main(void *data)
{
  if (setjmp(ons_script_error_jmp) != 0) {
    // ONScripter called errorAndExit() - caught by longjmp
    log_cb(RETRO_LOG_ERROR, "ONScripter script error - returning cleanly\n");
    ons_script_error_jmp_valid = false;
    engine_running = false;
    return 0;
  }
  ons_script_error_jmp_valid = true;
  engine_running = true;
  if (vtable->init(engine)) {
    return -1;
  }
  { extern int SDL_timer_started, SDL_timer_running;
    log_cb(RETRO_LOG_INFO, "Timer state: started=%d running=%d\n",
           SDL_timer_started, SDL_timer_running); }
  SDL_ShowCursor(SDL_DISABLE);
  vtable->eventLoop(engine);
  engine_running = false;
  ons_script_error_jmp_valid = false;
  return 0;
}
// KiriKiri engine C functions (from engine_kirikiri_cabi.cpp)
extern "C" {
  typedef void* EngineHandle;
  EngineHandle krkr_create(void);
  void krkr_setArchivePath(EngineHandle, const char*);
  void krkr_setSavePath(EngineHandle, const char*);
  int krkr_init(EngineHandle);
  void **krkr_get_surface_ptr(void);
  int *krkr_get_surface_updated_ptr(void);
  volatile int *krkr_get_auto_mode_ptr(void);
  volatile int *krkr_get_engine_stopped_ptr(void);
  void krkr_get_mouse_pos(int *x, int *y);
  void krkr_pump_events(void);
  void krkr_set_input_callbacks(retro_input_state_t state_cb, retro_input_poll_t poll_cb);
  void krkr_set_fastforward(int f);
  void krkr_notify_poll(long long ms);
  void krkr_set_resolution(int w, int h);
  void krkr_request_stop(void);
  int  krkr_finish_stop(void);
  void krkr_start(void);
  void krkr_push_key(int sym, int down);
  int krkr_get_audio(int16_t *out_buf, int max_frames);
  int krkr_audio_rate(void);
  void krkr_shutdown(void);
}

/* RetroArch keyboard callback -> engine key queue (KiriKiri). Also feeds the
 * SDL2 compat path used by ONScripter engines.
 *
 * The key-remap core options are semantic: the user picks which keyboard key
 * triggers Advance / Menu / Lookback etc. ONScripter applies that mapping in
 * sdl-libretro/events.c; KiriKiri gets its keys here, so the same options are
 * translated to KAG's accelerators (see kk_engine_keys) before queueing.
 * Without this the options did nothing for KiriKiri games. */
static void krkr_keyboard_cb(bool down, unsigned keycode, uint32_t, uint16_t)
{
  /* Translate a remapped function key to what KAG expects. Keys here are
   * ASCII/control codes, where RETROK_* and SDLK_* coincide. */
  if (vtable == NULL) {
    for (int r = 0; r < REMAP_COUNT; r++) {
      if ((int)keycode == remap_keys[r] && remap_keys[r] != kk_engine_keys[r]) {
        keycode = (unsigned)kk_engine_keys[r];
        break;
      }
    }
  }
  /* RetroArch's keyboard callback may deliver only the release (up) without a
   * matching press. KAG's scenario drivers advance in onKeyDown, so synthesize
   * the missing keydown immediately before an unmatched keyup. */
  static unsigned char seen[512];
  if (keycode < 512 && down)
    seen[keycode] = 1;
  if (!down && keycode < 512 && !seen[keycode])
    krkr_push_key((int)keycode, 1);
  if (keycode < 512)
    seen[keycode] = 0;
  krkr_push_key((int)keycode, down ? 1 : 0);
}

bool retro_load_game(const struct retro_game_info *game){
  if (!game)
    return false;

  /* Install the composed dispatcher for every input read by either engine
   * (ONScripter drives it through the SDL1.2 overlay; KiriKiri through
   * krkr_set_input_callbacks). Installed here because the pointer synthesis it
   * owns needs the game's surface size to clamp the virtual pointer. */
  SDL_libretro_input_state_cb = core_input_state_cb;

  char archive_path[PATH_MAX_LENGTH];
  fill_pathname_basedir(archive_path, game->path, sizeof(archive_path));
  // Get save directory early (needed for KiriKiri path)
  const char *save_dir = NULL;
  environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &save_dir);
  /* Follow the save directory the frontend actually gave us for this load. */
  ons_set_crash_log_path(save_dir);

  /* Per-game save isolation. RetroArch/ES give us a *per-system* directory
   * (savefile_directory = /recalbox/share/saves/vnovel), which is shared by
   * every game in that system. Without a per-game subdir, Tsukihime and Le
   * sanglot des cigales would overwrite each other's save1.dat/gloval.sav.
   * Derive a stable name from the game folder (the parent dir of the ROM)
   * and nest under it. */
  char game_save_dir[PATH_MAX_LENGTH];
  {
    /* archive_path is like ".../vnovel/Tsukihime/" -> take the last component */
    char trimmed[PATH_MAX_LENGTH];
    strncpy(trimmed, archive_path, sizeof(trimmed) - 1);
    trimmed[sizeof(trimmed) - 1] = '\0';
    size_t tl = strlen(trimmed);
    while (tl > 0 && trimmed[tl - 1] == '/') trimmed[--tl] = '\0';
    const char *base = strrchr(trimmed, '/');
    base = base ? base + 1 : trimmed;
    const char *root = (save_dir && save_dir[0]) ? save_dir : archive_path;
    fill_pathname_join(game_save_dir, root, base, sizeof(game_save_dir));
  }

  // Internal rendering resolution. "native" uses the host display size;
  // otherwise tell the engine the frontend expects a fixed resolution, so it
  // sizes itself (and its window/layer) from that. Read once here so both
  // engines see the same value: ONS via vtable->setResolution below, KiriKiri
  // via krkr_set_resolution in its branch.
  int forced_res_w = 0, forced_res_h = 0;
  {
    struct retro_variable resvar = {0};
    resvar.key = "onscripter_en_internal_resolution";
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &resvar) && resvar.value &&
        strcmp(resvar.value, "native") != 0) {
      unsigned a = 0, b = 0;
      if (sscanf(resvar.value, "%ux%u", &a, &b) == 2) { forced_res_w = (int)a; forced_res_h = (int)b; }
    }
  }

  // Detect engine: KiriKiri for .xp3, PONScripter for pscript.dat, ONScripter-EN otherwise
  char xp3_check[PATH_MAX_LENGTH];
  fill_pathname_join(xp3_check, archive_path, "data.xp3", sizeof(xp3_check));
  FILE *xp3_fp = fopen(xp3_check, "rb");
  if (xp3_fp) {
    fclose(xp3_fp);
    engine = krkr_create();
    log_cb(RETRO_LOG_INFO, "Using KiriKiri engine (data.xp3)\n");
    audio_logf("ENGINE=KiriKiri (data.xp3) save=%s\n", game_save_dir);
    krkr_setArchivePath(engine, archive_path);
    mkdir_p(game_save_dir);
    krkr_setSavePath(engine, game_save_dir);
    strncpy(last_save_path, game_save_dir, sizeof(last_save_path));
    /* Tell the engine the frontend's expected resolution so KiriKiri sizes
     * itself (System.screenWidth/screenHeight) from it. The request is stashed
     * by the cabi and applied when krkr_init resolves the engine's setter, so
     * the engine sees it before it builds its main window. 0,0 is a no-op: the
     * host display size applies. The engine's SDL surface is not forced; the
     * core scales the finished frame. */
    if (forced_res_w > 0 && forced_res_h > 0) {
      krkr_set_resolution(forced_res_w, forced_res_h);
      log_cb(RETRO_LOG_INFO, "Internal resolution: forced %dx%d\n", forced_res_w, forced_res_h);
    } else {
      krkr_set_resolution(0, 0);
      log_cb(RETRO_LOG_INFO, "Internal resolution: native\n");
    }
    krkr_init(engine);
    strncpy(last_archive_path, archive_path, sizeof(last_archive_path));
    return true;
  }
  // extern C declarations are at file scope above retro_load_game
  // Check for PONScripter
  char pscript_check[PATH_MAX_LENGTH];
  fill_pathname_join(pscript_check, archive_path, "pscript.dat", sizeof(pscript_check));
  FILE *pscript_fp = fopen(pscript_check, "rb");
  if (pscript_fp) {
    fclose(pscript_fp);
    vtable = &pons_engine_vtable; engine = vtable->create();
    log_cb(RETRO_LOG_INFO, "Using PONScripter engine (pscript.dat)\n");
    audio_logf("ENGINE=PONScripter (pscript.dat) save=%s\n", game_save_dir);
  } else {
    vtable = &ons_engine_vtable; engine = vtable->create();
    log_cb(RETRO_LOG_INFO, "Using ONScripter-EN engine (nscript.dat)\n");
    audio_logf("ENGINE=ONScripter-EN (nscript.dat) save=%s\n", game_save_dir);
  }
  vtable->setArchivePath(engine, archive_path);
  /* Remember the last archive that was run, so later comparisons (a restart
   * vs. a genuine content change) have something to compare against:
   * retro_reset() recreates the engine and re-applies last_archive_path.
   * (The KiriKiri branch above sets it too, but returns early.) */
  strncpy(last_archive_path, archive_path, sizeof(last_archive_path) - 1);
  last_archive_path[sizeof(last_archive_path) - 1] = '\0';

  // Per-game save directory (see above). Fall back to the game dir if
  // RetroArch gave us no save directory at all.
  if (save_dir && save_dir[0]) {
    mkdir_p(game_save_dir);
    vtable->setSavePath(engine, game_save_dir);
    strncpy(last_save_path, game_save_dir, sizeof(last_save_path));
    test_output_dir = strdup(game_save_dir);
  } else {
    vtable->setSavePath(engine, archive_path);
    strncpy(last_save_path, archive_path, sizeof(last_save_path));
    test_output_dir = strdup(archive_path);
  }

  // Auto-detect encryption key from .exe in game directory.
  // Only needed for encrypted scripts (nscript.___, mode 3).
  // nscript.dat (mode 1) uses simple XOR and doesn't need a key table.
  char enc_check[PATH_MAX_LENGTH];
  fill_pathname_join(enc_check, archive_path, "nscript.___", sizeof(enc_check));
  FILE *enc_fp = fopen(enc_check, "rb");
  if (enc_fp) {
    fclose(enc_fp); // nscript.___ exists — need key table
    const char* exe_names[] = {
      "nscr.exe", "Tsukihime.exe", "onscripter.exe",
      "ひぐらしのなく頃に解.exe",  // Higurashi Kai
      "game.exe",  // generic fallback (renamed from Japanese)
      NULL
    };
    for (int i = 0; exe_names[i]; i++) {
      char test_path[PATH_MAX_LENGTH];
      fill_pathname_join(test_path, archive_path, exe_names[i], sizeof(test_path));
      FILE* fp = fopen(test_path, "rb");
      if (fp) {
        fclose(fp);
        vtable->setKeyEXE(engine, test_path);
        log_cb(RETRO_LOG_INFO, "Using key from: %s\n", test_path);
        break;
      }
    }
  }

  SDL_libretro_event_sem = SDL_CreateSemaphore(0);
#ifdef LIBRETRO_FRAME_SYNC
  fs_sem = SDL_CreateSemaphore(0);
  SDL_libretro_wait_sem = NULL; // disabled during init
#endif

  // Read debug level option before starting engine
  struct retro_variable dbgvar = {0};
  dbgvar.key = "onscripter_en_debug_level";
  if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &dbgvar) && dbgvar.value)
    opt_debug_level = atoi(dbgvar.value);
  vtable->setDebugLevel(engine, opt_debug_level);
  log_cb(RETRO_LOG_INFO, "Debug level: %d\n", opt_debug_level);

  // Internal rendering resolution for the ONS/PONS engines (parsed above).
  if (vtable->setResolution) {
    if (forced_res_w > 0 && forced_res_h > 0) {
      vtable->setResolution(engine, forced_res_w, forced_res_h);
      log_cb(RETRO_LOG_INFO, "Internal resolution: forced %dx%d\n", forced_res_w, forced_res_h);
    } else {
      log_cb(RETRO_LOG_INFO, "Internal resolution: native\n");
    }
  }

  ons_thread = SDL_CreateThread(ons_main, NULL);

  return true;
}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
}

/* Stop the engine thread cooperatively.
 *
 * The ONS/PONS eventLoop blocks in SDL_WaitEvent() and leaves on SDL_QUIT
 * (both engines call endCommand() on SDL_QUIT). SDL_KillThread() would instead
 * leave that thread mid-wait with SDL's event/mixer state inconsistent, which
 * makes the *frontend* spin on shutdown. So: post an SDL_QUIT event (waking
 * SDL_WaitEvent), then join with a timeout. */
static void stop_engine_thread(void)
{
  if (!ons_thread) return;
  if (vtable) {
    /* Unblock any engine-side waits, then wake the event loop. */
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = SDL_QUIT;
    SDL_PushEvent(&ev);
  }
  /* Wait up to ~2s for a clean return. SDL 1.2 has no timed join, so poll. */
  for (int i = 0; i < 200; i++) {
    if (!engine_running) break;
    SDL_Delay(10);
  }
  if (!engine_running) {
    SDL_WaitThread(ons_thread, NULL);
  } else {
    /* Last resort: the engine ignored SDL_QUIT (e.g. stuck in a script). */
    SDL_KillThread(ons_thread);
  }
  ons_thread = nullptr;
}

void retro_deinit(void)
{
}

void retro_reset(void)
{
  /* First: unpause. A reset must reach the engine even if the frontend paused
   * us; otherwise the engine thread sits in SDL_Delay's pause gate and never
   * returns to its event loop to acknowledge the reset (deadlock). in_reset
   * additionally suspends the gate for the duration, because the frontend stops
   * calling retro_run() during the reset so the heartbeat goes stale. */
  SDL_libretro_paused = false;
  SDL_libretro_in_reset = true;
  SDL_libretro_frontend_hb = 0;   /* re-armed by the next retro_run */
  ons_libretro_clock_reset();     /* drop any in-flight FF epoch */

  /* KiriKiri runs with vtable == NULL and its engine thread lives in the cabi.
   * Reset is a two-part, engine-owned handshake: request that the engine return
   * to initial conditions, then start it again. Both parts are driven here, in
   * order, with a bounded wait for the engine to acknowledge the stop — the
   * core never tears engine state down itself and never pokes engine flags. */
  if (!vtable) {
    krkr_request_stop();
    krkr_finish_stop();          /* joins + tears down once the engine unwinds */
    kiri_input_wired = false;    /* new engine: callbacks must be re-installed */
    krkr_start();                /* re-arms + re-creates the engine thread */
    SDL_libretro_in_reset = false;
    return;
  }

  stop_engine_thread();
#ifdef LIBRETRO_FRAME_SYNC
  SDL_libretro_wait_sem = NULL;
#endif
  if (engine && vtable) {
    vtable->destroy(engine);
    engine = nullptr;
  }
  if (vtable) {
    engine = vtable->create();
    if (engine) {
      vtable->setArchivePath(engine, last_archive_path);
      vtable->setSavePath(engine, last_save_path);
      vtable->setDebugLevel(engine, opt_debug_level);
      SDL_PauseAudio(0); /* Resume audio after engine restart */
      ons_thread = SDL_CreateThread(ons_main, NULL);
    }
  }
  SDL_libretro_in_reset = false;
}

extern "C" void SDL_ThreadedTimerCheck(void);

/* Read the Fast-Forward Speed option and install it as the game clock's rate.
 * Both the KiriKiri and ONS paths need this: the KiriKiri branch returns before
 * the per-frame option block that owns the parse, so without an explicit call
 * there `opt_ff_scale` stays 1.0 and FF never dilates KiriKiri's timed work. */
static void read_ff_speed_option(void)
{
  struct retro_variable var = {0};
  var.key = "onscripter_en_fastforward_speed";
  if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
    int mult = 0;
    if (strcmp(var.value, "2x") == 0)       mult = 2;
    else if (strcmp(var.value, "4x") == 0)  mult = 4;
    else if (strcmp(var.value, "8x") == 0)  mult = 8;
    else if (strcmp(var.value, "16x") == 0) mult = 16;
    else if (strcmp(var.value, "32x") == 0) mult = 32;
    else if (strcmp(var.value, "max") == 0) mult = 64;
    /* The multiplier is the FF RATE for the single game clock: the clock
     * dilates by `mult` while FF is on, so every timed engine event (ONS/PON
     * timers via SDL_GetTicks, KiriKiri timers via the installed clock)
     * accelerates by the same factor. */
    opt_ff_scale = mult > 0 ? (double)mult : 1.0;
  }
}

/* Per-frame option re-read (Quick Menu hot-reload) plus the one-time
 * frame-sync setup. Split out of retro_run purely for readability. */
static void read_frame_options(void)
{
#ifdef LIBRETRO_FRAME_SYNC
  static bool frame_sync_activated = false;
  if (!frame_sync_activated) {
    if (engine && vtable->drainUserEvents) vtable->drainUserEvents(engine);
    frame_sync_activated = true;
    { char env_path[PATH_MAX_LENGTH]; fill_pathname_join(env_path, last_save_path, "envdata", sizeof(env_path));
      FILE *ef = fopen(env_path, "rb");
      if (ef) { fseek(ef, 0, SEEK_END); long esz = ftell(ef); fseek(ef, 0, SEEK_SET);
        unsigned char buf[256]; size_t nr = fread(buf, 1, esz < 256 ? esz : 256, ef);
        for (size_t i = 0; i < nr; i += 16) {
          char hex[128] = ""; int p = 0;
          for (size_t j = i; j < i+16 && j < nr; j++) p += snprintf(hex+p, sizeof(hex)-p, "%02x ", buf[j]);
          log_cb(RETRO_LOG_INFO, "  %04zx: %s\n", i, hex);
        }
        fclose(ef);
      } else log_cb(RETRO_LOG_INFO, "envdata: not found (using defaults)\n");
    }
  }
  // Re-read core options each frame (allows hot-reload from Quick Menu)
  struct retro_variable var = {0};

  var.key = "onscripter_en_frame_sync";
  if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
    bool new_sync = (strcmp(var.value, "enabled") == 0);
    if (new_sync != opt_frame_sync) {
      opt_frame_sync = new_sync;
      if (opt_frame_sync && !SDL_libretro_wait_sem)
        SDL_libretro_wait_sem = NULL; /* disabled — causes deadlock */
      else if (!opt_frame_sync)
        SDL_libretro_wait_sem = NULL;
    }
  }

  var.key = "onscripter_en_mode_indicators";
  if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    opt_indicators = (strcmp(var.value, "enabled") == 0);

  var.key = "onscripter_en_debug_level";
  if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
    int new_debug = atoi(var.value);
    if (new_debug != opt_debug_level) {
      opt_debug_level = new_debug;
      if (engine) vtable->setDebugLevel(engine, opt_debug_level);
    }
  }

  var.key = "onscripter_en_software_cursor";
  if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    opt_software_cursor = (strcmp(var.value, "enabled") == 0);

  var.key = "onscripter_en_text_speed";
  if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
    int new_speed = -1;
    if (strcmp(var.value, "slow") == 0)        new_speed = 0;
    else if (strcmp(var.value, "normal") == 0) new_speed = 1;
    else if (strcmp(var.value, "fast") == 0)   new_speed = 2;
    else if (strcmp(var.value, "instant") == 0) new_speed = 3;
    if (new_speed != opt_text_speed) {
      opt_text_speed = new_speed;
      if (engine && opt_text_speed >= 0 && opt_text_speed <= 2)
        vtable->setTextSpeed(engine, opt_text_speed);
    }
  }

  read_ff_speed_option();   /* shared with the KiriKiri branch (line ~917) */

  var.key = "onscripter_en_mouse_sensitivity";
  if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    opt_mouse_sensitivity = atof(var.value);
#endif
  /* Read key remap options */
  #define PARSE_KEY(optname, idx) do { \
    var.key = optname; \
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) { \
      if (strcmp(var.value, "[") == 0)            remap_keys[idx] = SDLK_LEFTBRACKET; \
      else if (strcmp(var.value, "]") == 0)       remap_keys[idx] = SDLK_RIGHTBRACKET; \
      else if (strcmp(var.value, "h") == 0)       remap_keys[idx] = SDLK_h; \
      else if (strcmp(var.value, "q") == 0)       remap_keys[idx] = SDLK_q; \
      else if (strcmp(var.value, "b") == 0)       remap_keys[idx] = SDLK_b; \
      else if (strcmp(var.value, "backspace") == 0) remap_keys[idx] = SDLK_BACKSPACE; \
      else if (strcmp(var.value, "escape") == 0)  remap_keys[idx] = SDLK_ESCAPE; \
      else if (strcmp(var.value, "x") == 0)       remap_keys[idx] = SDLK_x; \
      else if (strcmp(var.value, "return") == 0)  remap_keys[idx] = SDLK_RETURN; \
      else if (strcmp(var.value, "space") == 0)   remap_keys[idx] = SDLK_SPACE; \
      else if (strcmp(var.value, "z") == 0)       remap_keys[idx] = SDLK_z; \
      else if (strcmp(var.value, "l") == 0)       remap_keys[idx] = SDLK_l; \
      else if (strcmp(var.value, "f") == 0)       remap_keys[idx] = SDLK_f; \
      else remap_keys[idx] = default_remap_keys[idx]; \
    } else { \
      remap_keys[idx] = default_remap_keys[idx]; \
    } \
  } while(0)

  PARSE_KEY("onscripter_en_key_lookback", REMAP_LOOKBACK);
  PARSE_KEY("onscripter_en_key_menu", REMAP_MENU);
  PARSE_KEY("onscripter_en_key_advance", REMAP_ADVANCE);
}

/* KiriKiri frame. The engine owns its own thread and publishes a video surface;
 * this drives input, pacing, the single game clock, audio and the framebuffer
 * handoff. Runs only while KiriKiri is the active engine (vtable == NULL). */
static void kiri_frame(void)
{
    /* The game terminated itself (KAG `[close]` -> engine TVPTerminateAsync).
     * Per the desired behaviour: pause the core permanently, blank the screen,
     * reset the state — and do NOT let the frontend quit. The engine already
     * blanked its surface and is idle; stop polling it and present black. */
    { volatile int *stopped = krkr_get_engine_stopped_ptr();
      if (stopped && *stopped) {
        static uint32_t stopped_black[640 * 480];
        memset(stopped_black, 0, sizeof(stopped_black));
        video_cb(stopped_black, 640, 480, 640 * 4);
        return;
      }
    }
    /* Frontend poll heartbeat. RetroArch has no core-visible pause callback
     * (it never calls retro_run while paused, and 1.22 does not use
     * retro_pause/retro_unpause), so pause is signalled implicitly: the engine
     * idles its loop once this stops being refreshed. Stamp it every frame. */
    {
      struct timespec hb;
      clock_gettime(CLOCK_MONOTONIC, &hb);
      krkr_notify_poll((long long)hb.tv_sec * 1000 + hb.tv_nsec / 1000000);
    }
    /* libretro fast-forward: lift the engine's frame limiter and skip the
     * core's own pacing below so it runs unthrottled. */
    bool kiri_ff = false;
    environ_cb(RETRO_ENVIRONMENT_GET_FASTFORWARDING, &kiri_ff);
    krkr_set_fastforward(kiri_ff ? 1 : 0);
    /* Drive the single game clock. The `!vtable` KiriKiri branch returns before
     * the install block further down, so the rate must be set here too — without
     * this the clock stays at 1.0 for KiriKiri and all timed work (typewriter,
     * transitions, script waits) ignores fast-forward even though frame-driven
     * work speeds up. Same cached compare as the other branch. */
    {
      read_ff_speed_option();               /* KiriKiri returns before the main option block */
      double want = kiri_ff ? opt_ff_scale : 1.0;
      if (want != clock_scale_applied) {
        clock_scale_applied = want;
        ons_libretro_clock_set(want);
      }
      static int kcdbg = -1;
      if (kcdbg < 0) kcdbg = getenv("ONS_CLOCK_DEBUG") ? 1 : 0;
      if (kcdbg) {
        static int kcdbg_count = 0;
        if (++kcdbg_count >= 30) {
          kcdbg_count = 0;
          fprintf(stderr, "[clock] kiri ff=%d scale=%.3f t=%lldms\n",
                  (int)kiri_ff, clock_scale_applied, ons_libretro_game_clock_ms());
        }
      }
    }
    /* Pace to 60 fps. The engine's fpslimit plugin is a no-op and its main
     * loop has no pacing, so RetroArch would otherwise call retro_run as fast
     * as the video driver allows (600+ fps unlocked), copying and uploading a
     * full 800x600 frame every time. Cap it here; audio is fed from the
     * engine ring below and RetroArch's audio_sync follows. Fast-forward
     * bypasses this pacing. */
    { static struct timespec last = {0,0};
      struct timespec now;
      clock_gettime(CLOCK_MONOTONIC, &now);
      if (!kiri_ff && last.tv_sec != 0) {
        double dt = (now.tv_sec - last.tv_sec) + (now.tv_nsec - last.tv_nsec) / 1e9;
        const double tgt = 1.0 / 60.0;
        if (dt < tgt) {
          double wait = tgt - dt;
          struct timespec rem;
          rem.tv_sec  = (time_t)wait;
          rem.tv_nsec = (long)((wait - (double)rem.tv_sec) * 1e9);
          nanosleep(&rem, NULL);
        }
      }
      clock_gettime(CLOCK_MONOTONIC, &now);
      last = now;
    }
    if (!kiri_input_wired && SDL_libretro_input_state_cb && input_poll_cb) {
      krkr_set_input_callbacks(SDL_libretro_input_state_cb, input_poll_cb);
      kiri_input_wired = true;
    }
    void **surf_ptr = krkr_get_surface_ptr();
    if (input_poll_cb) input_poll_cb();
    krkr_pump_events();
    static int kiri_dbg = 0;
    if (kiri_dbg < 5) { fprintf(stderr, "KIRI retro_run: surf_ptr=%p *surf_ptr=%p\n", surf_ptr, surf_ptr ? *surf_ptr : NULL); kiri_dbg++; }
    if (surf_ptr && *surf_ptr) {
      char *s = (char*)*surf_ptr;
      // SDL2 SDL_Surface layout on x86-64:
      // flags(4) + pad(4) + format(8) + w(4) + h(4) + pitch(4) + pad(4) + pixels(8)
      int w = *(int*)(s + 16);
      int h = *(int*)(s + 20);
      int pitch = *(int*)(s + 24);
      void *pixels = *(void**)(s + 32);
      /* Trace every geometry change. Two things appear here: a genuine CRT
       * SuperRez mode (large but sane, e.g. 1920x240) versus a dangling-surface
       * read (garbage like 2097259x6225971), and a surface being replaced by
       * another one that took over rendering (a modal/dialog, say) — which is
       * exactly why the code below only ever *grows* the reported geometry. */
      {
        static int tw = -1, th = -1, tp = -1;
        if (w != tw || h != th || pitch != tp) {
          audio_logf("KRKR geom surf=%p w=%d h=%d pitch=%d pixels=%p bytes=%lld\n",
                     *surf_ptr, w, h, pitch, pixels, (long long)pitch * h);
          tw = w; th = h; tp = pitch;
        }
      }
      /* Sanity-bound the geometry before using it. SDL_libretro_surface is a
       * pointer the engine publishes from another thread; if it dangles (or
       * points at a half-freed surface) these fields are garbage and the
       * downstream pitch*h copy faults. A genuine CRT SuperRez mode is wide
       * and short (e.g. 1920x240), so the horizontal cap is generous (8192)
       * while the vertical one stays at real 4K-vertical (2160); garbage is
       * overwhelmingly negative or gigantic (2097259x6225971 above), so
       * bounds that are merely a bit loose still catch the vast majority.
       * Note the caps are asymmetric on purpose: a square 8192x8192 surface
       * would be 256 MiB at 32bpp, which is not a frame anyone ships. Drop
       * the frame rather than trust it. */
      if (w <= 0 || h <= 0 || w > 8192 || h > 2160 ||
          pitch < w * 2 || pitch > w * 8) {
        static int last_dbg = 0;
        if (last_dbg < 8) {
          audio_logf("KRKR REJECT geom surf=%p w=%d h=%d pitch=%d pixels=%p\n",
                     *surf_ptr, w, h, pitch, pixels);
          fprintf(stderr, "krkr: rejecting implausible surface w=%d h=%d pitch=%d pixels=%p\n",
                  w, h, pitch, pixels);
          last_dbg++;
        }
        return;
      }
      /* retro_get_system_av_info() runs before the engine exists, so it can
       * only guess 640x480. Report the real (larger) geometry once it is known
       * so RetroArch presents the native frame (e.g. 800x600) instead of
       * cropping to 640x480.
       *
       * Grow-only: the engine repoints its surface to modal dialogs (a save
       * confirmation is 317x117), and shrinking the reported geometry would
       * resize the whole RetroArch window and misalign all further input
       * coordinates. So only ever report a surface larger than everything seen
       * so far; anything smaller is a dialog and is ignored. */
      {
        static int max_w = 0, max_h = 0;
        if (w > 0 && h > 0 && (w > max_w || h > max_h)) {
          struct retro_system_av_info av = {};
          av.geometry.base_width = w;
          av.geometry.base_height = h;
          av.geometry.max_width = w;
          av.geometry.max_height = h;
          av.geometry.aspect_ratio = (float)w / (float)h;
          av.timing.fps = 60.0;
          av.timing.sample_rate = 48000.0;
          environ_cb(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &av);
          fprintf(stderr, "krkr_audio: SET_SYSTEM_AV_INFO %dx%d @ 48000 Hz\n", w, h);
          max_w = w;
          max_h = h;
        }
      }
      if (pixels) {
        /* Auto/skip mode badge: mirror of the game's own state (kag.tflags
         * .autoMode -> krkrsdl2_auto_mode: 0 off, 1 skip, 2 auto), matching the
         * ons/pons indicators so the same core option applies. */
        volatile int *am_ptr = krkr_get_auto_mode_ptr();
        int am = am_ptr ? *am_ptr : 0;
        bool draw_auto = opt_indicators && am == 2;
        bool draw_skip = opt_indicators && am == 1;
        bool draw_ff = opt_indicators && kiri_ff;
        /* Software cursor: the engine renders no pointer (dummy video
         * driver), so overlay one on a shadow copy of the framebuffer. */
        if (opt_software_cursor || draw_auto || draw_skip || draw_ff) {
          static Uint32 *kshadow = NULL;
          static int kshadow_sz = 0;
          int fb = pitch * h;
          if (fb > kshadow_sz) { kshadow = (Uint32*)realloc(kshadow, fb); kshadow_sz = fb; }
          if (kshadow) {
            memcpy(kshadow, pixels, fb);
            int kpitch = pitch / 4;
            if (opt_software_cursor) {
              int cx = 0, cy = 0;
              krkr_get_mouse_pos(&cx, &cy);
              /* cx/cy are negative until real pointer input arrives; nothing
               * is drawn then (a clipped arrow would otherwise appear at the
               * top-left corner). */
              /* Pre-rendered 12x18 cursor sprite: the art below is interpreted
               * once, into opaque pixels (0 = transparent), then each frame is
               * a straight blit instead of a character test per pixel. '#' =
               * black body, '.' = white outline, ' ' = clear. The stem is
               * offset from the tip, classic Windows-arrow shape. */
              #define ONS_CURSOR_ART \
                "##          ", \
                "#.#         ", \
                "#..#        ", \
                "#...#       ", \
                "#....#      ", \
                "#.....#     ", \
                "#......#    ", \
                "#.......#   ", \
                "#........#  ", \
                "#.........# ", \
                "#..........#", \
                "#.....######", \
                " #..#.#     ", \
                " #.#..#     ", \
                " ##...#     ", \
                " #....#     ", \
                "  #..#      ", \
                "   ##       "
              static Uint32 cursor_sprite[18 * 12];
              static bool   cursor_ready = false;
              if (!cursor_ready) {
                static const char *const cursor_art[18] = { ONS_CURSOR_ART };
                for (int dy = 0; dy < 18; dy++)
                  for (int dx = 0; dx < 12; dx++) {
                    char ch = cursor_art[dy][dx];
                    cursor_sprite[dy * 12 + dx] = (ch == '#') ? 0xFF000000u
                                                : (ch == '.') ? 0xFFFFFFFFu : 0u;
                  }
                cursor_ready = true;
              }
              for (int dy = 0; dy < 18 && cx >= 0 && cy >= 0; dy++) {
                for (int dx = 0; dx < 12; dx++) {
                  Uint32 v = cursor_sprite[dy * 12 + dx];
                  if (!v) continue;
                  int sx = cx + dx, sy = cy + dy;
                  if (sx < 0 || sx >= w || sy < 0 || sy >= h) continue;
                  kshadow[sy * kpitch + sx] = v;
                }
              }
            } /* opt_software_cursor */
            /* Mode indicators (top-right), matching the ons/pons colors. */
            {
              int bx = w - 12, by = 4;
              if (draw_auto) {
                for (int dy = 0; dy < 8; dy++)
                  for (int dx = 0; dx < 8; dx++)
                    if (by+dy >= 0 && by+dy < h && bx+dx >= 0 && bx+dx < w)
                      kshadow[(by+dy) * kpitch + (bx+dx)] = 0xCC22AA22;
                bx -= 12;
              }
              if (draw_skip) {
                for (int dy = 0; dy < 8; dy++)
                  for (int dx = 0; dx < 8; dx++)
                    if (by+dy >= 0 && by+dy < h && bx+dx >= 0 && bx+dx < w)
                      kshadow[(by+dy) * kpitch + (bx+dx)] = 0xCCAAAA22;
                bx -= 12;
              }
              if (draw_ff) {
                for (int dy = 0; dy < 8; dy++)
                  for (int dx = 0; dx < 8; dx++)
                    if (by+dy >= 0 && by+dy < h && bx+dx >= 0 && bx+dx < w)
                      kshadow[(by+dy) * kpitch + (bx+dx)] = 0xCC2266FF;
              }
            }
            video_cb(kshadow, w, h, pitch);
          } else {
            video_cb(pixels, w, h, pitch);
          }
        } else {
          video_cb(pixels, w, h, pitch);
        }
      } else {
        video_cb(NULL, 0, 0, 0);
      }
    } else {
      video_cb(NULL, 0, 0, 0);
    }
    // Audio: drain everything the engine has produced this frame and emit
    // exactly that. RetroArch's audio_sync adapts its pacing to the amount we
    // submit, so padding to a fixed frame count (previously 735) caused
    // periodic underruns -> "choppy" playback. The engine runs at 48kHz
    // (see -wsfreq in engine_kirikiri_cabi.cpp) and we advertise the same.
    {
      static int16_t audio_buf[16384 * 2];
      /* Submit about one frame of audio, not the whole ring.
       *
       * The engine's FAudio thread produces audio in real time (48000 Hz) into
       * a 16384-frame ring. If the engine ever gets ahead — a hitch, a pause, a
       * slow first frame — the ring holds a large backlog, and draining it in
       * full every frame means we hand RetroArch far more than real time. With
       * audio_sync on, RA's threaded pipeline publishes into a ring of only
       * AUDIO_PIPE_RING_FRAMES (3) video frames and blocks the core in an
       * eventcount wait when that fills, so an over-submitting core stalls the
       * whole frame (observed: 150-220 ms frame times, ~2% CPU, sawtooth) until
       * fast-forward switches the pipeline to its non-blocking state.
       *
       * A video-novel core has no use for a multi-frame audio backlog: dropping
       * it is strictly better than playing it late. So bound the submit to
       * ceil(rate/fps) plus a little slack, and drop whatever is beyond it by
       * advancing the read pointer (audio is a stream; stale samples have no
       * value). The ring's own lapped-writer guard is not enough — the surplus
       * must be discarded here, between frames. */
      const int sub_rate = krkr_audio_rate() ? krkr_audio_rate() : 48000;
      const int per_frame = sub_rate / 60 + 16;       /* one video frame + slack */
      int got = krkr_get_audio(audio_buf, per_frame);
      if (got > 0 && audio_batch_cb)
        audio_batch_cb(audio_buf, got);
    }
}


/* Pull one batch from the engine's mixer and hand it to the frontend.
 *
 * `rate` is passed in because retro_run already fetched it for the AV geometry;
 * calling the accessor again per frame would be wasted work. */
static void ons_submit_audio(int rate, int16_t *stream,
                             size_t max_frames, size_t frames)
{
  if (!engine || !vtable->getAudio || frames > max_frames) return;
  /* Paused or closed audio yields nothing; getAudio reports that as 0. */
  if (vtable->isAudioPaused && vtable->isAudioPaused(engine)) {
    static bool logged = false;
    if (!logged) { logged = true; audio_logf("STATUS paused freq=%d\n", rate); }
    return;
  }
  int got = vtable->getAudio(engine, stream, (int)frames);
  if (got <= 0) return;
  /* Heartbeat every ~120 frames: does the mixer actually produce samples
   * (non-silence), or is it handing back zeros? */
  static int hb = 0;
  if (++hb >= 120) {
    hb = 0;
    int peak = 0;
    for (size_t i = 0; i < (size_t)got * 2; i++) {
      int v = stream[i]; if (v < 0) v = -v; if (v > peak) peak = v;
    }
    audio_logf("HB freq=%d frames=%d peak=%d running=%d\n",
               rate, got, peak, engine_running);
  }
  audio_batch_cb(stream, (size_t)got);
}


/* Choose between a direct framebuffer hand-off and a shadow copy that
 * carries the mode badges and software cursor. The engine framebuffer is
 * never modified.
 *
 * Takes an EngineSurface rather than an SDL_Surface: the core does not include
 * SDL headers, so the engine's SDL cannot be confused with an SDL2/SDL3 the
 * frontend loads globally. Same four fields. */
static void ons_present_frame(const EngineSurface *screen)
{
  bool draw_auto = opt_indicators && engine && vtable->isAutoMode(engine);
  bool draw_skip = opt_indicators && engine && vtable->isSkipMode(engine);
// Use a shadow buffer for overlays (indicators + cursor) so the engine's
// framebuffer is never modified. Always use shadow when indicators or
// cursor are enabled; otherwise pass the framebuffer directly.
bool need_shadow = opt_software_cursor || draw_auto || draw_skip;
if (need_shadow && screen && screen->pixels) {
  static Uint32 *shadow = NULL;
  static int shadow_size = 0;
  int fb_size = screen->width * screen->height * 4;
  if (fb_size > shadow_size) {
    shadow = (Uint32 *)realloc(shadow, fb_size);
    shadow_size = fb_size;
  }
  memcpy(shadow, screen->pixels, fb_size);
  int w = screen->width, h = screen->height;
  int pitch = screen->pitch / 4;
  /* Draw mode indicators (top-right corner) */
  int bx = w - 12, by = 4;
  if (draw_auto) {
    for (int dy = 0; dy < 8; dy++)
      for (int dx = 0; dx < 8; dx++)
        if (by+dy >= 0 && by+dy < h && bx+dx >= 0 && bx+dx < w)
          shadow[(by+dy) * pitch + (bx+dx)] = 0xCC22AA22;
    bx -= 12;
  }
  if (draw_skip) {
    for (int dy = 0; dy < 8; dy++)
      for (int dx = 0; dx < 8; dx++)
        if (by+dy >= 0 && by+dy < h && bx+dx >= 0 && bx+dx < w)
          shadow[(by+dy) * pitch + (bx+dx)] = 0xCCAAAA22;
  }
  /* Draw software cursor on shadow (not on engine framebuffer) */
  if (opt_software_cursor) {
    int cx, cy;
    SDL_libretro_get_mouse_pos(&cx, &cy);
    /* Off-screen (negative) until real pointer input: draw nothing then. */
    for (int dy = 0; dy < 12 && cx >= 0 && cy >= 0; dy++) {
      for (int dx = 0; dx < 12; dx++) {
        int sx = cx + dx, sy = cy + dy;
        if (sx >= 0 && sx < w && sy >= 0 && sy < h) {
          if (dx == 0 || dy == dx)
            shadow[sy * pitch + sx] = 0xFFFFFFFF;
        }
      }
    }
  }
  video_cb(shadow, screen->width, screen->height, screen->pitch);
} else if (screen && screen->pixels) {
  video_cb(screen->pixels, screen->width, screen->height, screen->pitch);
} else {
  video_cb(NULL, 0, 0, 0);
}
}

/* Drive the single game clock. Only touch it when the effective rate
 * actually changes (FF on/off, or the option changed) — a plain double
 * compare per frame, no store, no call in the common case. `scale` is the
 * Nx only while FF is on; 1.0 otherwise. */
static void ons_drive_clock(bool ff)
{
  double want = ff ? opt_ff_scale : 1.0;
  if (want != clock_scale_applied) {
    clock_scale_applied = want;
    ons_libretro_clock_set(want);
  }
  /* ONS_CLOCK_DEBUG: ~2 Hz trace of the live clock so a human can watch the
   * bias grow at (scale-1)x while FF is on and freeze when it is off. */
  static int cdbg = -1;
  if (cdbg < 0) cdbg = getenv("ONS_CLOCK_DEBUG") ? 1 : 0;
  if (cdbg) {
    static int cdbg_count = 0;
    if (++cdbg_count >= 30) {
      cdbg_count = 0;
      fprintf(stderr, "[clock] ff=%d scale=%.3f t=%lldms\n",
              (int)ff, clock_scale_applied, ons_libretro_game_clock_ms());
    }
  }
}

/* Frontend heartbeat for the ONS/PONS engine thread. RetroArch stops calling
 * retro_run entirely while its menu is open, so a paused flag set here would
 * never be observed. The engine thread instead watches this timestamp:
 * SDL_Delay() blocks when the heartbeat goes stale (see SDL_systimer.c), which
 * freezes text/animation exactly like a pause. */
static void stamp_frontend_heartbeat(void)
{
  struct timespec hb;
  clock_gettime(CLOCK_MONOTONIC, &hb);
  SDL_libretro_frontend_hb =
      (unsigned long long)hb.tv_sec * 1000 + hb.tv_nsec / 1000000;
}

/* Pointer options drive the input callback for *both* engines, and the KiriKiri
 * branch returns before the main option block, so read them where every path
 * sees them. */
static void read_pointer_options(void)
{
  struct retro_variable pvar = {0};
  pvar.key = "onscripter_en_pointer_enable";
  if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &pvar) && pvar.value)
    opt_pointer_enable = (strcmp(pvar.value, "enabled") == 0);
  pvar.key = "onscripter_en_pointer_source";
  if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &pvar) && pvar.value)
    opt_pointer_source = (strcmp(pvar.value, "left") == 0) ? POINTER_SOURCE_LEFT
                                                           : POINTER_SOURCE_RIGHT;
}

void retro_run(void)
{
  stamp_frontend_heartbeat();
  read_pointer_options();

  /* KiriKiri runs its own engine thread and owns its frame loop; ONS/PONS do
   * not, so the two paths are entirely separate. */
  if (!vtable) {
    kiri_frame();
    return;
  }
  if (!engine_running) {
    static Uint32 black[640*480];
    memset(black, 0, sizeof(black));
    video_cb(black, 640, 480, 640*4);
    return;
  }
  read_frame_options();
  // Detect fast-forward mode
  bool ff = false;
  environ_cb(RETRO_ENVIRONMENT_GET_FASTFORWARDING, &ff);
  SDL_libretro_fastforward = ff;
  ons_drive_clock(ff);
  if (!engine || !vtable) return;
  int av_enable = 1;
  if (environ_cb(RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE, &av_enable) && !(av_enable & 1)) {
    SDL_libretro_paused = true;
    return;
  }
  // Unpause: clear flag and drain accumulated timer events
  if (SDL_libretro_paused) {
    SDL_libretro_paused = false;
    /* Drop timer events the engine queued while paused instead of replaying
     * them at once, which would make the script jump forward. */
    if (engine && vtable->drainUserEvents) vtable->drainUserEvents(engine);
  }

  /* The engine owns its SDL now, so ask it for the framebuffer and the audio
   * rate rather than reaching into SDL from here. Both are NULL/0 until the
   * engine's video mode and audio are up. */
  const EngineSurface *screen = (engine && vtable->getSurface) ? vtable->getSurface(engine) : NULL;
  int rate = (engine && vtable->getSampleRate) ? vtable->getSampleRate(engine) : 0;
  if (rate <= 0) rate = 44100;
  /* retro_get_system_av_info() runs before load_game, when the engine does not
   * exist yet, so it can only guess. Once the engine's surface is up, report
   * its real internal resolution (ONScripter/Ponscripter derive it from the
   * script: commonly 640x480 or 800x600) so RetroArch sizes the viewport
   * correctly. Scaling/aspect stay RetroArch's decision.
   *
   * Sample rate: report the rate the engine actually negotiated with SDL, NOT
   * a hardcoded value. ONScripter/PONScripter open at 44100; advertising 48000
   * here made RetroArch consume 800 samples/frame while the core only submitted
   * 736 (round-up of 44100/60) — a 64-sample/frame deficit that drained
   * RetroArch's audio buffer and caused periodic BGM underruns. Re-emit
   * whenever the rate appears too, since the engine may open audio after the
   * first frame without the geometry changing. */
  if (screen && screen->width > 0 && screen->height > 0) {
    static int last_w = 0, last_h = 0, last_rate = 0;
    if (screen->width != last_w || screen->height != last_h || rate != last_rate) {
      struct retro_system_av_info av;
      av.geometry.base_width = screen->width;
      av.geometry.base_height = screen->height;
      av.geometry.max_width = screen->width;
      av.geometry.max_height = screen->height;
      av.geometry.aspect_ratio = (float)screen->width / (float)screen->height;
      av.timing.fps = 60.0;
      av.timing.sample_rate = (double)rate;
      environ_cb(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &av);
      fprintf(stderr, "ons_audio: SET_SYSTEM_AV_INFO %dx%d @ %d Hz (was %d Hz)\n",
              screen->width, screen->height, rate, last_rate);
      audio_logf("SET_SYSTEM_AV_INFO %dx%d @ %d Hz (was %d) frames/frame=%zu\n",
                 screen->width, screen->height, rate, last_rate, ((size_t)rate / 60 / 32 + 1) * 32);
      last_w = screen->width;
      last_h = screen->height;
      last_rate = rate;
    }
  }
  static const size_t max_frames = 2048;
  static int16_t stream[max_frames * 2];
  size_t frames = (size_t)rate / 60;
  frames = (frames / 32 + 1) * 32;
  input_poll_cb();
  SDL_libretro_input_ready = true;
  vtable->pumpEvents ? vtable->pumpEvents(engine) : (void)0;
  /* Geometry is left to the core's reported values: retro_get_system_av_info()
   * sizes the window and video_cb() dimensions drive the viewport each frame.
   * (A hardcoded 640x480 SET_GEOMETRY here would also fight the KiriKiri path,
   * which reports 800x600, and overrode the user's scaling/aspect settings.) */
#ifdef LIBRETRO_FRAME_SYNC
  if (pending_snapshot[0]) {
    const EngineSurface *snap_screen = vtable->getSurface ? vtable->getSurface(engine) : NULL;
    if (snap_screen && snap_screen->pixels) {
      char bmp_path[PATH_MAX_LENGTH];
      log_cb(RETRO_LOG_INFO, "Snapshot saved: %s\n", bmp_path);
    }
    pending_snapshot[0] = '\0';
  }
  if (pending_dumpstate[0]) {
    char dump_path[PATH_MAX_LENGTH];
    snprintf(dump_path, sizeof(dump_path), "%s/%s.txt", test_output_dir, pending_dumpstate);
    FILE *f = fopen(dump_path, "w");
    if (f) {
      engine_dump_state(engine, f);
      fclose(f);
      log_cb(RETRO_LOG_INFO, "State dump saved: %s\n", dump_path);
    }
    pending_dumpstate[0] = '\0';
  }
  /* wait_sem disabled — engine paces via event_sem only */
#endif

  // Show toast when engine state actually changes
  static bool prev_auto = false, prev_skip = false;
  bool cur_auto = engine && vtable->isAutoMode(engine);
  bool cur_skip = engine && vtable->isSkipMode(engine);
  if (cur_auto != prev_auto)  show_message(cur_auto ? "Auto: on" : "Auto: off");
  if (cur_skip != prev_skip) show_message(cur_skip ? "Skip: on" : "Skip: off");
  prev_auto  = cur_auto;
  prev_skip = cur_skip;

  // Draw mode indicators (auto/skip badges) on the shadow buffer
  // along with the cursor, so the engine framebuffer is never modified.
  ons_present_frame(screen);
  ons_submit_audio(rate, stream, max_frames, frames);
}
size_t retro_serialize_size(void)
{
  return 0;
}

bool retro_serialize(void *data, size_t size)
{
  return false;
}

bool retro_unserialize(const void *data, size_t size)
{
  return false;
}

void retro_cheat_reset(void) {}
void retro_cheat_set(unsigned index, bool enabled, const char *code) {}

bool retro_load_game_special(unsigned game_type, const struct retro_game_info *info, size_t num_info)
{
  return false;
}

void retro_unload_game(void)
{
  /* Force the engine reachable before tearing it down. If the frontend is
   * paused (or simply stopped calling retro_run), the engine thread is parked
   * in SDL_Delay's pause gate and would never observe the shutdown request, so
   * close-content would hang. Same rule as reset: unpause first. */
  SDL_libretro_paused = false;
  SDL_libretro_in_reset = true;   /* suspend the pause gate until we're gone */
  SDL_libretro_frontend_hb = 0;
  /* Close content ends this run: drop the FF epoch/bias and rebase the clock so
   * the next load starts on a fresh timeline (retro_load_game is not guaranteed
   * to follow a reset, and a frozen bias would otherwise leak into the new
   * game). Mirrors retro_reset(). */
  ons_libretro_clock_reset();
  clock_scale_applied = -1.0;     /* force retro_run to re-install the rate */

  if (!vtable) {
    // KiriKiri path: shut down the krkrsdl2 engine thread cleanly.
    krkr_shutdown();
    SDL_libretro_in_reset = false;
    return;
  }
  if (ons_thread) {
    stop_engine_thread();
  }
  SDL_DestroySemaphore(SDL_libretro_event_sem);
  SDL_libretro_event_sem = nullptr;
#ifdef LIBRETRO_FRAME_SYNC
  SDL_libretro_wait_sem = NULL; // unblock engine before destroying
  SDL_DestroySemaphore(fs_sem);
#endif
  vtable->destroy(engine);
  engine = nullptr;
  SDL_Quit();
  SDL_libretro_in_reset = false;
}

unsigned retro_get_region(void)
{
  return RETRO_REGION_NTSC;
}

void *retro_get_memory_data(unsigned id)
{
  return 0;
}

size_t retro_get_memory_size(unsigned id)
{
  return 0;
}


#ifdef ANDROID
extern "C" {
#undef fseek
int fseek_ons(FILE *stream, long offset, int whence) {
  return fseek(stream, offset, whence);
}

#undef ftell
long ftell_ons(FILE *stream) {
  return ftell(stream);
}

#undef fgetc
int fgetc_ons(FILE *stream) {
  return fgetc(stream);
}

#undef fgets
char *fgets_ons(char *s, int size, FILE *stream) {
  return fgets(s, size, stream);
}

#undef fread
size_t fread_ons(void *ptr, size_t size, size_t nmemb, FILE *stream) {
  return fread(ptr, size, nmemb, stream);
}

#undef fopen
FILE *fopen_ons(const char *str, const char *mode) {
  return fopen(str, mode);
}

#undef mkdir
extern int mkdir(const char *pathname, mode_t mode);
int mkdir_ons(const char *pathname, mode_t mode) {
  return mkdir(pathname, mode);
}

void playVideoAndroid(const char *filename) {}
int __android_log_print(int prio, const char *tag, const char *fmt, ...) {
  va_list va;
  va_start(va, fmt);
  log_cb(RETRO_LOG_INFO, fmt, va);
  va_end(va);
  return 0;
}
}
#endif
