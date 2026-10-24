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

      /* Where the SURFACE (pixels/width/height) sits in the window, in window
       * pixels: read-only context, engine-set, never host-set. Valid only while
       * the splice flag below is on; 0/0 otherwise.
       *
       * This is what makes a page reconstructible on every engine: a shell that
       * accumulates text blits each publication's changed rect out of the
       * surface to (plane_x + changed_x, plane_y + changed_y). Engines that
       * publish a whole page set 0/0; KiriKiri publishes one line box at a time
       * and sets the box's window position, so the fragments line up. */
      int plane_x, plane_y;

      /* The size of the WINDOW this surface lives in, in pixels. 0 until the
       * engine has composited a frame.
       *
       * Sent with every publication so a shell can size its page without
       * waiting for a frame of its own: an engine publishes text during
       * start-up, before the shell has presented anything, and a shell that
       * learned the size on its own thread dropped all of those fragments (the
       * start of the page went missing). Reported here it is always available
       * alongside the fragment it describes. */
      int window_w, window_h;

      /* Nonzero when the engine was asked for the placement context above (see
       * the splice toggle on the engine side). A shell sets this once at
       * startup when it intends to accumulate. Zero means the engine is not
       * maintaining any of it and plane_x/plane_y are meaningless. */
      int splice;

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

   /* Ask the shell to write its copy of the plane to <save dir><name>.bmp, the
    * plane's equivalent of the engines' `snapshotsave` (which dumps the frame,
    * not the core's plane). An engine command handler calls this; it is a no-op
    * when no shell is listening, nothing has been captured, or `name` is empty.
    * `name` is consumed during the call, so the caller need not keep it alive. */
   typedef void (*vnovel_text_plane_dump_fn)(const char *name, void *userdata);
   VNOVEL_EXPORT extern vnovel_text_plane_dump_fn vnovel_text_plane_dump_cb;
   VNOVEL_EXPORT extern void *vnovel_text_plane_dump_userdata;

   VNOVEL_EXPORT void vnovel_request_text_plane_dump(const char *name);

   /* Ask the engines to maintain the text plane's PLACEMENT context (plane_x/
    * plane_y in VNovelTextPlane) as well as its pixels. A shell calls this once
    * at startup if it intends to accumulate the published fragments into a
    * page; without it the engines skip the per-publication work and report 0/0.
    *
    * A no-op on an engine with nothing to do (ONScripter/PONScripter publish a
    * whole page, so their placement is 0/0 by definition). */
   typedef void (*vnovel_text_plane_splice_fn)(int enabled);
   VNOVEL_EXPORT extern vnovel_text_plane_splice_fn
         vnovel_text_plane_splice_cb;

   VNOVEL_EXPORT void vnovel_request_text_plane_splice(int enabled);

   /* --- Text runs: the STRUCTURED half of the text separation -------------
    *
    * VNovelTextPlane above is pixels - a picture of the text, which a shell can
    * relocate but not restyle or reflow. This is the text the engine was asked
    * to draw, before it became pixels: the characters, the position, and the
    * style of one draw call.
    *
    * They are separate streams because they are different things, produced at
    * different points: the plane is per-bitmap (whatever surface glyphs landed
    * on), a run is per CALL. Only at the call does the engine still know which
    * characters produced which ink; nothing downstream can recover it. A shell
    * may want either without the other.
    *
    * KiriKiri/KAG shows why it is worth having: KAG draws one character per
    * call into a per-line bitmap, so runs ARE the script's text with layout
    * resolved, while the plane is a line box a shell must accumulate and cannot
    * read as text at all.
    *
    * What a run is NOT: not a line, not a page, not an utterance. Those are
    * groupings a shell builds from order and position; the engine has no such
    * notion to report. Nor is there a "this run is the shadow of that one" -
    * a call carries its own shadow parameters and both are rendered from them.
    *
    * A shell that wants only a proportion of the work pays only for that: with
    * no callback installed an engine skips producing runs (it does not even
    * resolve the window position), and the pixels-only plane path is unchanged.
    */
   typedef struct VNovelTextRun
   {
      /* The characters this call carried, as UTF-16 code units. NOT
       * NUL-terminated in general, and NOT valid after the callback returns
       * (the engine may reuse the string): copy what you keep, and use
       * text_length rather than scanning for a terminator.
       *
       * Usually one character on KiriKiri/KAG, but not always - a ruby run or a
       * pre-composed string arrives whole - so do not assume length 1. */
      const unsigned short *text;
      int text_length;

      /* Where the call was told to draw, in LAYER-LOCAL pixels. Kept as given
       * rather than normalised: its meaning is the engine's own text metric (a
       * KAG glyph's y is the glyph's top, not a baseline), and inventing a
       * convention here would be a guess a shell cannot check.
       *
       * There is deliberately NO window-resolved position. This stream exists
       * so a shell can RE-LAY-OUT the text (reflow, totalfit, Knuth-Plass), and
       * a shell that does that derives its own placement; the engine's, already
       * resolved through the layer tree, is at best a hint. Reporting it would
       * also force a parent-chain walk per glyph - the only part of this path
       * that costs anything. A shell that wants "where did the engine put it"
       * has the plane, which is that answer already rendered. */
      int x, y;

      /* Style, as given to the call. Color is 0xRRGGBB - the engine's alpha
       * rides in opacity, not here. shadow_level 0 means no shadow and the
       * shadow fields are then meaningless. */
      unsigned int color;
      int opacity;
      int antialias;
      int shadow_level;
      unsigned int shadow_color;
      int shadow_offset_x, shadow_offset_y;

      /* Which producer this run came from. The two are different things and a
       * consumer must not confuse them:
       *
       *   VNOVEL_RUN_DRAW  — the engine DREW these characters. Position and
       *                      style below are meaningful, and the run arrives
       *                      when the engine renders it, so it is paced by the
       *                      game's own typewriter speed.
       *   VNOVEL_RUN_PARSE — a look-ahead parser READ these characters without
       *                      drawing them. They arrive as fast as the scenario
       *                      can be parsed, which is the whole point: the text
       *                      of a page is known before the engine has drawn any
       *                      of it. Position and style are meaningless (the
       *                      parser has no layer) and are 0.
       *
       * A consumer that wants "the page's text, now" filters for PARSE; one that
       * wants "what the engine is actually showing" filters for DRAW. Neither
       * is a superset of the other — during a page the same text arrives on
       * both — so a consumer must pick one rather than merging them. */
      int kind;

      /* Increments once per run. Wraps; compare by subtraction. */
      unsigned long long serial;
   } VNovelTextRun;

   /* Which producer a VNovelTextRun came from. See the `kind` field. */
#define VNOVEL_RUN_DRAW  0
#define VNOVEL_RUN_PARSE 1

   typedef void (*vnovel_text_run_fn)(const VNovelTextRun *run, void *userdata);

   /* Installed by the shell before any engine starts. NULL (the default) means
    * no shell is listening and an engine should skip producing runs entirely. */
   VNOVEL_EXPORT extern vnovel_text_run_fn vnovel_text_run_cb;
   VNOVEL_EXPORT extern void *vnovel_text_run_userdata;

   /* True when a shell is listening for runs, so a caller can skip the work
    * (including resolving window coordinates) rather than filling a struct
    * nobody reads. */
   VNOVEL_EXPORT int vnovel_text_run_wanted(void);

   /* Publish one run. A no-op when no shell installed a callback, so a caller
    * that publishes unconditionally is still correct. */
   VNOVEL_EXPORT void vnovel_publish_text_run(const VNovelTextRun *run);

   /* Ask the engines to compose their frame from ARTWORK ONLY, leaving out any
    * layer that has received text. A shell shows the text from its own surface
    * instead (the text plane), so the two arrive separately and can be placed
    * or scaled independently - which is what makes a split/large-canvas view
    * possible without masking or subtraction.
    *
    * A no-op on an engine that already separates the two (ONScripter/PONScripter
    * composite text in a refresh pass, not a layer), so this is only meaningful
    * where the frame and the text genuinely share one composite. */
   typedef void (*vnovel_split_view_fn)(int enabled);
   VNOVEL_EXPORT extern vnovel_split_view_fn vnovel_split_view_cb;

   VNOVEL_EXPORT void vnovel_request_split_view(int enabled);

   /* --- navigation results: the shell <- engine half of "select link N" ----
    *
    * The shell can ask the engine to take the Nth choice a page offers (the
    * development socket's inbound direction; see devsock.h). The engine is the
    * only side that knows whether that choice EXISTS - it holds the live button
    * set, and the published page text names `<<choice>>` without saying how many
    * labels follow it - so the verdict comes back through here rather than being
    * guessed by the shell.
    *
    * `ordinal` is 1-based and is the same number a CLICK on that label produces,
    * which is also what `selnum` reports (minus one): the index of the choice in
    * the page's own list. `accepted` is nonzero when the engine took it, zero
    * when nothing was pending or no such choice was offered.
    *
    * Called from the engine's own script thread (the engine resolves the
    * request there, because that is where the button set lives), so an
    * implementation must be safe to call from a thread other than the shell's. */
   typedef void (*vnovel_nav_result_fn)(int ordinal, int accepted);
   VNOVEL_EXPORT extern vnovel_nav_result_fn vnovel_nav_result_cb;
   VNOVEL_EXPORT extern void *vnovel_nav_result_userdata;

   /* Report the outcome of a navigation request. A no-op when no shell is
    * listening, so an engine that reports unconditionally is still correct. */
   VNOVEL_EXPORT void vnovel_publish_nav_result(int ordinal, int accepted);

   /* --- choice labels: WHAT the choices are, not just that there are some ---
    *
    * A pause says the reader must act; it does not say what they are choosing
    * between. Usually the page's own text carries the labels, so a consumer that
    * renders the page already shows them. But a menu built from SPRITES (PON's
    * `spbtn`, the title screen's Start/Replay/System/Exit, a scenario select)
    * has no text anywhere: the label IS an image, and a consumer that only reads
    * text sees a page with no options at all while the engine waits on one.
    *
    * So the engine publishes the label as its own record when it arms a button
    * wait. `label` is UTF-8 and is a DESCRIPTION, not the pixels: for an
    * Onscripter sprite button it is the sprite's own filename (which is all the
    * engine retains -- `title\Start.bmp` -- and all a consumer needs to name the
    * option), for a text button it is the button's text.
    *
    * Published AFTER the pause edge for the same wait, once per label, in the
    * engine's own button order, so a consumer collects them until the next
    * record that is not one. `ordinal` is 1-based and is the number `select`
    * takes: the same one a click on that label produces. */
   typedef void (*vnovel_choice_label_fn)(int ordinal, const char *label);
   VNOVEL_EXPORT extern vnovel_choice_label_fn vnovel_choice_label_cb;

   /* The publisher is declared with the rest of the wire-protocol surface, at
    * the end of this file, so that a non-DEVWIRE build's inline no-op and this
    * declaration cannot both be visible (that was a compile error: a static
    * declaration after a non-static one). */

   /* --- the page-event vocabulary: ONE list, shared with the client --------
 *
 * What happens to the READER's place in the text, as five events. They are the
 * same vocabulary the client sees, under the same names (devsock.h spells each
 * as a wire token), so the engine side and the client side are one protocol
 * rather than two that have to be kept in step -- and the client's own tokens
 * (PAGE, run, CHOICE) are a SUPERSET of these, which is what makes the client
 * protocol the whole picture and this ABI its event half.
 *
 * The shape this replaces had two separate enums, and they collided:
 * VNOVEL_FLOW_LP and VNOVEL_EDGE_START were BOTH 0, so passing one where the
 * other belonged compiled, linked, and published a plausible wrong event. One
 * list with distinct values cannot do that.
 *
 * A boundary and a wait are different events, and a script may have one without
 * the other: a game that flows from page into page pauses nowhere and still
 * begins and ends pages. So START/END describe the page's LIMITS and the three
 * pauses describe the reader being BLOCKED (or released).
 */
#define VNOVEL_EV_PAGE_START  0   /* a page began            -> `<<<ps>>>` */
#define VNOVEL_EV_PAGE_END    1   /* a page ended            -> `<<<pe>>>` */
#define VNOVEL_EV_LINE_PAUSE  2   /* an in-page wait         -> `<<<lp>>>` */
#define VNOVEL_EV_PAGE_PAUSE  3   /* a wait at a page's end  -> `<<<pp>>>` */
#define VNOVEL_EV_RESUME      4   /* the reader acted        -> `<go>`     */

   /* The publisher is declared with the rest of the wire-protocol surface at the
    * end of this file, so that a non-DEVWIRE build's inline no-op and a real
    * declaration cannot both be visible (that was a compile error).
    *
    * The shell's sink is registered through this, either way. */
   typedef void (*vnovel_page_event_fn)(int event);
   VNOVEL_EXPORT extern vnovel_page_event_fn vnovel_page_event_cb;

/* --- WHAT a menu's options are, for a menu with no text labels -----------
 *
 * A pause says the reader must act; it does not say what they are choosing
 * between. Usually the page's text carries the labels, so a consumer that
 * renders the page shows them. But a menu built from SPRITES (the title screen,
 * a scenario select) has no text anywhere: the option IS an image.
 *
 * `label` is UTF-8 and is a DESCRIPTION, not pixels: for an Onscripter sprite
 * button it is the sprite's own name (all the engine retains, and all a consumer
 * needs to name the option), for a text button its text. `ordinal` is 1-based
 * and is the number `select` takes -- the same one a click on that label makes.
 *
 * Published after the pause that arms the menu, one per choice in the engine's
 * own order. */
   typedef void (*vnovel_choice_label_fn)(int ordinal, const char *label);
   VNOVEL_EXPORT extern vnovel_choice_label_fn vnovel_choice_label_cb;

/* --- the wire protocol, compiled out ------------------------------------
 *
 * The PUBLISHERS are development surface: they feed devsock.c and its records,
 * which the Makefile compiles in only for `make DEVWIRE=1`. Without VNOVEL_DEVWIRE
 * they are INLINE NO-OPS rather than declarations.
 *
 * Here and not at the call sites on purpose: the engines publish unconditionally,
 * and a caller that must remember an #ifdef will one day forget one -- linking
 * against a socket the build does not have, or silently dropping an event it
 * meant to send. An inline no-op makes the call vanish at compile time and
 * cannot be forgotten.
 *
 * The event constants and the callback globals stay unconditional: they are the
 * bridge's own vocabulary, and the shell defines its sink whether or not the
 * wire protocol is compiled in. */
#define VNOVEL_BRIDGE_SUPPLIES_FLOW 1
#ifndef VNOVEL_DEVWIRE
   static inline void vnovel_publish_page_event(int event) { (void)event; }
   static inline void vnovel_publish_choice_label(int o, const char *l)
   { (void)o; (void)l; }
#else
   VNOVEL_EXPORT void vnovel_publish_page_event(int event);
   VNOVEL_EXPORT void vnovel_publish_choice_label(int ordinal, const char *label);
#endif /* VNOVEL_DEVWIRE */
   /* Apply any request made before this engine registered its callbacks.
    *
    * The core must request these BEFORE it starts an engine (an engine draws
    * its opening burst during start-up, and a request made afterwards arrives
    * too late - measured: 6 publications with splice=0 before the first
    * retro_run, all of them, so the feature looks broken rather than late). But
    * the engine only publishes its callbacks DURING that start. So an engine
    * calls this immediately after it fills in vnovel_text_plane_splice_cb /
    * vnovel_split_view_cb, and any request already made takes effect against
    * it. A request made later is forwarded immediately as before. */
   VNOVEL_EXPORT void vnovel_apply_pending_requests(void);

#ifdef __cplusplus
}
#endif

#endif /* ENGINE_BRIDGE_H */
