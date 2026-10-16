/*---------------------------------------------------------------------------*/
/* NP2kai's <taskmng.h>, reduced to what menubase uses.
 *
 * menubase calls taskmng_sleep() as a yield between menu frames. Upstream's
 * returns FALSE to mean "the emulator should terminate", which is how its menu
 * loop ends; this core's implementation always returns TRUE, because process
 * termination is the frontend's decision (see deps/np2kai-menu/host.c).
 */
/*---------------------------------------------------------------------------*/

#ifndef NP2KAI_TASKMNG_H
#define NP2KAI_TASKMNG_H

#include <compiler.h>

#ifdef __cplusplus
extern "C" {
#endif

BOOL taskmng_sleep(UINT32 tick);

#ifdef __cplusplus
}
#endif

#endif /* NP2KAI_TASKMNG_H */
