/* engine_debug.h — the engine slices of the debug mask.
 *
 * Each engine gets 8 bits of its own inside the shared word (see vnovel_debug.h
 * for the layout). The ONScripter engines and the core share this header, which
 * is why it lives here and not in either engine tree: both ONScripter engines
 * are built from this repository's Makefile with -I$(SRC_DIR), and a single
 * header means the two cannot drift apart on a name.
 *
 * KiriKiri does not use this header. It keeps its own self-contained table
 * (deps/krkrsdl2/src/core/base/sdl2/DebugFlags.h), because that engine has a
 * real standalone CMake build that must not depend on the core's headers; the
 * table there maps onto the same KiriKiri slice of the same word.
 *
 * Bits are local to each engine's slice, so ONS and PONS may both call their
 * bit 0 "avi". A bit is a diagnostic: setting one may not change what the
 * engine draws or how it behaves. */

#ifndef ENGINE_DEBUG_H
#define ENGINE_DEBUG_H

#include "vnovel_debug.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* ONScripter-EN (bits 8-15) */
#define ONS_DEBUG_FLAG_AVI (1u << 0) /* the AVI decoder's stream parameters */
#define ONS_DEBUG_ON(bit) VNOVEL_DEBUG_TEST(VNOVEL_DEBUG_SHIFT_ONS, (bit))

/* PONScripter (bits 16-23) */
#define PONS_DEBUG_FLAG_AVI (1u << 0) /* the AVI decoder's stream parameters */
#define PONS_DEBUG_ON(bit) VNOVEL_DEBUG_TEST(VNOVEL_DEBUG_SHIFT_PONS, (bit))

/* The two ONScripter slices, for the parser and the start-up banner. */
extern const VNovelDebugDesc ons_debug_desc;
extern const VNovelDebugDesc pons_debug_desc;

/* Resolve both ONScripter slices from VNOVEL_DEBUG, once. The core calls this
 * with its own slice at start-up; a second call does nothing. The engines
 * themselves only test bits, so they never call it. */
void ons_debug_resolve(void);

#ifdef __cplusplus
}
#endif

#endif /* ENGINE_DEBUG_H */
