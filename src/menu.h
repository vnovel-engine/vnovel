/* The core's own API to the native menu (src/menu.cpp).
 *
 * Our header, not the menu's: deps/np2kai-menu is vendored C whose own headers
 * (menubase.h and friends) are an implementation detail of menu.cpp. Nothing
 * outside menu.cpp should include them, so the menu's bitmask encodings and
 * struct layouts stay behind this boundary.
 */
#ifndef VNOVEL_MENU_H
#define VNOVEL_MENU_H

#ifdef __cplusplus
extern "C" {
#endif

/* Menu navigation, as presses rather than held state: the menu treats a bit as
 * a one-shot command (OK opens the focused submenu, LEFT/RIGHT move the
 * highlight), so the caller sends each press once and never repeats it while
 * the key is held. */
enum {
	VNOVEL_MENU_UP     = 1 << 0,
	VNOVEL_MENU_DOWN   = 1 << 1,
	VNOVEL_MENU_LEFT   = 1 << 2,
	VNOVEL_MENU_RIGHT  = 1 << 3,
	VNOVEL_MENU_OK     = 1 << 4,
	VNOVEL_MENU_CANCEL = 1 << 5
};

/* Create the menu's font handles and icon tables. Called once at start-up;
 * harmless to call twice. `font_path` may be NULL. */
void vnovel_menu_init(const char *font_path);

/* Open the menu bar at `w`x`h` (the live frame size). Returns non-zero on
 * success. The menu bar is a floating overlay: there is no window frame in a
 * libretro core, so the win32-style windowed menu bar is not available. */
int vnovel_menu_open(int w, int h);
int vnovel_menu_is_open(void);
void vnovel_menu_close(void);

/* Let the menu advance one frame. Must be called whether or not it is drawn, so
 * it can run its own timers while open. */
void vnovel_menu_proc(void);

/* Toggle the menu. Opens it if nothing is open, otherwise shows/hides it. This
 * is the only way the menu is shown, and the key it is bound to comes from the
 * `vnovel_menu_hotkey` core option (unbound by default). */
void vnovel_menu_toggle(void);

/* Whether the frame path should composite the menu this frame: it is open and
 * the hotkey has it shown. A game's own `menu.visible` is deliberately not part
 * of this - see the note in vnovel_menu_should_draw's definition. */
int vnovel_menu_should_draw(void);

/* The geometry the menu opens at, published from the live frame so the menu
 * matches the running game rather than a hardcoded size. */
void vnovel_menu_set_pref_geometry(int w, int h);

/* --- the game's own menu, as entries ---------------------------------------
 *
 * A script declares its right-click menu and this is where those entries are
 * offered to the core's menu bar, so the game's options appear in the menu the
 * reader can actually reach. The engine's own renderer cannot show them nested
 * (its menu is a flat list), and the vendored menu HERE can (MSYSITEM carries a
 * child pointer), so the tree is built on this side.
 *
 * A game sends its entries through the bridge -- `vnovel_menu_entry` in
 * engine_bridge.h -- and the core translates them into the menu's own tree.
 *
 * `id` is the GAME's own code for the entry (PONScripter's system-call number),
 * handed back when the reader picks it, so the core never has to know what
 * "save" means: it reports the code and the engine acts on it. 0 means "this is
 * a submenu heading, not a selectable entry" -- the parent of the entries that
 * follow at a greater depth.
 *
 * A heading is signalled by a negative id -- the engine passes -1 when the script
 * named no code the engine knows (its `SUB`, which only opens a submenu), and the
 * core turns that into a non-selectable parent. Any other negative is treated the
 * same way, so an unresolved code degrades into an inert heading rather than into
 * a wrong action.
 *
 * `depth` is 0 for a top-level entry, 1 for a child of the last depth-0 entry,
 * and so on. Entries arrive in script order. */
void vnovel_menu_begin_entries(void);
void vnovel_menu_add_entry(const char *label, int id, int depth);
void vnovel_menu_end_entries(void);

/* Take the code of the entry the reader last picked, or 0 when none is pending.
 * A one-shot read, so a game acts on a pick exactly once. */
int vnovel_menu_take_pick(void);

/* One live session value for the Session page, applied when the bar next opens.
 *
 * `which` is a VNOVEL_MENU_INFO_* index; the text is copied into the menu's own
 * buffer, so the caller's string need not outlive the call and may be freed.
 * Values set before the menu is created are kept. */
enum {
	VNOVEL_MENU_INFO_GAME = 0,      /* what was loaded, by name       */
	VNOVEL_MENU_INFO_ENGINE,        /* which engine, and its version  */
	VNOVEL_MENU_INFO_COMPAT,        /* the era resolved, and its source */
	VNOVEL_MENU_INFO_VERSION,       /* this core's build              */
	VNOVEL_MENU_INFO_SAVES,         /* where saves land               */
	VNOVEL_MENU_INFO_OVERRIDES,     /* the override directory, if any */
	VNOVEL_MENU_INFO_COUNT
};

void vnovel_menu_set_info(int which, const char *text);

/* Pointer input, one frame. `btn` is NP2kai's convention: 0 = move only,
 * 1 = left down, 2 = left up (up outside the menu closes it). `x`/`y` are in
 * frame pixels. */
int vnovel_menu_input(int x, int y, int btn);

/* Key input, a VNOVEL_MENU_* mask of presses. */
void vnovel_menu_key_mask(unsigned mask);

/* Latch a press from the edge-driven keyboard callback, consumed by the next
 * frame. A poll cannot see a tap that begins and ends between frames, so keys
 * arrive this way; the menu is then fed once per frame. */
void vnovel_menu_latch_key(unsigned mask);
unsigned vnovel_menu_take_latched_keys(void);

/* The menu's pixels for compositing, or NULL when none is open.
 *
 * Both planes are 32bpp/8bpp and `mpitch`/`apitch` are per row. The alpha plane
 * is what says which pixels the menu owns - the pixel buffer alone cannot, since
 * untouched VRAM is opaque. Both buffers belong to the menu and stay valid until
 * it is closed.
 *
 * The caller composites onto its own copy of the frame, never onto the engine's
 * framebuffer: modifying that would break the engine's redraw/dirty-rect
 * tracking. */
const unsigned char *vnovel_menu_draw(int *w, int *h, int *pitch,
                                      const unsigned char **alpha,
                                      int *alpha_pitch);

void vnovel_menu_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* VNOVEL_MENU_H */
