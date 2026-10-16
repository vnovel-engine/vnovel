/*---------------------------------------------------------------------------*/
/* NP2kai's <inputmng.h>, reduced to what menubase uses.
 *
 * Upstream this is the emulator's input manager (key binds, mouse state). What
 * menubase actually uses from it is the *key bitset* below and nothing else:
 * the menu's navigation arrives through `menusys_key(UINT key)`, a bitmask the
 * host produces and menubase interprets itself.
 *
 * That is why the input manager is not ported: this core produces the bitmask
 * from its own input path and calls menusys_key, so the emulator's key-bind
 * machinery has no role here.
 *
 * The values are upstream's, and their order matters - they are tested as bits
 * throughout menubase - so they are copied rather than renumbered.
 *
 * The mouse bits are upstream's too, kept for completeness: nothing in menubase
 * reads them today, and omitting them would be a silent discrepancy if it ever
 * did.
 */
/*---------------------------------------------------------------------------*/

#ifndef NP2KAI_INPUTMNG_H
#define NP2KAI_INPUTMNG_H

#include <compiler.h>

enum {
	LBUTTON_BIT			= (1 << 0),
	RBUTTON_BIT			= (1 << 1),
	LBUTTON_DOWNBIT		= (1 << 2),
	RBUTTON_DOWNBIT		= (1 << 3),
	LBUTTON_UPBIT		= (1 << 4),
	RBUTTON_UPBIT		= (1 << 5),
	MOUSE_MOVEBIT		= (1 << 6),

	KEY_ENTER			= 0x01,
	KEY_MENU			= 0x02,
	KEY_SKIP			= 0x04,
	KEY_EXT				= 0x08,
	NP2_KEY_UP			= 0x10,
	NP2_KEY_DOWN		= 0x20,
	NP2_KEY_LEFT		= 0x40,
	NP2_KEY_RIGHT		= 0x80
};

#endif /* NP2KAI_INPUTMNG_H */
