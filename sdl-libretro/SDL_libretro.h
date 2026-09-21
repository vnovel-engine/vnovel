#ifndef _SDL_LIBRETRO_H
#define _SDL_LIBRETRO_H
#include <SDL.h>
#include <libretro.h>

#ifdef __cplusplus
extern "C" {
#endif

extern SDL_AudioSpec       *SDL_libretro_audio_spec;
extern retro_input_state_t  SDL_libretro_input_state_cb;
extern SDL_sem             *SDL_libretro_event_sem;
void                        SDL_libretro_PumpEvents(void);

/* RetroPad button to ONScripter key mapping (configurable via core options) */
typedef enum {
  ONS_KEY_SPACE = 0,
  ONS_KEY_RETURN,
  ONS_KEY_ESCAPE,
  ONS_KEY_RCTRL,
  ONS_KEY_a,
  ONS_KEY_o,
  ONS_KEY_s,
  ONS_KEY_q,
  ONS_KEY_0,
  ONS_KEY_TAB,
  ONS_KEY_PAGEUP,
  ONS_KEY_PAGEDOWN,
  ONS_KEY_UP,
  ONS_KEY_DOWN,
  ONS_KEY_LEFT,
  ONS_KEY_RIGHT,
  ONS_KEY_NONE,
  ONS_KEY_COUNT
} ons_key_t;

extern ons_key_t SDL_libretro_pad_mapping[16];
int SDL_libretro_key_from_ons_key(ons_key_t k);

/* Which RetroPad button (if any) generates a right-click mouse event */
extern int SDL_libretro_rightclick_button;
enum { RIGHTCLICK_NONE = -1 };

#ifdef __cplusplus
}
#endif
#endif
