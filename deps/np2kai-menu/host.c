/*---------------------------------------------------------------------------*/
/* Host surface for NP2kai's menubase - implementation. See include/host.h for
 * why these five calls are answered here rather than by porting the emulator's
 * screen and task managers.
 *
 * The shape of a menu session, as menubase drives it:
 *
 *     scrnmng_entermenu(&smenu)   -> mark active, record geometry
 *     ... menu draws into its own VRAM, calls menudraw() to show it ...
 *     scrnmng_leavemenu()         -> clear active
 *
 * `taskmng_sleep` is a yield between menu frames. Upstream's returns FALSE to
 * mean "the emulator should terminate", which is how its menu loop ends; this
 * returns TRUE always, because a libretro core's termination is the frontend's
 * decision and the menu has no business ending the process.
 */
/*---------------------------------------------------------------------------*/

#include <compiler.h>
#include <host.h>
#include <embed/vramhdl.h>

/* Set by menubase to the VRAM it has drawn into; the host presents that. */
void *np2kai_menu_vram = NULL;

static int menu_active = 0;
static SCRNMENU menu_geom;

/* The menu's pixel buffer and stride, as this host sees it, so a present can
 * describe it to the seam without re-deriving the layout each time. */
static const unsigned char *menu_pixels = NULL;
static int menu_pitch = 0;

/* The geometry the host offers the menu. A sane default so the smoke test works
 * before the core has said anything; the core overrides it with the real frame
 * size through vnovel_menu_set_geometry(). */
static SCRNMENU menu_host_geom = { 640, 480, 32 };

void vnovel_menu_set_geometry(int width, int height, int bpp)
{
	if (width > 0 && height > 0 && bpp > 0)
	{
		menu_host_geom.width = width;
		menu_host_geom.height = height;
		menu_host_geom.bpp = bpp;
	}
}

BRESULT scrnmng_entermenu(SCRNMENU *smenu)
{
	/* The menu ASKS the host for the geometry it should draw at - menubase_open
	 * calls this and reads the result - rather than telling the host what it
	 * chose. So the host supplies it, and must supply something valid: returning
	 * zeroes makes open fail and the menu never appears. */
	if (!smenu)
		return FAILURE;

	*smenu = menu_host_geom;
	if (smenu->width <= 0 || smenu->height <= 0 || smenu->bpp <= 0)
		return FAILURE;

	menu_geom = *smenu;
	menu_active = 1;
	return SUCCESS;
}

void scrnmng_leavemenu(void)
{
	menu_active = 0;
	menu_pixels = NULL;
	menu_pitch = 0;
	np2kai_menu_vram = NULL;
}

/* Record where the menu's pixels are; the hand-off itself happens on the core's
 * frame path (vnovel_menu_pixels), because the seam's callback is invoked from
 * the core's thread, not from the menu's code.
 *
 * `rct` is the changed region, or NULL for everything. Currently unused: the
 * seam takes a whole buffer per present, and the menu is small enough that a
 * dirty-rect optimisation would buy little while risking a stale region being
 * shown. Recorded as a deliberate omission rather than an oversight. */
void scrnmng_menudraw(const RECT_T *rct)
{
	VRAMHDL v;

	(void)rct;

	v = (VRAMHDL)np2kai_menu_vram;
	if (!v || !v->ptr || v->width <= 0 || v->height <= 0 || v->bpp <= 0)
		return;

	menu_pixels = (const unsigned char *)v->ptr;
	/* VRAM's own pitch convention: width * bytes-per-pixel. */
	menu_pitch = v->width * (v->bpp / 8);
}

/* The core already overlays a software cursor on the engine's frame, so the
 * menu does not need to ask for one. Present for link compatibility; if the
 * menu ever renders its own cursor, this is where it goes. */
void scrnmng_updatecursor(void)
{
}

BOOL taskmng_sleep(UINT32 tick)
{
	(void)tick;
	return TRUE;
}

/*---------------------------------------------------------------------------*/
/* Core-facing queries
 */
/*---------------------------------------------------------------------------*/

int vnovel_menu_active(void)
{
	return menu_active;
}

const unsigned char *vnovel_menu_pixels(int *width, int *height, int *pitch)
{
	if (width)  *width  = menu_geom.width;
	if (height) *height = menu_geom.height;
	if (pitch)  *pitch  = menu_pitch;
	return menu_pixels;
}
