#include <SDL_libretro.h>
#include <sdl/src/audio/SDL_audiomem.h>
#include <sdl/src/audio/SDL_audio_c.h>
#include <sdl/src/audio/SDL_audiodev_c.h>
#include <sdl/src/audio/dummy/SDL_dummyaudio.h>

#define DUMMYAUD_DRIVER_NAME "dummy"

static int DUMMYAUD_OpenAudio(_THIS, SDL_AudioSpec *spec);
static void DUMMYAUD_WaitAudio(_THIS);
static void DUMMYAUD_PlayAudio(_THIS);
static Uint8 *DUMMYAUD_GetAudioBuf(_THIS);
static void DUMMYAUD_CloseAudio(_THIS);

static int DUMMYAUD_Available(void) { return 1; }

static void DUMMYAUD_DeleteDevice(SDL_AudioDevice *device) {
  SDL_free(device->hidden);
  SDL_free(device);
}

static SDL_AudioDevice *DUMMYAUD_CreateDevice(int devindex) {
  SDL_AudioDevice *this = (SDL_AudioDevice *)SDL_malloc(sizeof(SDL_AudioDevice));
  if (this) {
    SDL_memset(this, 0, sizeof(*this));
    this->hidden = (struct SDL_PrivateAudioData *)SDL_malloc(sizeof(*this->hidden));
  }
  if (!this || !this->hidden) {
    if (this) SDL_free(this);
    SDL_OutOfMemory();
    return 0;
  }
  SDL_memset(this->hidden, 0, sizeof(*this->hidden));
  this->OpenAudio = DUMMYAUD_OpenAudio;
  this->WaitAudio = DUMMYAUD_WaitAudio;
  this->PlayAudio = DUMMYAUD_PlayAudio;
  this->GetAudioBuf = DUMMYAUD_GetAudioBuf;
  this->CloseAudio = DUMMYAUD_CloseAudio;
  this->free = DUMMYAUD_DeleteDevice;
  return this;
}

AudioBootStrap DUMMYAUD_bootstrap = {
  DUMMYAUD_DRIVER_NAME, "SDL libretro audio driver",
  DUMMYAUD_Available, DUMMYAUD_CreateDevice
};

static void DUMMYAUD_WaitAudio(_THIS) {}
static void DUMMYAUD_PlayAudio(_THIS) {}
static Uint8 *DUMMYAUD_GetAudioBuf(_THIS) { return NULL; }
static void DUMMYAUD_CloseAudio(_THIS) {}

static int DUMMYAUD_OpenAudio(_THIS, SDL_AudioSpec *spec) {
  SDL_libretro_audio_spec = spec;
  /* Don't spawn SDL_RunAudio thread — retro_run calls the callback. */
  return 1;
}
