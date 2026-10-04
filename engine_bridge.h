// engine_bridge.h — the shared state the core and every engine module link.
//
// Definitions are in engine_bridge.c. Include this to see the declarations.
//
// The core and each engine module link this module; nothing here is resolved
// from the other side, so neither needs to export symbols for the other. The
// storage is static for the life of the process, so the address of every object
// below is stable and a module may hold it indefinitely (see the lifetime notes
// in engine_bridge.c for what must NOT be cached).

#ifndef ENGINE_BRIDGE_H
#define ENGINE_BRIDGE_H

#include <SDL.h>
#include <libretro.h>
#include <SDL_keysym.h>
#include <setjmp.h>
#include "key_remap.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Set once by retro_set_input_state; read by the SDL events glue. */
extern retro_input_state_t  SDL_libretro_input_state_cb;

/* Set once by retro_set_environment; read by the SDL events glue for options. */
extern retro_environment_t  environ_cb_global;

/* Core options: mouse sensitivity scale. The key remap table is declared by
 * key_remap.h (C++ linkage, matching its definition in engine_bridge.c via the
 * same header) — do not redeclare it here. */
extern float opt_mouse_sensitivity;

/* The engine's audio spec, published on SDL_OpenAudio and read by the core to
 * size the audio batch. NULL until audio is open. */
extern SDL_AudioSpec *SDL_libretro_audio_spec;

/* Unwinds out of engine code that called exit(). The engines call this; the
 * storage it jumps to also lives here. The setjmp that arms it runs on the
 * engine's own thread (setjmp and longjmp must be on one thread), so the engine
 * sets ons_script_error_jmp_valid and the core only provides the storage. See
 * engine_bridge.c for the full contract. */
extern jmp_buf ons_script_error_jmp;
extern volatile int ons_script_error_jmp_valid;
void libretro_script_error_longjmp(void);

#ifdef __cplusplus
}
#endif

#endif /* ENGINE_BRIDGE_H */
