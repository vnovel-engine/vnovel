#include <SDL_libretro.h>
#include <time.h>
#include <sdl/src/video/SDL_pixels_c.h>
#include <sdl/src/video/dummy/SDL_nullvideo.h>
#include <sdl/src/video/dummy/SDL_nullevents_c.h>
#include <sdl/src/video/dummy/SDL_nullmouse_c.h>


#define DUMMYVID_DRIVER_NAME "dummy"

/* Initialization/Query functions */
static int DUMMY_VideoInit(_THIS, SDL_PixelFormat *vformat);
static SDL_Rect **DUMMY_ListModes(_THIS, SDL_PixelFormat *format, Uint32 flags);
static SDL_Surface *DUMMY_SetVideoMode(_THIS, SDL_Surface *current, int width, int height, int bpp, Uint32 flags);
static int DUMMY_SetColors(_THIS, int firstcolor, int ncolors, SDL_Color *colors);
static void DUMMY_VideoQuit(_THIS);

/* Hardware surface functions */
static int DUMMY_AllocHWSurface(_THIS, SDL_Surface *surface);
static int DUMMY_LockHWSurface(_THIS, SDL_Surface *surface);
static void DUMMY_UnlockHWSurface(_THIS, SDL_Surface *surface);
static void DUMMY_FreeHWSurface(_THIS, SDL_Surface *surface);

/* etc. */
static void DUMMY_UpdateRects(_THIS, int numrects, SDL_Rect *rects);

/* DUMMY driver bootstrap functions */

static int DUMMY_Available(void)
{
  return 1;
}

static void DUMMY_DeleteDevice(SDL_VideoDevice *device)
{
  SDL_free(device->hidden);
  SDL_free(device);
}

static SDL_VideoDevice *DUMMY_CreateDevice(int devindex)
{
  SDL_VideoDevice *device;

  /* Initialize all variables that we clean on shutdown */
  device = (SDL_VideoDevice *)SDL_malloc(sizeof(SDL_VideoDevice));
  if (device) {
    SDL_memset(device, 0, (sizeof *device));
    device->hidden = (struct SDL_PrivateVideoData *)
      SDL_malloc(sizeof(*device->hidden));
  }
  if (device == NULL || device->hidden == NULL) {
    SDL_OutOfMemory();
    if (device) {
      SDL_free(device);
    }
    return 0;
  }
  SDL_memset(device->hidden, 0, sizeof(*device->hidden));

  /* Set the function pointers */
  device->VideoInit = DUMMY_VideoInit;
  device->ListModes = DUMMY_ListModes;
  device->SetVideoMode = DUMMY_SetVideoMode;
  device->CreateYUVOverlay = NULL;
  device->SetColors = DUMMY_SetColors;
  device->UpdateRects = DUMMY_UpdateRects;
  device->VideoQuit = DUMMY_VideoQuit;
  device->AllocHWSurface = DUMMY_AllocHWSurface;
  device->CheckHWBlit = NULL;
  device->FillHWRect = NULL;
  device->SetHWColorKey = NULL;
  device->SetHWAlpha = NULL;
  device->LockHWSurface = DUMMY_LockHWSurface;
  device->UnlockHWSurface = DUMMY_UnlockHWSurface;
  device->FlipHWSurface = NULL;
  device->FreeHWSurface = DUMMY_FreeHWSurface;
  device->SetCaption = NULL;
  device->SetIcon = NULL;
  device->IconifyWindow = NULL;
  device->GrabInput = NULL;
  device->GetWMInfo = NULL;
  device->InitOSKeymap = DUMMY_InitOSKeymap;
  device->PumpEvents = DUMMY_PumpEvents;

  device->free = DUMMY_DeleteDevice;

  return device;
}

VideoBootStrap DUMMY_bootstrap = {
  DUMMYVID_DRIVER_NAME, "SDL dummy video driver",
  DUMMY_Available, DUMMY_CreateDevice
};


int DUMMY_VideoInit(_THIS, SDL_PixelFormat *vformat)
{
  /*
    fprintf(stderr, "WARNING: You are using the SDL dummy video driver!\n");
  */

  /* Determine the screen depth (use default 8-bit depth) */
  /* we change this during the SDL_SetVideoMode implementation... */
  vformat->BitsPerPixel = 8;
  vformat->BytesPerPixel = 1;

  /* We're done! */
  return 0;
}

SDL_Rect **DUMMY_ListModes(_THIS, SDL_PixelFormat *format, Uint32 flags)
{
  return (SDL_Rect **) -1;
}

SDL_Surface *DUMMY_SetVideoMode(_THIS, SDL_Surface *current,
				int width, int height, int bpp, Uint32 flags)
{
  if (this->hidden->buffer) {
    SDL_free(this->hidden->buffer);
  }

  this->hidden->buffer = SDL_malloc(width * height * (bpp / 8));
  if (!this->hidden->buffer) {
    SDL_SetError("Couldn't allocate buffer for requested mode");
    return(NULL);
  }

  /* 	printf("Setting mode %dx%d\n", width, height); */

  SDL_memset(this->hidden->buffer, 0, width * height * (bpp / 8));

  /* Set up the new mode framebuffer */
  current->flags = flags & SDL_FULLSCREEN;
  this->hidden->w = current->w = width;
  this->hidden->h = current->h = height;
  current->pitch = current->w * (bpp / 8);
  current->pixels = this->hidden->buffer;

  /* Use standard XRGB8888 format matching what RetroArch expects */
  if (bpp == 32) {
    SDL_ReallocFormat(current, bpp, 0x00ff0000, 0x0000ff00, 0x000000ff, 0);
  } else {
    SDL_ReallocFormat(current, bpp, 0, 0, 0, 0);
  }
  return current;
}

/* We don't actually allow hardware surfaces other than the main one */
static int DUMMY_AllocHWSurface(_THIS, SDL_Surface *surface)
{
  return -1;
}
static void DUMMY_FreeHWSurface(_THIS, SDL_Surface *surface)
{
  return;
}

/* We need to wait for vertical retrace on page flipped displays */
static int DUMMY_LockHWSurface(_THIS, SDL_Surface *surface)
{
  return 0;
}

static void DUMMY_UnlockHWSurface(_THIS, SDL_Surface *surface)
{
  return;
}

/* Frame sync semaphore: when LIBRETRO_FRAME_SYNC is enabled, the engine
   blocks here after each screen update, waiting for retro_run() to post.
   This gives 1:1 frame synchronization — every flush() is displayed. */
SDL_sem *SDL_libretro_wait_sem = NULL;
SDL_sem *SDL_libretro_rendered_sem = NULL;
bool SDL_libretro_paused = false;
/* Set for the duration of a core reset (retro_reset .. engine restarted). While
 * set, the engine thread must NOT honour the pause gate: a reset has to reach
 * the engine so it can acknowledge the quit request, and while a reset runs the
 * frontend is not calling retro_run(), so the heartbeat is stale and the engine
 * would otherwise sit blocked in SDL_Delay's pause loop forever (deadlock on
 * reset-while-paused). */
bool SDL_libretro_in_reset = false;
/* Frontend heartbeat, milliseconds on CLOCK_MONOTONIC: retro_run() stamps it
 * every frame. The engine thread treats a stale value as "the frontend stopped
 * feeding us" (its menu is open, the core is paused) and idles — see the
 * SDL_Delay path in SDL_systimer.c. This is how core pause is signalled at all,
 * since RetroArch calls no core-visible pause callback and simply stops calling
 * retro_run. 0 = never stamped. */
volatile unsigned long long SDL_libretro_frontend_hb = 0;
bool SDL_libretro_fastforward = false;

/* ---- The single game clock ------------------------------------------------
 * Engines must not each invent their own fast-forward time dilation: that is
 * how ONS and KiriKiri drifted (different rate definitions, one frame-count
 * based and one wall-time based). Instead the core owns one clock and every
 * engine reads it — ONS through SDL_GetTicks() (same .so), KiriKiri through a
 * callback installed with krkr_set_clock() (separate .so, cdecl function
 * pointer is safe across the dlopen boundary).
 *
 * Definition: real monotonic time, plus a bias that grows at (scale-1)x while
 * fast-forward is on, derived from an epoch so it never depends on frame rate
 * or per-iteration rounding. On release the bias freezes, so the clock stays
 * monotonic and deltas remain correct.
 *
 * The core drives the state with ons_libretro_clock_set()/_reset(); scale 1
 * means no acceleration. */
static long long clock_bias_ms   = 0;   /* accumulated virtual ms */
static double    clock_scale     = 1.0; /* current fast-forward multiplier */
static long long clock_epoch_ms  = -1;  /* FF-on epoch (monotonic), -1 = off */
static long long clock_epoch_bias= 0;   /* bias captured at FF-on */
static long long clock_base_ms   = 0;   /* offset subtracted so the first
                                         * returned value starts near 0 */
static long long pause_started_ms= -1;  /* wall ms when frontend paused, -1=off */

static long long mono_ms(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

long long ons_libretro_game_clock_ms(void)
{
  /* While the frontend is paused (RetroArch menu / window unfocused) the core
   * stops being driven, but the engine thread keeps running and reading this
   * clock. If the clock kept advancing on wall time, every timer in the engine
   * would see a huge elapsed delta the moment the frontend resumes — the engine
   * then spends many frames "catching up" (sluggish render) until the backlog
   * drains (which is what a fast-forward burst appears to fix). Freeze the
   * clock at the pause instant so no time accrues while paused; resume shifts
   * the base by the paused span so the clock stays continuous, not monotonic
   * wall time. */
  long long now = (pause_started_ms >= 0) ? pause_started_ms : mono_ms();
  if (clock_scale > 1.0)                          /* fast-forward active */
  {
    if (clock_epoch_ms < 0) { clock_epoch_ms = now; clock_epoch_bias = clock_bias_ms; }
    clock_bias_ms = clock_epoch_bias
                  + (long long)((now - clock_epoch_ms) * (clock_scale - 1.0));
  }
  else
  {
    clock_epoch_ms = -1;                          /* freeze the bias */
  }
  if (clock_base_ms == 0) clock_base_ms = now;    /* first call: start at 0 */
  return (now - clock_base_ms) + clock_bias_ms;
}

/* Freeze the game clock while the frontend pauses the core. */
void ons_libretro_clock_pause(void)
{
  if (pause_started_ms < 0) pause_started_ms = mono_ms();
}

/* Resume: shift the base past the paused span so the clock continues from
 * where it stopped instead of jumping by the pause duration. */
void ons_libretro_clock_resume(void)
{
  if (pause_started_ms < 0) return;
  long long paused = mono_ms() - pause_started_ms;
  if (clock_base_ms != 0) clock_base_ms += paused;
  pause_started_ms = -1;
  if (getenv("ONS_CLOCK_DEBUG"))
    fprintf(stderr, "[clock] resume after %lldms pause\n", paused);
}

void ons_libretro_clock_set(double scale)
{
  clock_scale = (scale > 1.0) ? scale : 1.0;
  if (getenv("ONS_CLOCK_DEBUG"))
    fprintf(stderr, "[clock] set scale=%.3f (bias=%lld epoch=%lld)\n",
            clock_scale, clock_bias_ms, clock_epoch_ms);
}

/* Restart the clock (e.g. on core reset). A reset begins a new run: the wall
 * clock rebases to ~0 and any fast-forward bias is dropped, so the restarted
 * engine reads a fresh timeline instead of the previous run's elapsed time.
 * The scale is left to the core's FF state (retro_run re-sets it on change). */
void ons_libretro_clock_reset(void)
{
  clock_epoch_ms   = -1;
  clock_epoch_bias = 0;
  clock_bias_ms    = 0;
  clock_base_ms    = 0;   /* nonzero check in game_clock_ms rebases on next call */
  if (getenv("ONS_CLOCK_DEBUG")) fprintf(stderr, "[clock] reset\n");
}

static void DUMMY_UpdateRects(_THIS, int numrects, SDL_Rect *rects)
{
  /* No-op. Engine renders freely to the framebuffer.
   * retro_run() displays whatever is current each frame. */
}

int DUMMY_SetColors(_THIS, int firstcolor, int ncolors, SDL_Color *colors)
{
  /* do nothing of note. */
  return 1;
}

/* Note:  If we are terminated, this could be called in the middle of
   another SDL video routine -- notably UpdateRects.
*/
void DUMMY_VideoQuit(_THIS)
{
  if (this->screen->pixels != NULL) {
      SDL_free(this->screen->pixels);
      this->screen->pixels = NULL;
  }
}
