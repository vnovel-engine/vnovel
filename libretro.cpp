#include <SDL_libretro.h>
#include <retro_miscellaneous.h>
#include <file/file_path.h>
#include <setjmp.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "libretro_core_options.h"

#ifdef _WIN32
#define ons_mkdir(path) mkdir(path)
#else
#define ons_mkdir(path) mkdir(path, 0755)
#endif

// ONScripter calls exit() on script errors and SDL init failures.
// We catch these via longjmp. errorAndExit() calls ons_script_error_longjmp()
// which longjmps back to ons_main's setjmp checkpoint.
static jmp_buf ons_error_jmp;
static bool ons_error_jmp_valid = false;

extern "C" void ons_script_error_longjmp(void)
{
  if (ons_error_jmp_valid) longjmp(ons_error_jmp, 1);
}

//don't use #include <onscripter/ONScripter.h>, use #include <ONScripterLabel.h> instead (stop including wrong header file)  
#include <ONScripterLabel.h>

retro_input_state_t  SDL_libretro_input_state_cb;
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

static void                        fallback_log(enum retro_log_level level, const char *fmt, ...);
static retro_log_printf_t          log_cb = fallback_log;
static retro_video_refresh_t       video_cb;
static retro_input_poll_t          input_poll_cb;
static retro_audio_sample_batch_t  audio_batch_cb;
static retro_environment_t         environ_cb;
static ONScripterLabel *ons = nullptr;
static SDL_Thread                 *ons_thread;
static uint32_t                    delta;


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
  { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Skip Mode" },
  { 0, 0, 0, 0, NULL },
};

void retro_set_environment(retro_environment_t cb)
{
  static struct retro_log_callback log;
  environ_cb = cb;
  if (environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log))
    log_cb = log.log;
  environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, input_descs);

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
  int width = ons ? ons->getWidth() : 0;
  int height = ons ? ons->getHeight() : 0;
  // Clamp to sane defaults — initSDL() may not have run yet
  if (width <= 0 || width > 4096) width = 640;
  if (height <= 0 || height > 4096) height = 480;
  info->geometry.base_width = width;
  info->geometry.base_height = height;
  info->geometry.max_width = width;
  info->geometry.max_height = height;
  info->geometry.aspect_ratio = 0.0;
  info->timing.fps = 60.0;
  info->timing.sample_rate = 44100.0;
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
  if (setjmp(ons_error_jmp) != 0) {
    // ONScripter called errorAndExit() - caught by longjmp
    log_cb(RETRO_LOG_ERROR, "ONScripter script error - returning cleanly\n");
    ons_error_jmp_valid = false;
    return 0;
  }
  ons_error_jmp_valid = true;

  if (ons->init()) {
    log_cb(RETRO_LOG_ERROR, "Failed to initialize ONScripter\n");
    ons_error_jmp_valid = false;
    return -1;
  }

  SDL_ShowCursor(SDL_DISABLE);
  ons->executeLabel();
  ons_error_jmp_valid = false;
  return 0;
}

bool retro_load_game(const struct retro_game_info *game)
{
  if (!game)
    return false;

  char archive_path[PATH_MAX_LENGTH];
  fill_pathname_basedir(archive_path, game->path, sizeof(archive_path));
  ons = new ONScripterLabel();
  ons->setArchivePath(archive_path);

  // Honor RetroArch's save directory; fall back to game directory
  const char *save_dir = NULL;
  if (environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &save_dir) && save_dir && save_dir[0]) {
    char save_path[PATH_MAX_LENGTH];
    fill_pathname_join(save_path, save_dir, "onscripter-en", sizeof(save_path));
    ons_mkdir(save_path);
    ons->setSavePath(save_path);
  } else {
    ons->setSavePath(archive_path);
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
        ons->setKeyEXE(test_path);
        log_cb(RETRO_LOG_INFO, "Using key from: %s\n", test_path);
        break;
      }
    }
  }

  SDL_libretro_event_sem = SDL_CreateSemaphore(0);
#ifdef LIBRETRO_FRAME_SYNC
  // Semaphore created here but not assigned yet — engine init flushes must not block
  fs_sem = SDL_CreateSemaphore(0); // created here, activated in retro_run
  SDL_libretro_wait_sem = NULL; // disabled during init
#endif

  // Apply core options
  ons->setDebugLevel(opt_debug_level);
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
#ifdef LIBRETRO_FRAME_SYNC
  SDL_sem *saved_wait_sem = SDL_libretro_wait_sem;
  SDL_libretro_wait_sem = NULL;
#endif
  if (ons) {
    ons->resetCommand();
  }
#ifdef LIBRETRO_FRAME_SYNC
  SDL_libretro_wait_sem = saved_wait_sem;
#endif
}

void retro_run(void)
{
#ifdef LIBRETRO_FRAME_SYNC
  // Activate frame sync on the first retro_run call.
  // By now the engine has finished init and is in its event loop.
  static bool frame_sync_activated = false;
  if (!frame_sync_activated) {
    if (opt_frame_sync)
      SDL_libretro_wait_sem = fs_sem;
    frame_sync_activated = true;
  }

  // Re-read core options each frame (allows hot-reload from Quick Menu)
  struct retro_variable var = {0};

  var.key = "onscripter-en_frame_sync";
  if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
    bool new_sync = (strcmp(var.value, "enabled") == 0);
    if (new_sync != opt_frame_sync) {
      opt_frame_sync = new_sync;
      if (opt_frame_sync && !SDL_libretro_wait_sem)
        SDL_libretro_wait_sem = fs_sem;
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
      if (ons) ons->setDebugLevel(opt_debug_level);
    }
  }
#endif

  // If the frontend has disabled video (e.g. menu open), pause the ONScripter thread
  // by not posting the event semaphore — the thread blocks in DUMMY_PumpEvents
  int av_enable = 1;
  if (environ_cb(RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE, &av_enable) && !(av_enable & 1)) {
    // Still need to provide a frame to RA
    SDL_Surface *screen = SDL_GetVideoSurface();
    if (screen)
      video_cb(screen->pixels, screen->w, screen->h, screen->pitch);
    else
      video_cb(NULL, 0, 0, 0);
    return;
  }

  SDL_Surface *screen = SDL_GetVideoSurface();
  SDL_AudioSpec *spec = SDL_libretro_audio_spec;
  static const size_t max_frames = 2048;
  static int16_t stream[max_frames * 2];
  size_t frames = delta * 44100 / 1000;
  frames = (frames / 32 + 1) * 32;

  input_poll_cb();

  SDL_libretro_PumpEvents();

#ifdef LIBRETRO_FRAME_SYNC
  // Wake the engine thread — one frame's worth of events has been injected
  SDL_SemPost(SDL_libretro_wait_sem);
#endif

  // Show toast when engine state actually changes
  static bool prev_auto = false, prev_skip = false;
  bool cur_auto = ons && ons->isAutoMode();
  bool cur_skip = ons && ons->isSkipMode();
  if (cur_auto != prev_auto)  show_message(cur_auto ? "Auto: on" : "Auto: off");
  if (cur_skip != prev_skip) show_message(cur_skip ? "Skip: on" : "Skip: off");
  prev_auto  = cur_auto;
  prev_skip = cur_skip;

  // Draw discreet persistent mode indicators in top-right corner.
  // Save/restore the pixels under the badges so they don't persist on the
  // ONScripter surface between frames (which uses dirty-rect rendering).
  if (opt_indicators && screen && screen->pixels && ons) {
    Uint32 *px = (Uint32 *)screen->pixels;
    int w = screen->w, h = screen->h;
    int pitch = screen->pitch / 4;
    int by = 4, badge_w = 8, badge_h = 8, gap = 4;

    // Restore saved pixels from previous frame
    static Uint32 saved_px[2 * 8 * 8]; // max 2 badges of 8x8
    static int saved_count = 0;
    static int saved_pos[2][2]; // [x,y] per saved badge
    for (int i = 0; i < saved_count; i++) {
      int sx = saved_pos[i][0], sy = saved_pos[i][1];
      for (int dy = 0; dy < badge_h; dy++)
        for (int dx = 0; dx < badge_w; dx++)
          if (sy+dy >= 0 && sy+dy < h && sx+dx >= 0 && sx+dx < w)
            px[(sy+dy) * pitch + (sx+dx)] = saved_px[i * badge_w * badge_h + dy * badge_w + dx];
    }
    saved_count = 0;

    // Draw badges and save what's underneath
    int bx = w - badge_w - gap;
    // Green badge for auto mode
    if (ons->isAutoMode()) {
      saved_pos[saved_count][0] = bx;
      saved_pos[saved_count][1] = by;
      for (int dy = 0; dy < badge_h; dy++)
        for (int dx = 0; dx < badge_w; dx++)
          if (by+dy >= 0 && by+dy < h && bx+dx >= 0 && bx+dx < w) {
            saved_px[saved_count * badge_w * badge_h + dy * badge_w + dx] = px[(by+dy) * pitch + (bx+dx)];
            px[(by+dy) * pitch + (bx+dx)] = 0xCC22AA22;
          }
      saved_count++;
      bx -= badge_w + gap;
    }
    // Yellow badge for skip mode
    if (ons->isSkipMode()) {
      saved_pos[saved_count][0] = bx;
      saved_pos[saved_count][1] = by;
      for (int dy = 0; dy < badge_h; dy++)
        for (int dx = 0; dx < badge_w; dx++)
          if (by+dy >= 0 && by+dy < h && bx+dx >= 0 && bx+dx < w) {
            saved_px[saved_count * badge_w * badge_h + dy * badge_w + dx] = px[(by+dy) * pitch + (bx+dx)];
            px[(by+dy) * pitch + (bx+dx)] = 0xCCAAAA22;
          }
      saved_count++;
    }
  }

  video_cb(screen->pixels, screen->w, screen->h, screen->pitch);

  // XXX: convert audio format?
  if (spec && SDL_GetAudioStatus() == SDL_AUDIO_PLAYING && frames <= max_frames) {
    SDL_LockAudio();
    memset(stream, 0, frames * 4);
    spec->callback(NULL, (uint8_t *)stream, frames * 4);
    audio_batch_cb(stream, frames);
    SDL_UnlockAudio();
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
  delete ons;
  ons = nullptr;
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
