// engine_pons_cabi.cpp — C ABI wrapper for PONScripter.
// Same pattern as engine_ons_cabi.cpp.

#include "engine_cabi.h"
#include <stdio.h>
#include "PonscripterLabel.h"
#include "engine_cabi_impl.h"

/* The forks differ only in: class name, function prefix, init/event-loop/reset
 * method names, and the select-event constant. Bind those here; the shared
 * delegators live in engine_cabi_impl.h. */
ENGINE_CABI_DELEGATORS(PonscripterLabel, pons, ->init(NULL), ->eventLoop(), ->reset())

#include "engine_runtime_accessors.h"

ENGINE_CABI_FINISH(pons, PONS_SELECT_EVENT)
