/*---------------------------------------------------------------------------*/
/* NP2kai's <compiler.h>, reduced to what the vendored sources need.
 *
 * Upstream's version drags in the platform base header (which redefines the
 * stdint types - see np2kai_types.h for why that cannot be used here), the
 * thread headers, and a handful of project-wide switches. None of that reaches
 * the files vendored under deps/np2kai-menu/: they were checked, and rect.c,
 * resize.c and vramhdl.c use standard C only, with no NP2kai-specific call.
 *
 * So this carries the type definitions and nothing else. If a future vendored
 * file needs more, adding it here is a deliberate act rather than a side effect
 * of copying upstream's header.
 */
/*---------------------------------------------------------------------------*/

#ifndef NP2KAI_COMPILER_H
#define NP2KAI_COMPILER_H

#include "np2kai_types.h"

/* The vendored files reach the rect and list-array types through this header,
 * as upstream's compiler_base.h does (it ends by including both). Kept so the
 * sources stay unmodified. */
#include <common/rect.h>
#include <common/lstarray.h>

#endif /* NP2KAI_COMPILER_H */
