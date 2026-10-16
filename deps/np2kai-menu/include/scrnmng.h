/*---------------------------------------------------------------------------*/
/* NP2kai's <scrnmng.h>, reduced to what menubase uses.
 *
 * Upstream this is the emulator's screen manager - PC-98 video modes, palettes,
 * the WAB/GDC hardware. menubase includes it for four calls and nothing else,
 * and those four are implemented in deps/np2kai-menu/host.c against this core's
 * presentation seam.
 *
 * Only the menu-facing declarations are here. If menubase ever needs more of the
 * screen manager, the link fails loudly rather than the call silently doing
 * nothing - which is the reason for a narrow header rather than upstream's.
 */
/*---------------------------------------------------------------------------*/

#ifndef NP2KAI_SCRNMNG_H
#define NP2KAI_SCRNMNG_H

#include <host.h>
#include <common/rect.h>

#ifdef __cplusplus
extern "C" {
#endif

BRESULT scrnmng_entermenu(SCRNMENU *smenu);
void scrnmng_leavemenu(void);
void scrnmng_menudraw(const RECT_T *rct);
void scrnmng_updatecursor(void);

#ifdef __cplusplus
}
#endif

#endif /* NP2KAI_SCRNMNG_H */
