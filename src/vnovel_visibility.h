/* vnovel-visibility.h — how the core's symbols are exported, portably.
 *
 * Replaces link.T. A linker version script (`--version-script`, `global:` /
 * `local: *`) does the same job on ELF, but it is ELF-only: PE/COFF has no
 * equivalent, so Windows builds silently exported everything. The standard,
 * link-script-free mechanism is a source-level one, and libretro's own header
 * already uses it — `RETRO_API` is `__attribute__((visibility("default")))`
 * on ELF and `__declspec(dllexport)` on PE (see libretro.h).
 *
 * So: compile everything with -fvisibility=hidden (default hidden, opt in per
 * symbol) and mark only the cross-module surface:
 *
 *   - the retro_* entry points, via RETRO_API in libretro.h (nothing to do);
 *   - the bridge objects the engine modules read by name, via VNOVEL_EXPORT
 *     below;
 *   - the vendored SDL/SDL_mixer/SMPEG symbols the engines call, which are
 *     already renamed to Private* by sdl-vendored.h and must stay visible
 *     because the engines resolve them from the runtime.
 *
 * Deliberately NOT a version script, even on ELF: one mechanism for every
 * platform means a Windows-only export bug cannot hide behind a passing Linux
 * build.
 */

#ifndef VNOVEL_VISIBILITY_H
#define VNOVEL_VISIBILITY_H

#if defined(_WIN32) || defined(__CYGWIN__)
/* PE/COFF. dllexport on the definition, dllimport on the use would be ideal,
 * but the header is shared by definition and use sites, so export is used
 * throughout: dllimport is an optimisation, not a correctness requirement. */
#define VNOVEL_EXPORT __declspec(dllexport)
#define VNOVEL_HIDDEN
#elif defined(__GNUC__) && (__GNUC__ >= 4)
/* ELF/Mach-O: the counterpart of -fvisibility=hidden. */
#define VNOVEL_EXPORT __attribute__((visibility("default")))
#define VNOVEL_HIDDEN __attribute__((visibility("hidden")))
#else
#define VNOVEL_EXPORT
#define VNOVEL_HIDDEN
#endif

#endif /* VNOVEL_VISIBILITY_H */
