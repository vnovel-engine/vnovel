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

/* Core options the glue consumes */
extern float opt_mouse_sensitivity;

/* Text-rendering behaviour, published by the core and read by the ONScripter
 * engine. Both default on; the frontend exposes them as core options.
 *
 * ons_english_word_wrap: in LATIN (English) mode, never break a line in the
 *   middle of a word — if the next word does not fit on the remaining line,
 *   move the whole word to the next line. Off = classic cell-based wrapping.
 *
 * ons_fit_oversize_banners: some English-patch asset cards (chapter-title
 *   bitmaps) were exported far wider than the game's screen (e.g. 1920px on a
 *   640px screen) and the engine draws loaded images at natural size, so the
 *   centre is cropped. When on, a loaded image that is much wider than the
 *   screen but not tall (a banner, not character art) is scaled down to fit,
 *   and the resize is logged. Off = draw at natural size. */
extern int ons_english_word_wrap;
extern int ons_fit_oversize_banners;

/* Last-resort font, published by the core and read by the engine. Points at an
 * embedded TTF (mona.ttf; see resources/fonts/fallback_font.h) held in the
 * core's own static storage, so the address is valid for the process's life.
 * When the game's font is missing or unreadable the engine opens this from
 * memory instead of aborting — a bad game font then costs glyph quality, not
 * the whole run. NULL/0 outside the libretro build (nothing to fall back to). */
extern const unsigned char *ons_fallback_font_data;
extern int ons_fallback_font_len;

/* Directory of loose files that override game/archive assets, published by the
 * core and read by the engine. The engine prepends it to the archive path
 * search list, and a loose file wins over archive contents, so a corrected
 * asset dropped here (e.g. a right-sized title card) is used instead of the
 * one inside the game's .nsa/.sar. Empty/NULL = no override directory. The
 * core points it at `.overrides/` beside the game data — nscript.dat,
 * pscript.dat, or the KiriKiri archive. */
extern const char *ons_override_dir;

/* Multiplier (percent, 0..200) applied to the text-window backdrop image's
 * alpha channel, so a backdrop can be made more or less opaque than authored.
 * 100 = unchanged. Only affects windows whose backdrop is an image; colour
 * backdrops do not use the alpha channel. Written by the core, read by the
 * engine. */
extern int ons_textbox_opacity;

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
