// engine_module.h — load protocol for an engine module.
//
// The core dlopen's an engine module and calls ons_module_entry (or
// pons_module_entry). The module fills in the EngineVTable the core drives from
// then on, and resolves the handful of shared values through the bridge module
// (engine_bridge.h), which both sides link.
//
// Why the module is a separate shared object at all:
//
// ONScripter and PONScripter define identical C++ class symbols (ScriptHandler,
// DirectReader, NsaReader, SaReader, ...). Linked into one binary the linker
// keeps one and silently drops the other, which is why there used to be a
// rename pass (rename-syms.sh, ons_rename.txt, pons_rename.txt, -z muldefs) and
// COMDAT-group surgery. Separate modules have separate symbol tables, so the
// collision cannot happen and all of that machinery goes away.
//
// The engine's SDL is not the core's: there must be exactly one live SDL, since
// its event queue, video surface and timers are state the engine's loop is
// blocked in. The module holds that SDL (plus the sdl-libretro glue, mixer and
// decoders); the core reaches it through the returned vtable's pointers.

#ifndef ENGINE_MODULE_H
#define ENGINE_MODULE_H

#include "engine_cabi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Called once, immediately after dlopen. Returns the module's engine vtable, or
 * NULL if the module cannot run — the core checks for NULL rather than calling
 * through it. */
typedef const EngineVTable *(*ons_module_entry_t)(void);

/* The symbol the core dlsyms. Named per engine because one process may load
 * both modules and each needs its own entry point. */
#define ONS_MODULE_ENTRY  "ons_module_entry"
#define PONS_MODULE_ENTRY "pons_module_entry"

const EngineVTable *ons_module_entry(void);
const EngineVTable *pons_module_entry(void);

#ifdef __cplusplus
}
#endif

#endif /* ENGINE_MODULE_H */
