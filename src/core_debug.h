/* core_debug.h — the core's slice of the debug mask. See vnovel_debug.h.
 *
 * Bits are local to the core's slice (bits 0-7 of the 64-bit word), so these
 * numbers are independent of every engine's. A bit here is a diagnostic:
 * setting one may not change what the core draws or how it drives an engine.
 *
 * A diagnostic that carries a VALUE rather than a yes/no is not a bit and keeps
 * its own environment variable, read once at start-up like any other parameter:
 *
 *   VNOVEL_TEXT_RUN_LOG=<path>      write the text stream to this file
 *   VNOVEL_PRESENT_STRIPES=<h>:<a>  stripe geometry (with DEBUG_FLAG_PRESENT)
 *   VNOVEL_DEV_PORT=<port>          open the development socket
 *   ONS_TEST_WIDTH / ONS_TEST_HEIGHT, ONS_TEST_AUTODVANCE, VNOVEL_MENU_TEST,
 *   KRKRSDL2_TEXT_PAGE              test hooks that change behaviour, not
 *                                   observation, and so are not mask bits. */

#ifndef CORE_DEBUG_H
#define CORE_DEBUG_H

#include "vnovel_debug.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define DEBUG_FLAG_CLOCK   (1u << 0) /* the game clock, ~2 Hz */
#define DEBUG_FLAG_INPUT   (1u << 1) /* input path: pad/mouse poll and push */
#define DEBUG_FLAG_PRESENT (1u << 2) /* stripes over the finished frame */
#define DEBUG_FLAG_MENU    (1u << 3) /* the native menu tree as built */

/* The core's slice, for the parser and the banner. */
extern const VNovelDebugDesc core_debug_desc;

/* Resolve VNOVEL_DEBUG for the core's slice. Call once at start-up, before
 * anything can print a diagnostic; a second call does nothing. */
void core_debug_init(void);

/* True when the core's `bit` is set. */
#define CORE_DEBUG_ON(bit) VNOVEL_DEBUG_TEST(VNOVEL_DEBUG_SHIFT_CORE, (bit))

#ifdef __cplusplus
}
#endif

#endif /* CORE_DEBUG_H */