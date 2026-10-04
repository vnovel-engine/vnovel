// engine_bridge.c — the shared state that both the core and every engine module
// link against, so that neither has to resolve the other's symbols by name.
//
// Why a bridge module rather than a struct of pointers handed over at load:
//
// The engines and the glue refer to these values by name, in 58 places, and
// several translation units re-declare them locally:
//
//     extern "C" void libretro_script_error_longjmp(void);   (12 files)
//     extern retro_environment_t environ_cb_global;          (events.c:83)
//     extern float opt_mouse_sensitivity;                    (events.c:283)
//
// A macro-based shim rewrites those declarations and does not compile. Moving
// the *definitions* here instead leaves every declaration and every use site
// untouched: the symbols are still resolved by name, just against this module
// rather than against whichever side happened to define them.
//
// Portability: this is an ordinary shared library. On ELF the two modules link
// -lons_bridge; on Windows they link the import library generated when the
// bridge DLL is built. Either way neither module needs the other's symbols, so
// there is no import library pointing at the core and nothing has to be
// exported from it (link.T still hides everything but retro_*).
//
// Verified on both loaders (Wine, real PE binaries): two LoadLibraryA calls for
// the same DLL path return one HMODULE and share one data section, so the core
// and the engine really do see the same objects.
//
// THE CONTRACT — ONE BRIDGE FILE, ONE PATH:
//
// That sharing holds only while every loader resolves the same file. Measured:
// loading bridge.dll and bridge_copy.dll (byte-identical copies, different
// names) yields two distinct instances — two separate copies of every object
// below. Nothing errors; the state simply splits, and the core then reads
// values the engine never wrote. So:
//
//   * ship exactly one bridge binary;
//   * the core loads it by absolute path derived from its own location, and
//     the engine modules link the import library so the loader reuses the
//     already-loaded module rather than searching;
//   * never place a second copy under another name.
//
// Equivalent failure on ELF: two copies differing by path.
//
// Lifetime contract — the reason these are plain globals rather than pointers
// allocated at load:
//
// A pointer is only valid until its pointee is reallocated, and a stale pointer
// shared across a module boundary does not fault, it silently reads the wrong
// memory. So every piece of state here is a static object that lives for the
// whole process: its address never changes, so a copy of that address in any
// module stays correct. Callers must never store the address of anything
// *reachable through* these (e.g. the SDL_AudioSpec that SDL_libretro_audio_spec
// points at) beyond the object's own lifetime — the audio spec is owned by the
// engine's SDL and is only valid while audio is open, exactly as before this
// split.

#include "engine_bridge.h"
#include <setjmp.h>

/* --- frontend callbacks ---------------------------------------------------- */

/* Set once by retro_set_input_state before any engine module is loaded; the
 * glue then calls it to read RetroPad state. */
retro_input_state_t SDL_libretro_input_state_cb = 0;

/* Set once by retro_set_environment. Exported (not static) because the SDL
 * events glue reads core options through it. */
retro_environment_t environ_cb_global = 0;

/* --- core options the glue consumes --------------------------------------- */

float opt_mouse_sensitivity = 1.0;

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
SDL_AudioSpec *SDL_libretro_audio_spec = 0;

/* --- error unwinding ------------------------------------------------------ */

/* Storage for the setjmp/longjmp pair the engines use to unwind out of exit().
 *
 * The buffer lives here (static storage, one copy per process) but it is the
 * ENGINE that executes both calls, on its own thread:
 *
 *     engine thread:  if (setjmp(ons_script_error_jmp)) { clean up; return; }
 *                     ... run the script ...
 *     anywhere:       errorAndExit() -> libretro_script_error_longjmp()
 *
 * setjmp and longjmp must run on the same thread — longjmp restores a stack
 * frame, so jumping into a buffer captured on another thread is undefined. Since
 * the engine owns its thread (see engine_cabi.h), it must own the setjmp, and
 * this module only has to provide the storage both sides can name.
 *
 * The core sets ons_script_error_jmp_valid = 0 before the thread starts and may
 * read it afterwards to learn whether the engine unwound; the engine sets it to
 * 1 once its setjmp is armed, and to 0 on the way out. It is written by one
 * thread and read by another, hence volatile. */
jmp_buf ons_script_error_jmp;
volatile int ons_script_error_jmp_valid = 0;

void libretro_script_error_longjmp(void)
{
    if (ons_script_error_jmp_valid)
        longjmp(ons_script_error_jmp, 1);
}
