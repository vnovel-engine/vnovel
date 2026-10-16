/*---------------------------------------------------------------------------*/
/* The host surface NP2kai's menubase expects, implemented for this core.
 *
 * menubase reaches outside itself through exactly five calls (measured, not
 * assumed):
 *
 *     scrnmng_entermenu(SCRNMENU *)    hand the host the menu's geometry
 *     scrnmng_leavemenu(void)          return control
 *     scrnmng_menudraw(const RECT_T *) present; NULL means everything
 *     scrnmng_updatecursor(void)
 *     taskmng_sleep(UINT32)            yield
 *
 * Upstream those live in its emulator's screen and task managers, which are not
 * portable (they drive the PC-98 video hardware). But the menu only asks them to
 * show a bitmap and to yield, which is what this core already does every frame.
 * So rather than drag in two emulator subsystems, the five calls are answered
 * here from what the core has.
 *
 * `menuvram` is the menu's own VRAM handle (menubase sets it); the draw call
 * hands that bitmap to the presentation seam, exactly as the engines hand their
 * finished composites, so the menu needs no second output path.
 */
/*---------------------------------------------------------------------------*/

#ifndef VNOVEL_NP2KAI_HOST_H
#define VNOVEL_NP2KAI_HOST_H

#include "np2kai_types.h"
#include <common/rect.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The menu's target geometry, as upstream's scrnmng.h defines it. */
typedef struct {
	int width;
	int height;
	int bpp;
} SCRNMENU;

/* Set by menubase to the VRAM it has drawn into; the host presents this. */
extern void *np2kai_menu_vram;

/* Present the menu to the frontend. Called from the core's own frame path, so
 * the seam's callback is already installed and a present is a copy. */
void vnovel_menu_present(void);

/* True while a menu is open, so the core knows to present the menu plane
 * instead of the engine's frame. */
int vnovel_menu_active(void);

/* The geometry the host offers the menu. The menu asks for this (menubase_open
 * reads it from scrnmng_entermenu), so the host must supply something valid or
 * the menu cannot open at all. */
void vnovel_menu_set_geometry(int width, int height, int bpp);

#ifdef __cplusplus
}
#endif

#endif /* VNOVEL_NP2KAI_HOST_H */
