// engine_bridge.c — shared state for the core and every engine module.
//
// These symbols are referenced by name from both the core and the engine/glue
// sources, in several translation units that re-declare them locally (12 files
// declare libretro_script_error_longjmp; events.c also declares
// environ_cb_global and opt_mouse_sensitivity). Defining them here lets every
// declaration and use site stay as written, resolved against this module
// instead of whichever side happened to define them.
//
// ONE BRIDGE FILE PER PROCESS. Everything here is a static object, so its
// address never changes and a copy of that address stays valid in any module.
// That only holds while all loaders resolve the same file: two byte-identical
// copies under different names load as two independent instances, and the state
// silently splits — the core then reads values the engine never wrote. So ship
// exactly one bridge binary, and never place a second copy under another name.
//
// Everything is linked into one shared object, so there is one copy of this
// file per process by construction. Keep it that way: two byte-identical copies
// under different names would load as two independent instances and the state
// would silently split. Nothing has to be exported from the core beyond what
// RETRO_API and VNOVEL_EXPORT mark (-fvisibility=hidden hides the rest).
//
// These are plain globals rather than pointers allocated at load because a
// pointer is only valid until its pointee is reallocated, and a stale pointer
// shared across a module boundary does not fault — it silently reads the wrong
// memory. Callers must not cache anything reachable *through* these beyond that
// object's own lifetime; SDL_libretro_audio_spec in particular is owned by the
// engine's SDL and is only valid while audio is open.

#include "engine_bridge.h"
#include <setjmp.h>
#include <stdio.h>

/* frontend callbacks */

/* Set once by retro_set_input_state before any engine module is loaded; the
 * glue then calls it to read RetroPad state. */
VNOVEL_EXPORT retro_input_state_t SDL_libretro_input_state_cb = 0;

/* Set once by retro_set_environment. Exported (not static) because the SDL
 * events glue reads core options through it. */
VNOVEL_EXPORT retro_environment_t environ_cb_global = 0;

/* core options the glue consumes */

VNOVEL_EXPORT float opt_mouse_sensitivity = 1.0;

/* Text-rendering behaviour (see engine_bridge.h). Written by the core from the
 * frontend options, read by the ONScripter engine. */
VNOVEL_EXPORT int vnovel_english_word_wrap    = 1;
VNOVEL_EXPORT int vnovel_fit_oversize_banners = 0;
VNOVEL_EXPORT int vnovel_wrap_total_fit       = 1;

/* Last-resort font and asset-override directory (see engine_bridge.h). Written
 * by the core at start-up, read by the engine. */
VNOVEL_EXPORT const unsigned char *vnovel_fallback_font_data = 0;
VNOVEL_EXPORT int vnovel_fallback_font_len                   = 0;
VNOVEL_EXPORT const char *vnovel_override_dir                = 0;
VNOVEL_EXPORT int vnovel_textbox_opacity                     = 100;

/* User font selection (see engine_bridge.h and src/font_options.h). `font_dir`
 * is the directory the faces are installed in, set by the core from the engine
 * data path; NULL until then, which makes every lookup report "no face", so an
 * engine that runs before the core publishes behaves as it always did. The
 * per-glyph values default to the identity. */
VNOVEL_EXPORT const char *vnovel_font_dir    = 0;
VNOVEL_EXPORT const char *vnovel_font_face_file = 0;
VNOVEL_EXPORT int vnovel_font_size_percent   = 100;
VNOVEL_EXPORT int vnovel_font_tint_r         = 100;
VNOVEL_EXPORT int vnovel_font_tint_g         = 100;
VNOVEL_EXPORT int vnovel_font_tint_b         = 100;

/* Letter- and word-spacing, in per-mille of the em (see font_options.h). 0 is
 * the identity, so an engine that multiplies its em by these gets nothing when
 * the options are at their defaults. */
VNOVEL_EXPORT int vnovel_font_letter_spacing = 0;
VNOVEL_EXPORT int vnovel_font_word_spacing   = 0;

/* Set when the font-size option changes, cleared by an engine that has applied
 * it at a page boundary (see vnovel_font_api.h). Lives here with the other
 * bridge state so the core and the engines share one definition. */
VNOVEL_EXPORT int vnovel_font_scale_refresh_requested = 0;

VNOVEL_EXPORT int vnovel_font_selected_path(char *out, int out_size,
                                            const char **name)
{
   const char *file;
   int n;
   if (name) *name = 0;
   if (out && out_size > 0) out[0] = '\0';
   /* Nothing published, no directory to look in, or the game's own font chosen:
    * all three mean "do nothing", which is also what a run with the frontend
    * options absent gets. */
   if (!out || out_size <= 0 || !vnovel_font_dir || !vnovel_font_dir[0])
      return 0;
   if (!vnovel_font_face_file || !vnovel_font_face_file[0])
      return 0;
   file = vnovel_font_face_file;
   n = snprintf(out, (size_t)out_size, "%s/%s", vnovel_font_dir, file);
   /* Truncated: a path that does not fit is not a path, so report "no face"
    * rather than something an engine would try to open. */
   if (n <= 0 || n >= out_size)
   {
      out[0] = '\0';
      return 0;
   }
   if (name) *name = file;
   return 1;
}

VNOVEL_EXPORT int vnovel_font_scaled_size(int size)
{
   int pct = vnovel_font_size_percent;
   if (pct == 100)
      return size;
   if (pct < VNOVEL_FONT_SCALE_MIN) pct = VNOVEL_FONT_SCALE_MIN;
   if (pct > VNOVEL_FONT_SCALE_MAX) pct = VNOVEL_FONT_SCALE_MAX;
   /* Round to nearest, then floor at 1: a size of 0 makes the engines skip the
    * glyph (ONScripter's drawGlyph returns early), which would silently drop the
    * line rather than show it small. */
   int out = (size * pct + 50) / 100;
   return out < 1 ? 1 : out;
}

VNOVEL_EXPORT unsigned int vnovel_font_tint_rgb(unsigned int rgb)
{
   if (vnovel_font_tint_r == 100 && vnovel_font_tint_g == 100 &&
       vnovel_font_tint_b == 100)
      return rgb;
   int r = (int)((rgb >> 16) & 0xff);
   int g = (int)((rgb >> 8) & 0xff);
   int b = (int)(rgb & 0xff);
   r = r * vnovel_font_tint_r / 100;
   g = g * vnovel_font_tint_g / 100;
   b = b * vnovel_font_tint_b / 100;
   if (r > 255) r = 255;
   if (g > 255) g = 255;
   if (b > 255) b = 255;
   if (r < 0) r = 0;
   if (g < 0) g = 0;
   if (b < 0) b = 0;
   return ((unsigned int)r << 16) | ((unsigned int)g << 8) | (unsigned int)b;
}

VNOVEL_EXPORT int vnovel_font_spacing_px(int per_mille, int em_px)
{
   /* Plain truncating division, on purpose: truncation toward zero is an ODD
    * function, so tightening by the amount a widening added cancels to exactly
    * 0. Flooring toward -infinity would not (it maps +0.5 to 0 and -0.5 to -1),
    * which would leave a sub-pixel drift between the two. The result is whole
    * script pixels, so the spacing quantises to the engine's own unit; the
    * engine's measure and draw both add that same integer, so the plan is
    * unaffected by the quantisation. */
   return (int)(((long)per_mille * (long)em_px) / 1000L);
}

/* text plane (see engine_bridge.h)
 * Installed by the shell before any engine starts. An engine checks
 * vnovel_text_plane_wanted() before separating text from its frame, so with no
 * shell listening the feature costs nothing and behaviour is unchanged. */
VNOVEL_EXPORT vnovel_text_plane_fn vnovel_text_plane_cb   = 0;
VNOVEL_EXPORT void *vnovel_text_plane_userdata            = 0;

VNOVEL_EXPORT int vnovel_text_plane_wanted(void)
{
   return vnovel_text_plane_cb != 0;
}

VNOVEL_EXPORT void vnovel_publish_text_plane(const VNovelTextPlane *plane)
{
   if (!vnovel_text_plane_cb || !plane)
      return;
   vnovel_text_plane_cb(plane, vnovel_text_plane_userdata);
}

/* Filled by the shell when one is listening; the engines call the requester
 * below rather than reaching into the core. */
VNOVEL_EXPORT vnovel_text_plane_dump_fn vnovel_text_plane_dump_cb = 0;
VNOVEL_EXPORT void *vnovel_text_plane_dump_userdata               = 0;

VNOVEL_EXPORT void vnovel_request_text_plane_dump(const char *name)
{
   if (!vnovel_text_plane_dump_cb || !name || !*name)
      return;
   vnovel_text_plane_dump_cb(name, vnovel_text_plane_dump_userdata);
}

/* text runs (see engine_bridge.h)
 * The structured half of the text separation: the characters a draw call
 * carried, where, and in what style. Same install-before-start, no-listener-
 * no-cost discipline as the plane above. */
VNOVEL_EXPORT vnovel_text_run_fn vnovel_text_run_cb = 0;
VNOVEL_EXPORT void *vnovel_text_run_userdata       = 0;

VNOVEL_EXPORT int vnovel_text_run_wanted(void)
{
   return vnovel_text_run_cb != 0;
}

VNOVEL_EXPORT void vnovel_publish_text_run(const VNovelTextRun *run)
{
   if (!vnovel_text_run_cb || !run)
      return;
   vnovel_text_run_cb(run, vnovel_text_run_userdata);
}

/* Filled by an engine whose published surface is a FRAGMENT of the page (only
 * KiriKiri, which publishes one line box at a time). Engines that publish the
 * whole page leave it NULL and have nothing to maintain. */
VNOVEL_EXPORT vnovel_text_plane_splice_fn vnovel_text_plane_splice_cb = 0;

/* The last request of each kind, so a request made BEFORE an engine registered
 * is not lost.
 *
 * This ordering is forced: the core must make these requests before it starts
 * the engine (otherwise the engine draws its opening burst with the flags still
 * off, which is invisible - it just looks like the feature does not work), but
 * the engine only publishes its callbacks DURING that start. So the request is
 * remembered here and replayed when a callback registers; a request that
 * arrives after registration is forwarded immediately. */
static int splice_request_wanted = 0;
static int split_request_wanted  = 0;

VNOVEL_EXPORT void vnovel_request_text_plane_splice(int enabled)
{
   splice_request_wanted = enabled ? 1 : 0;
   if (vnovel_text_plane_splice_cb)
      vnovel_text_plane_splice_cb(splice_request_wanted);
}

/* Called by the seam's setter when an engine installs a callback, so a request
 * already made takes effect against it. (Engines register through
 * vnovel_text_plane_splice_cb / vnovel_split_view_cb; see engine_bridge.h.) */
VNOVEL_EXPORT void vnovel_apply_pending_requests(void)
{
   if (vnovel_text_plane_splice_cb)
      vnovel_text_plane_splice_cb(splice_request_wanted);
   if (vnovel_split_view_cb)
      vnovel_split_view_cb(split_request_wanted);
}

/* Filled by an engine whose frame and text share one composite (only KiriKiri:
 * its text is layers). The two Scripter engines composite text in a refresh
 * pass of their own, so they leave this NULL and need nothing. */
VNOVEL_EXPORT vnovel_split_view_fn vnovel_split_view_cb = 0;

VNOVEL_EXPORT void vnovel_request_split_view(int enabled)
{
   split_request_wanted = enabled ? 1 : 0;
   if (vnovel_split_view_cb)
      vnovel_split_view_cb(split_request_wanted);
}

/* navigation results (engine -> shell)
 *
 * The shell asks an engine to take the Nth choice a page offers; the engine
 * answers here, because only it knows whether that choice exists. See the
 * navigation section of engine_bridge.h for the contract.
 *
 * No remembered value, unlike the requests above: a result describes one
 * request that has already been made, and replaying it to a shell that
 * registered its callback later would report an outcome for something the
 * shell never asked. It is published to whoever is listening at the time. */
VNOVEL_EXPORT vnovel_nav_result_fn vnovel_nav_result_cb = 0;
VNOVEL_EXPORT void *vnovel_nav_result_userdata      = 0;

VNOVEL_EXPORT void vnovel_publish_nav_result(int ordinal, int accepted)
{
   if (vnovel_nav_result_cb)
      vnovel_nav_result_cb(ordinal, accepted);
}

/* The page-event and choice-label sinks. They exist whether or not the wire
 * protocol is compiled in: the shell assigns its own sink unconditionally, so a
 * non-wire build still needs the symbols to assign to (the publishers become
 * inline no-ops in that build -- engine_bridge.h). */
VNOVEL_EXPORT vnovel_page_event_fn vnovel_page_event_cb = 0;
VNOVEL_EXPORT vnovel_choice_label_fn vnovel_choice_label_cb = 0;

/* page events (engine -> shell)
 *
 * The reader's place in the text, as one vocabulary shared with the client
 * (see engine_bridge.h). One-way and edge-only: an event describes something
 * that has just happened, so replaying it to a shell that registered later
 * would claim a page just began when the engine may be pages further on. */
VNOVEL_EXPORT void vnovel_publish_page_event(int event)
{
   if (vnovel_page_event_cb)
      vnovel_page_event_cb(event);
}

/* WHAT a menu's options are, for a menu whose labels are not text: an
 * Onscripter sprite button's label is an image, so a consumer that renders only
 * text shows a page with no options while the engine waits on one. */
VNOVEL_EXPORT void vnovel_publish_choice_label(int ordinal, const char *label)
{
   if (vnovel_choice_label_cb && label)
      vnovel_choice_label_cb(ordinal, label);
}

int remap_keys[REMAP_COUNT] = {
      [REMAP_ADVANCE]      = SDLK_RETURN,
      [REMAP_MENU]         = SDLK_BACKSPACE,
      [REMAP_SKIP]         = SDLK_s,
      [REMAP_AUTO]         = SDLK_a,
      [REMAP_LOOKBACK]     = SDLK_LEFTBRACKET,
      [REMAP_PAGE_FORWARD] = SDLK_RIGHTBRACKET,
      [REMAP_TEXT_SPEED]   = SDLK_0,
      [REMAP_FULLSCREEN]   = SDLK_F11,
};

/* audio */

/* The engine's SDL publishes its spec here when audio opens; the core reads it
 * to size the audio batch and re-emit AV geometry. NULL until audio is open.
 *
 * The address of this variable is stable; the pointer it holds is not, and is
 * expected to change as audio opens and closes. Read it fresh each time. */
VNOVEL_EXPORT SDL_AudioSpec *SDL_libretro_audio_spec = 0;

/* error unwinding */

/* Storage for the setjmp/longjmp pair the engines use to unwind out of exit().
 *
 * The buffer lives here (static storage, one copy per process) but it is the
 * ENGINE that executes both calls, on its own thread:
 *
 *     engine thread:  if (setjmp(vnovel_script_error_jmp)) { clean up; return; }
 *                     ... run the script ...
 *     anywhere:       errorAndExit() -> libretro_script_error_longjmp()
 *
 * setjmp and longjmp must run on the same thread — longjmp restores a stack
 * frame, so jumping into a buffer captured on another thread is undefined. Since
 * the engine owns its thread (see engine_cabi.h), it must own the setjmp, and
 * this module only has to provide the storage both sides can name.
 *
 * The core sets vnovel_script_error_jmp_valid = 0 before the thread starts and may
 * read it afterwards to learn whether the engine unwound; the engine sets it to
 * 1 once its setjmp is armed, and to 0 on the way out. It is written by one
 * thread and read by another, hence volatile. */
VNOVEL_EXPORT jmp_buf vnovel_script_error_jmp;
VNOVEL_EXPORT volatile int vnovel_script_error_jmp_valid = 0;

VNOVEL_EXPORT void libretro_script_error_longjmp(void)
{
   if (vnovel_script_error_jmp_valid)
      longjmp(vnovel_script_error_jmp, 1);
}
