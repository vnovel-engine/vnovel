#include <SDL_libretro.h>
#include <retro_miscellaneous.h>
#include <file/file_path.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "libretro_core_options.h"
#include "engine_cabi.h"

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
static jmp_buf engine_error_jmp;
static bool engine_error_jmp_valid = false;

extern "C" void libretro_script_error_longjmp(void)
{
  if (engine_error_jmp_valid) longjmp(engine_error_jmp, 1);
}

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

retro_input_state_t  SDL_libretro_input_state_cb;
bool SDL_libretro_input_ready = false;
SDL_AudioSpec       *SDL_libretro_audio_spec = NULL;
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
static int  opt_ff_speed = 50;
float opt_mouse_sensitivity = 1.0;
/* Key remap state */
#include "key_remap.h"

int remap_keys[REMAP_COUNT] = {
    [REMAP_ADVANCE]       = SDLK_RETURN,
    [REMAP_MENU]          = SDLK_BACKSPACE,
    [REMAP_SKIP]          = SDLK_s,
    [REMAP_AUTO]          = SDLK_a,
    [REMAP_LOOKBACK]      = SDLK_LEFTBRACKET,
    [REMAP_PAGE_FORWARD]  = SDLK_RIGHTBRACKET,
    [REMAP_TEXT_SPEED]    = SDLK_0,
    [REMAP_FULLSCREEN]    = SDLK_F11,
};
static retro_video_refresh_t       video_cb;
static retro_input_poll_t          input_poll_cb;
static retro_audio_sample_batch_t  audio_batch_cb;
static retro_environment_t         environ_cb;
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

/* Fixed RetroPad → ONScripter key mapping.
 * Users remap at the RetroArch Controls level (Quick Menu → Controls),
 * which maps physical buttons to RetroPad buttons. The core always reads
 * RetroPad buttons the same way — RetroArch handles the rest. */
static struct retro_input_descriptor input_descs[] = {
  { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,     "Up" },
  { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,   "Down" },
  { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,   "Left" },
  { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT,  "Right" },
  { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,      "Confirm / Advance" },
  { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,      "Skip Text" },
  { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,      "Back / Menu" },
  { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,      "Hide Text Window" },
  { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,      "Open Save Menu" },
  { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,      "Save" },
  { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,     "Scroll Back" },
  { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,     "Scroll Forward" },
  { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3,     "Reading Speed" },
  { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3,     "Quick Save" },
  { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,  "Auto Mode" },
  { 0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X,      "Mouse X" },
  { 0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y,      "Mouse Y" },
  { 0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT,   "Click" },
  { 0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_RIGHT,  "Menu" },
  { 0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_X,   "Touch X" },
  { 0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_Y,   "Touch Y" },
  { 0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_PRESSED, "Touch" },
  { 0, 0, 0, 0, NULL },
};

static void krkr_keyboard_cb(bool down, unsigned keycode, uint32_t, uint16_t);

void retro_set_environment(retro_environment_t cb)
{
  static struct retro_log_callback log;
  environ_cb = cb;
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

  // L button generates right-click to open the rmenu (save/load/lookback)
  SDL_libretro_rightclick_button = RETRO_DEVICE_ID_JOYPAD_L;
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
  SDL_libretro_input_state_cb = cb;
}

void retro_get_system_info(struct retro_system_info *info)
{
  info->need_fullpath = true;
  info->valid_extensions = "txt|dat|___|ons";
  info->library_version = "0.2";
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
  info->timing.sample_rate = 48000.0;  /* matches the engine (krkr -wsfreq=48000) */
}

static void frame_cb(retro_usec_t usec)
{
  delta = usec / 1000;
}


void retro_init(void)
{
  enum retro_pixel_format pixfmt = RETRO_PIXEL_FORMAT_XRGB8888;
  struct retro_frame_time_callback frametime = {
    .callback = frame_cb,
    .reference = 1000000 / 60,
  };
  environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &pixfmt);
  environ_cb(RETRO_ENVIRONMENT_SET_FRAME_TIME_CALLBACK, &frametime);
}

static int ons_main(void *data)
{
  if (setjmp(engine_error_jmp) != 0) {
    // ONScripter called errorAndExit() - caught by longjmp
    log_cb(RETRO_LOG_ERROR, "ONScripter script error - returning cleanly\n");
    engine_error_jmp_valid = false;
    engine_running = false;
    return 0;
  }
  engine_error_jmp_valid = true;
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
  engine_error_jmp_valid = false;
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
  void krkr_get_mouse_pos(int *x, int *y);
  void krkr_pump_events(void);
  void krkr_set_input_callbacks(retro_input_state_t state_cb, retro_input_poll_t poll_cb);
  void krkr_push_key(int sym, int down);
  int krkr_get_audio(int16_t *out_buf, int max_frames);
  void krkr_shutdown(void);
}

/* RetroArch keyboard callback -> engine key queue. Also feed the SDL2
 * compat path (SDL_libretro_keyboard_event) used by ONScripter engines. */
static void krkr_keyboard_cb(bool down, unsigned keycode, uint32_t, uint16_t)
{
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

  char archive_path[PATH_MAX_LENGTH];
  fill_pathname_basedir(archive_path, game->path, sizeof(archive_path));
  // Get save directory early (needed for KiriKiri path)
  const char *save_dir = NULL;
  environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &save_dir);

  // Detect engine: KiriKiri for .xp3, PONScripter for pscript.dat, ONScripter-EN otherwise
  char xp3_check[PATH_MAX_LENGTH];
  fill_pathname_join(xp3_check, archive_path, "data.xp3", sizeof(xp3_check));
  FILE *xp3_fp = fopen(xp3_check, "rb");
  if (xp3_fp) {
    fclose(xp3_fp);
    engine = krkr_create();
    log_cb(RETRO_LOG_INFO, "Using KiriKiri engine (data.xp3)\n");
    krkr_setArchivePath(engine, archive_path);
    krkr_setSavePath(engine, save_dir ? save_dir : archive_path);
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
  } else {
    vtable = &ons_engine_vtable; engine = vtable->create();
    log_cb(RETRO_LOG_INFO, "Using ONScripter-EN engine (nscript.dat)\n");
  }
  vtable->setArchivePath(engine, archive_path);

  // Honor RetroArch's save directory; fall back to game directory
  if (save_dir && save_dir[0]) {
    char save_path[PATH_MAX_LENGTH];
    fill_pathname_join(save_path, save_dir, "onscripter-en", sizeof(save_path));
    ons_mkdir(save_path);
    vtable->setSavePath(engine, save_path);
    strncpy(last_save_path, save_path, sizeof(last_save_path));
    test_output_dir = strdup(save_path);
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
  dbgvar.key = "onscripter-en_debug_level";
  if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &dbgvar) && dbgvar.value)
    opt_debug_level = atoi(dbgvar.value);
  vtable->setDebugLevel(engine, opt_debug_level);
  log_cb(RETRO_LOG_INFO, "Debug level: %d\n", opt_debug_level);
  ons_thread = SDL_CreateThread(ons_main, NULL);

  return true;
}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
}

void retro_deinit(void)
{
}

void retro_reset(void)
{
  if (ons_thread) {
    SDL_KillThread(ons_thread);
    ons_thread = nullptr;
  }
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
}

extern "C" void SDL_ThreadedTimerCheck(void);

void retro_run(void)
{
  // KiriKiri engine path: read from SDL_libretro_surface
  // Use void* to avoid SDL 1.2/SDL2 type conflict
  if (!vtable) {
    /* Pace to 60 fps. The engine's fpslimit plugin is a no-op and its main
     * loop has no pacing, so RetroArch would otherwise call retro_run as fast
     * as the video driver allows (600+ fps unlocked), copying and uploading a
     * full 800x600 frame every time. Cap it here; audio is fed from the
     * engine ring below and RetroArch's audio_sync follows. */
    { static struct timespec last = {0,0};
      struct timespec now;
      clock_gettime(CLOCK_MONOTONIC, &now);
      if (last.tv_sec != 0) {
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
    static bool kiri_input_wired = false;
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
      if (pixels) {
        /* Software cursor: the engine renders no pointer (dummy video
         * driver), so overlay one on a shadow copy of the framebuffer. */
        if (opt_software_cursor) {
          static Uint32 *kshadow = NULL;
          static int kshadow_sz = 0;
          int fb = pitch * h;
          if (fb > kshadow_sz) { kshadow = (Uint32*)realloc(kshadow, fb); kshadow_sz = fb; }
          if (kshadow) {
            memcpy(kshadow, pixels, fb);
            int cx = 0, cy = 0;
            krkr_get_mouse_pos(&cx, &cy);
            int kpitch = pitch / 4;
            /* 12x18 arrow cursor; '#' = black, '.' = white outline, ' ' = clear */
            static const char *arrow[18] = {
              "##          ",
              "#.#         ",
              "#..#        ",
              "#...#       ",
              "#....#      ",
              "#.....#     ",
              "#......#    ",
              "#.......#   ",
              "#........#  ",
              "#.........# ",
              "#..........#",
              "#.....######",
              "#..#.#      ",
              "#.#..#      ",
              "##...#      ",
              "#....#      ",
              " #..#       ",
              "  ##        ",
            };
            for (int dy = 0; dy < 18; dy++) {
              for (int dx = 0; dx < 12; dx++) {
                char c = arrow[dy][dx];
                if (c == ' ') continue;
                int sx = cx + dx, sy = cy + dy;
                if (sx < 0 || sx >= w || sy < 0 || sy >= h) continue;
                kshadow[sy * kpitch + sx] = (c == '#') ? 0xFF000000 : 0xFFFFFFFF;
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
      int got = krkr_get_audio(audio_buf, 16384);
      if (got > 0 && audio_batch_cb)
        audio_batch_cb(audio_buf, got);
    }
    return;
  }
  if (!engine_running) {
    static Uint32 black[640*480];
    memset(black, 0, sizeof(black));
    video_cb(black, 640, 480, 640*4);
    return;
  }
#ifdef LIBRETRO_FRAME_SYNC
  static bool frame_sync_activated = false;
  if (!frame_sync_activated) {
    SDL_Event ev;
    while (SDL_PeepEvents(&ev, 1, SDL_GETEVENT, SDL_EVENTMASK(SDL_USEREVENT)) > 0)
      ;
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

  var.key = "onscripter-en_frame_sync";
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

  var.key = "onscripter-en_mode_indicators";
  if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    opt_indicators = (strcmp(var.value, "enabled") == 0);

  var.key = "onscripter-en_debug_level";
  if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
    int new_debug = atoi(var.value);
    if (new_debug != opt_debug_level) {
      opt_debug_level = new_debug;
      if (engine) vtable->setDebugLevel(engine, opt_debug_level);
    }
  }

  var.key = "onscripter-en_software_cursor";
  if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    opt_software_cursor = (strcmp(var.value, "enabled") == 0);

  var.key = "onscripter-en_text_speed";
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

  var.key = "onscripter-en_fastforward_speed";
  if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
    if (strcmp(var.value, "2x") == 0)       SDL_libretro_ff_speed = 10;
    else if (strcmp(var.value, "4x") == 0)  SDL_libretro_ff_speed = 25;
    else if (strcmp(var.value, "8x") == 0)  SDL_libretro_ff_speed = 50;
    else if (strcmp(var.value, "16x") == 0) SDL_libretro_ff_speed = 100;
    else if (strcmp(var.value, "32x") == 0) SDL_libretro_ff_speed = 200;
    else if (strcmp(var.value, "max") == 0)  SDL_libretro_ff_speed = 1000;
  }

  var.key = "onscripter-en_mouse_sensitivity";
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

  PARSE_KEY("onscripter-en_key_lookback", REMAP_LOOKBACK);
  PARSE_KEY("onscripter-en_key_menu", REMAP_MENU);
  PARSE_KEY("onscripter-en_key_advance", REMAP_ADVANCE);
  // Detect fast-forward mode
  bool ff = false;
  environ_cb(RETRO_ENVIRONMENT_GET_FASTFORWARDING, &ff);
  SDL_libretro_fastforward = ff;
  if (ff) SDL_libretro_ff_offset += SDL_libretro_ff_speed;
  else SDL_libretro_ff_offset = 0;
  if (!engine || !vtable) return;
  int av_enable = 1;
  if (environ_cb(RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE, &av_enable) && !(av_enable & 1)) {
    SDL_libretro_paused = true;
    return;
  }
  // Unpause: clear flag and drain accumulated timer events
  if (SDL_libretro_paused) {
    SDL_libretro_paused = false;
    SDL_Event ev;
    while (SDL_PeepEvents(&ev, 1, SDL_GETEVENT, SDL_EVENTMASK(SDL_USEREVENT)) > 0)
      ;
  }

  SDL_Surface *screen = SDL_GetVideoSurface();
  SDL_AudioSpec *spec = SDL_libretro_audio_spec;
  static const size_t max_frames = 2048;
  static int16_t stream[max_frames * 2];
  size_t frames = (spec ? spec->freq : 44100) / 60;
  frames = (frames / 32 + 1) * 32;
  input_poll_cb();
  SDL_libretro_input_ready = true;
  SDL_libretro_PumpEvents();
  /* Force RetroArch to recalculate the GL viewport on every frame.
   * Fixes scaling bug when toggling fullscreen mid-scene. */
  struct retro_game_geometry geom;
  geom.base_width = 640;
  geom.base_height = 480;
  geom.max_width = 640;
  geom.max_height = 480;
  geom.aspect_ratio = 4.0 / 3.0;
  environ_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &geom);
#ifdef LIBRETRO_FRAME_SYNC
  if (pending_snapshot[0]) {
    SDL_Surface *snap_screen = SDL_GetVideoSurface();
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
  bool draw_auto = opt_indicators && engine && vtable->isAutoMode(engine);
  bool draw_skip = opt_indicators && engine && vtable->isSkipMode(engine);
  // Use a shadow buffer for overlays (indicators + cursor) so the engine's
  // framebuffer is never modified. Always use shadow when indicators or
  // cursor are enabled; otherwise pass the framebuffer directly.
  bool need_shadow = opt_software_cursor || draw_auto || draw_skip;
  if (need_shadow && screen && screen->pixels) {
    static Uint32 *shadow = NULL;
    static int shadow_size = 0;
    int fb_size = screen->w * screen->h * 4;
    if (fb_size > shadow_size) {
      shadow = (Uint32 *)realloc(shadow, fb_size);
      shadow_size = fb_size;
    }
    memcpy(shadow, screen->pixels, fb_size);
    int w = screen->w, h = screen->h;
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
      for (int dy = 0; dy < 12; dy++) {
        for (int dx = 0; dx < 12; dx++) {
          int sx = cx + dx, sy = cy + dy;
          if (sx >= 0 && sx < w && sy >= 0 && sy < h) {
            if (dx == 0 || dy == dx)
              shadow[sy * pitch + sx] = 0xFFFFFFFF;
          }
        }
      }
    }
    video_cb(shadow, screen->w, screen->h, screen->pitch);
  } else if (screen && screen->pixels) {
    video_cb(screen->pixels, screen->w, screen->h, screen->pitch);
  } else {
    video_cb(NULL, 0, 0, 0);
  }
  /* Audio: call mixer callback directly. No lock — the engine rarely
 * holds the mixer lock (only during Mix_PlayChannel etc.), and the
 * callback is fast. This avoids ring buffer underruns. */
  if (spec && SDL_GetAudioStatus() == SDL_AUDIO_PLAYING && frames <= max_frames) {
    memset(stream, 0, frames * 4);
    { static int tc = 0; Uint32 t0 = SDL_GetTicks();
    spec->callback(NULL, (uint8_t *)stream, frames * 4);
    Uint32 t1 = SDL_GetTicks();
    if (tc < 20) { log_cb(RETRO_LOG_INFO, "[%u] audio: mix=%dms frames=%zu\n", t0, t1-t0, frames); tc++; } }
    audio_batch_cb(stream, frames);
  }
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
  if (!vtable) {
    // KiriKiri path: shut down the krkrsdl2 engine thread cleanly.
    krkr_shutdown();
    return;
  }
  if (ons_thread) {
    SDL_KillThread(ons_thread);
    ons_thread = nullptr;
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
