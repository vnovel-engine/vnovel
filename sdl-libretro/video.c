#include <SDL_libretro.h>
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
volatile unsigned long long SDL_libretro_frontend_hb = 0;
bool SDL_libretro_fastforward = false;
int SDL_libretro_ff_speed = 50;
Uint32 SDL_libretro_ff_offset = 0;
/* Fast-forward multiplier from the "Fast-Forward Speed" core option
 * (1 / 2 / 4 / 8 / 16 / 32 / 64 for 2x..max). Used to scale timer intervals
 * so text/animation advance at the selected speed instead of always max. */
int SDL_libretro_ff_mult = 8;

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
