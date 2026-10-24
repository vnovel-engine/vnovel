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
// On ELF the modules link -lvnovel_runtime, which contains this file. On
// Windows the loader reuses the already-loaded DLL when the import library
// names it, so the same single instance is shared. Either way nothing has to be
// exported from the core (-fvisibility=hidden hides everything but what
// RETRO_API and VNOVEL_EXPORT mark).
//
// These are plain globals rather than pointers allocated at load because a
// pointer is only valid until its pointee is reallocated, and a stale pointer
// shared across a module boundary does not fault — it silently reads the wrong
// memory. Callers must not cache anything reachable *through* these beyond that
// object's own lifetime; SDL_libretro_audio_spec in particular is owned by the
// engine's SDL and is only valid while audio is open.

#include "engine_bridge.h"
#include <setjmp.h>

/* --- frontend callbacks ---------------------------------------------------- */

/* Set once by retro_set_input_state before any engine module is loaded; the
 * glue then calls it to read RetroPad state. */
VNOVEL_EXPORT retro_input_state_t SDL_libretro_input_state_cb = 0;

/* Set once by retro_set_environment. Exported (not static) because the SDL
 * events glue reads core options through it. */
VNOVEL_EXPORT retro_environment_t environ_cb_global = 0;

/* --- core options the glue consumes --------------------------------------- */

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

/* --- text plane (see engine_bridge.h) --------------------------------------
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

/* --- text runs (see engine_bridge.h) ---------------------------------------
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

/* --- navigation results (engine -> shell) ---------------------------------
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

/* --- page events (engine -> shell) ---------------------------------------
 *
 * The reader's place in the text, as one vocabulary shared with the client
 * (see engine_bridge.h). One-way and edge-only: an event describes something
 * that has just happened, so replaying it to a shell that registered later
 * would claim a page just began when the engine may be pages further on. */
#ifdef VNOVEL_DEVWIRE
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
#endif /* VNOVEL_DEVWIRE */

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

/* --- audio ---------------------------------------------------------------- */

/* The engine's SDL publishes its spec here when audio opens; the core reads it
 * to size the audio batch and re-emit AV geometry. NULL until audio is open.
 *
 * The address of this variable is stable; the pointer it holds is not, and is
 * expected to change as audio opens and closes. Read it fresh each time. */
VNOVEL_EXPORT SDL_AudioSpec *SDL_libretro_audio_spec = 0;

/* --- error unwinding ------------------------------------------------------ */

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
