// engine_ons_cabi.cpp — C ABI wrapper for ONScripter-EN.
// Compiled normally (no namespace), linked into the main .so.
// The ONS engine sources are compiled as a separate static library
// with -fvisibility=hidden so their symbols don't collide with PONScripter.

#include "engine_cabi.h"
#include <stdio.h>
#include "ONScripterLabel.h"
#include "engine_cabi_impl.h"

/* The forks differ only in: class name, function prefix, init/event-loop/reset
 * method names, and the select-event constant. Bind those here; the shared
 * delegators live in engine_cabi_impl.h. */
ENGINE_CABI_DELEGATORS(ONScripterLabel, ons, ->init(), ->executeLabel(), ->resetCommand())

#include "engine_runtime_accessors.h"

ENGINE_CABI_FINISH(ons, ONS_SELECT_EVENT)
