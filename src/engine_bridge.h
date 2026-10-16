// engine_bridge.h — the interaction surface between an engine and its
// execution shell.
//
// This is a deliberate interface, not a bag of globals. An engine is machinery
// that runs a script; the "shell" drives it. Today the shell is the libretro
// core, but the same engines must also be buildable standalone (the krkrsdl2
// and ONScripter trees each have their own main()), so everything the two sides
// exchange lives here and nowhere else.
//
// Shape of the seam
// -----------------
//   engine_cabi.h    the shell -> engine direction: one vtable per engine.
//   engine_bridge.h  this file, the engine -> shell direction: the state and
//                    callbacks an engine reads, plus the operations the shell
//                    provides (unwinding out of exit(), the script clock).
//
// Consequences worth preserving
// -----------------------------
//   - An engine must not include core headers or call the frontend directly;
//     it links against the bridge and the vtable only. That is what keeps it
//     loadable into a different shell (a standalone runner, a test harness, or
//     a second core) without editing engine code.
//   - The bridge is therefore linked into every configuration, including a
//     standalone engine build, where the shell-provided callbacks are simply
//     not installed.
//   - Storage is static for the life of the process, so the address of every
//     object below is stable and an engine may hold it indefinitely (see the
//     lifetime notes in engine_bridge.c for what must NOT be cached across a
//     shell teardown).
//
// Definitions are in engine_bridge.c. Include this to see the declarations.
//
// The core and each engine link this module; where an engine is dlopen'd, the
// core resolves these by name, which is why the exports below are marked
// VNOVEL_EXPORT rather than left to default visibility.

#ifndef ENGINE_BRIDGE_H
#define ENGINE_BRIDGE_H

#include <SDL.h>
#include <libretro.h>
#include <SDL_keysym.h>
#include <setjmp.h>
#include "key_remap.h"
#include "vnovel_visibility.h"

#ifdef __cplusplus
extern "C"
{
#endif

   /* Set once by retro_set_input_state; read by the SDL events glue. */
   VNOVEL_EXPORT extern retro_input_state_t SDL_libretro_input_state_cb;

   /* Set once by retro_set_environment; read by the SDL events glue for options. */
   VNOVEL_EXPORT extern retro_environment_t environ_cb_global;

   /* Core options the glue consumes */
   VNOVEL_EXPORT extern float opt_mouse_sensitivity;

   /* Text-rendering behaviour, published by the core and read by the ONScripter
 * engine. Both default on; the frontend exposes them as core options.
 *
 * vnovel_english_word_wrap: in LATIN (English) mode, never break a line in the
 *   middle of a word — if the next word does not fit on the remaining line,
 *   move the whole word to the next line. Off = classic cell-based wrapping.
 *
 * vnovel_fit_oversize_banners: some English-patch asset cards (chapter-title
 *   bitmaps) were exported far wider than the game's screen (e.g. 1920px on a
 *   640px screen) and the engine draws loaded images at natural size, so the
 *   centre is cropped. When on, a loaded image that is much wider than the
 *   screen but not tall (a banner, not character art) is scaled down to fit,
 *   and the resize is logged. Off = draw at natural size. */
   VNOVEL_EXPORT extern int vnovel_english_word_wrap;
   VNOVEL_EXPORT extern int vnovel_fit_oversize_banners;

   /* vnovel_wrap_total_fit: plan every break in a text segment at once instead of
 *   choosing one word at a time. A segment runs to the next page break, hard
 *   newline, prompt or control-flow command — a clickwait ('@') is a pause
 *   inside it, not an end — so the layout can see the whole line, including
 *   what follows a pause. Off (default) = the greedy per-word wrap, whose
 *   behaviour is unchanged. See ONScripterLabel::planSegment. */
   VNOVEL_EXPORT extern int vnovel_wrap_total_fit;

   /* Last-resort font, published by the core and read by the engine. Points at an
 * embedded TTF (mona.ttf; see resources/fonts/fallback_font.h) held in the
 * core's own static storage, so the address is valid for the process's life.
 * When the game's font is missing or unreadable the engine opens this from
 * memory instead of aborting — a bad game font then costs glyph quality, not
 * the whole run. NULL/0 outside the libretro build (nothing to fall back to). */
   VNOVEL_EXPORT extern const unsigned char *vnovel_fallback_font_data;
   VNOVEL_EXPORT extern int vnovel_fallback_font_len;

   /* Directory of loose files that override game/archive assets, published by the
 * core and read by the engine. The engine prepends it to the archive path
 * search list, and a loose file wins over archive contents, so a corrected
 * asset dropped here (e.g. a right-sized title card) is used instead of the
 * one inside the game's .nsa/.sar. Empty/NULL = no override directory. The
 * core points it at `.overrides/` beside the game data — nscript.dat,
 * pscript.dat, or the KiriKiri archive. */
   VNOVEL_EXPORT extern const char *vnovel_override_dir;

   /* Multiplier (percent, 0..200) applied to the text-window backdrop image's
 * alpha channel, so a backdrop can be made more or less opaque than authored.
 * 100 = unchanged. Only affects windows whose backdrop is an image; colour
 * backdrops do not use the alpha channel. Written by the core, read by the
 * engine. */
   VNOVEL_EXPORT extern int vnovel_textbox_opacity;

   /* The engine's audio spec, published on SDL_OpenAudio and read by the core to
 * size the audio batch. NULL until audio is open. */
   VNOVEL_EXPORT extern SDL_AudioSpec *SDL_libretro_audio_spec;

   /* Unwinds out of engine code that called exit(). The engines call this; the
 * storage it jumps to also lives here. The setjmp that arms it runs on the
 * engine's own thread (setjmp and longjmp must be on one thread), so the engine
 * sets vnovel_script_error_jmp_valid and the core only provides the storage. See
 * engine_bridge.c for the full contract. */
   VNOVEL_EXPORT extern jmp_buf vnovel_script_error_jmp;
   VNOVEL_EXPORT extern volatile int vnovel_script_error_jmp_valid;
   VNOVEL_EXPORT void libretro_script_error_longjmp(void);

   /* --- text plane --------------------------------------------------------
    * Text rendered independently of the frame, so a shell can restyle it,
    * reflow it, or show it as a page instead of accepting the game's own
    * compositing. The design is per-engine capture behind one shared sink:
    * each engine knows where its text goes, the shell sees one interface.
    *
    * Why here rather than in an engine's own presentation header: the engines
    * do not share a rendering path (ONScripter draws sentences into a surface,
    * KiriKiri composites layers), so an engine-specific seam would give the
    * shell three interfaces to implement. This is the same engine-to-shell
    * direction as everything else in this file, so it lives with it.
    *
    * Plain C, no engine types: an engine fills this in and calls the publisher.
    * Same pixel format as the frame (BGRA, padded pitch, top-down) so a shell
    * can composite the two without a conversion pass. */
   typedef struct VNovelTextPlane
   {
      const void *pixels;  /* BGRA, `pitch` bytes per row, `height` rows */
      int width;
      int height;
      int pitch;           /* never assume width * 4 - rows are padded */

      /* Region changed since the previous call, in plane coordinates. Width or
       * height of 0 means "geometry or restart changed, no pixels did". */
      int changed_x, changed_y, changed_w, changed_h;

      /* Where the plane's visible origin sits, in plane coordinates, so a shell
       * that does not scroll can place it where the game intended. */
      int origin_x, origin_y;

      /* Nonzero when the engine began a new page: everything previously drawn
       * on the plane is gone, so a scrolling shell returns to the top. */
      int restart;

      /* Increments once per update. Wraps; compare by subtraction. */
      unsigned long long serial;
   } VNovelTextPlane;

   typedef void (*vnovel_text_plane_fn)(const VNovelTextPlane *plane,
                                        void *userdata);

   /* Installed by the shell before any engine starts. NULL (the default) means
    * no shell is listening, and an engine should then keep drawing text into
    * its frame as it always did - so `vnovel_text_plane_wanted()` is the check
    * an engine makes before doing the work of separating text out. */
   VNOVEL_EXPORT extern vnovel_text_plane_fn vnovel_text_plane_cb;
   VNOVEL_EXPORT extern void *vnovel_text_plane_userdata;

   /* True when a shell is listening, so the caller can skip the separation work
    * entirely rather than filling a struct nobody reads. */
   VNOVEL_EXPORT int vnovel_text_plane_wanted(void);

   /* Publish a text-plane update. A no-op when no shell installed a callback,
    * so a caller that publishes unconditionally is still correct - but check
    * `vnovel_text_plane_wanted()` first if producing the pixels costs work. */
   VNOVEL_EXPORT void vnovel_publish_text_plane(const VNovelTextPlane *plane);

#ifdef __cplusplus
}
#endif

#endif /* ENGINE_BRIDGE_H */
