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
extern SDL_sem             *SDL_libretro_wait_sem;  /* engine blocks here after flush; retro_run posts */
extern SDL_sem             *SDL_libretro_rendered_sem;
extern Uint8               *SDL_libretro_audio_buf;
extern int                   SDL_libretro_audio_buf_size;
int SDL_libretro_get_audio(int16_t *out, int max_frames);
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

/* When true, the engine thread should block (paused by frontend) */
extern bool SDL_libretro_paused;
/* Monotonic ms of the last retro_run() call. The engine thread treats a stale
 * value as "frontend is not driving us" (menu open / paused) and blocks. */
extern volatile unsigned long long SDL_libretro_frontend_hb;
/* Get current mouse position (tracked by PumpMouseEvents) */
void SDL_libretro_get_mouse_pos(int *x, int *y);
/* When true, fast-forward is active — timers fire immediately */
extern bool SDL_libretro_fastforward;
/* Fast-forward time acceleration (ms added per SDL_GetTicks call) */
extern int SDL_libretro_ff_speed;
extern int SDL_libretro_ff_mult;
/* Fast-forward time offset (accumulated once per frame in retro_run) */
extern Uint32 SDL_libretro_ff_offset;

#ifdef __cplusplus
}
#endif
#endif
