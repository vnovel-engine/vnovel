// menu.cpp — the core's side of the vendored NP2kai menu.
//
// deps/np2kai-menu/ is the menu itself; this drives it. The menu is a normal
// library: create it once, open a page, feed it input, let it draw, present.
//
// Why this lives in the core rather than in the vendor directory: the menu's
// host surface (deps/np2kai-menu/host.c) answers the five calls menubase makes,
// but *when* to make them — which frame, from where, with what input — is the
// core's business, and it needs the core's input and video paths to answer.
//
// State of this file: the menu is created and can be opened, drawn and closed.
// `vnovel_menu_toggle()` is the entry point a frontend action calls; nothing
// opens it automatically. See private/notes/text-plane-design.md for the
// remaining wiring.

#include <compiler.h>

/* fontmng.h declares its last two entry points OUTSIDE its own extern "C" block
 * (they are the "for SDL" additions upstream appended after closing it), so a
 * C++ caller would get an undefined reference to the mangled name while
 * fontmng.c defines them as plain C.
 *
 * Including the header from inside an extern "C" block gives every declaration
 * in it C linkage, which is both what the definitions are and what upstream
 * intended. This is preferred over redeclaring the two: a redeclaration after
 * the C++ one is a conflict, not a fix. The vendored header stays untouched. */
extern "C" {
#include <fontmng.h>
}
#include <inputmng.h>
#include <embed/vramhdl.h>
/* menubase.h pulls in the rest of the menu's own headers (menusys.h,
 * menuicon.h, menudlg.h, ...) and those have NO include guards, so they must be
 * included exactly once, through here. */
#include <embed/menubase/menubase.h>
#include <host.h>

#include "menu.h"
#include "core_debug.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The core's own menu bar.
 *
 * These pages answer questions that otherwise cost a log file or a gdb session:
 * what the core believes it loaded, which engine era it resolved and from which
 * source, and where saves and overrides are landing. Every value is filled in
 * by the core before the menu is built, so the bar describes the running
 * session rather than a fixed string.
 *
 * Shape follows NP2kai's own tables (sdl/sysmenu.str): rows are
 * {caption, child, id, flag}, a parent row has a non-NULL `child`, and the last
 * row carries MENU_DELETED because appends() walks the table until it sees that
 * flag - getting it wrong walks off the end of the array.
 *
 * The rows are static and only their captions are replaced, which is what the
 * vendored API supports and what keeps opening the bar allocation-free. So the
 * session values live in fixed buffers here rather than as pointers into the
 * core's state: a caption is copied by the menu at create time, and the core's
 * own strings can be replaced (a new game, a device reset) while the bar is up.
 *
 * This is not the game's menu and there is no path from one to the other:
 * NP2kai's menu is built from this array at open time and its runtime-appendable
 * surface is a positioned dialog, so a TJS item tree cannot grow it. */
enum {
	/* Handled by this file. Zero is not used: a row with no action carries 0,
	 * and menubase treats it as such. */
	MENU_ID_CLOSE = 1,
	/* Reserved for items whose action is dispatched from elsewhere, so a page
	 * added later cannot collide with the ones above. */
	MENU_ID_FIRST_DYNAMIC = 0x0100,
	/* Game entries live high, well clear of the core's own ids and of
	 * MENU_ID_FIRST_DYNAMIC + MENU_INFO_COUNT. The LOW 16 bits carry the game's
	 * own code, so a code of 0 would collide with "no entry" -- engine codes are
	 * nonzero by construction (PONScripter's system calls start at 1). */
	MENU_GAME_ID_FIRST = 0x4000
};

/* Which live value a row displays, as a small index into menu_info. The table
 * below and menu_info[] are ordered to match, so a row's id doubles as the
 * index - one place to look, and adding a value means adding it to both. */
enum {
	MENU_INFO_GAME = 0,
	MENU_INFO_ENGINE,
	MENU_INFO_COMPAT,
	MENU_INFO_VERSION,
	MENU_INFO_SAVES,
	MENU_INFO_OVERRIDES,
	MENU_INFO_COUNT
};

static char menu_info[MENU_INFO_COUNT][160] = {
	"Game: (none loaded)",
	"Engine: (unknown)",
	"Era: (not resolved)",
	"Core: ",
	"Saves: (unknown)",
	"Overrides: (none)"
};

/* The Session page: what this run is. Ids are MENU_INFO_FIRST + index so the
 * dispatcher can find the row's value without a second table. */
enum { MENU_INFO_FIRST = MENU_ID_FIRST_DYNAMIC };

static const MSYSITEM menu_items_file[] = {
		{OEMTEXT("Close menu"),	NULL,	MENU_ID_CLOSE,	MENU_DELETED}};

static const MSYSITEM menu_items_session[] = {
		{menu_info[MENU_INFO_GAME],		NULL,	MENU_INFO_FIRST + MENU_INFO_GAME,	MENU_GRAY},
		{menu_info[MENU_INFO_ENGINE],	NULL,	MENU_INFO_FIRST + MENU_INFO_ENGINE,	MENU_GRAY},
		{menu_info[MENU_INFO_COMPAT],	NULL,	MENU_INFO_FIRST + MENU_INFO_COMPAT,	MENU_GRAY},
		{menu_info[MENU_INFO_VERSION],	NULL,	MENU_INFO_FIRST + MENU_INFO_VERSION,	MENU_GRAY},
		{menu_info[MENU_INFO_SAVES],	NULL,	MENU_INFO_FIRST + MENU_INFO_SAVES,	MENU_GRAY},
		{menu_info[MENU_INFO_OVERRIDES],	NULL,	MENU_INFO_FIRST + MENU_INFO_OVERRIDES,	MENU_GRAY | MENU_DELETED}};
/* The MENU_DELETED terminator is on the LAST of these, not a row after it:
 * appends() appends an item and only then tests the flag, so a separate
 * sentinel would be appended too and drawn as a blank row. Verified against a
 * dump of the built tree. */

/* The root bar. MUTABLE, because the Game page's child pointer is filled in by
 * vnovel_menu_end_entries once the game has declared its menu (the tree cannot
 * be static: menusys_create keeps the pointer it is given, and a game's entries
 * are not known until it has run). */
static MSYSITEM menu_items[] = {
		{OEMTEXT("File"),	menu_items_file,	0,	MENU_NOSEND},
		{OEMTEXT("Game"),	NULL,			0,	MENU_NOSEND},
		{OEMTEXT("Session"),	menu_items_session,	0,	MENU_NOSEND | MENU_DELETED}};
/* MENU_DELETED on the last page, for the same reason as above: it is the
 * terminator of the top-level run, and a row after it would be drawn. */

/* The vendored headers declare their own extern "C" (see fontmng.h, menubase.h),
 * so nothing here may wrap them again - nesting the linkage makes the
 * declarations stop matching the C definitions, which shows up as an undefined
 * reference at link time. Only this file's own entry points are wrapped. */
extern "C" {

/* Whether the menu is up. Read by the core's frame path to decide what to
 * present. */
static int menu_open = 0;
static int menu_created = 0;
/* menusys_create() builds the item list once; menusys_open() may then be called
 * repeatedly (each F12). */
static int menu_sys_created = 0;

/* Defined below; used by the hotkey, which opens the root page when nothing is
 * open yet. */
int vnovel_menu_open(int w, int h);
void vnovel_menu_close(void);

/* The frame geometry the menu should open at. The core sets this from the live
 * frame each frame, so the menu matches whatever the running game uses rather
 * than a hardcoded size. */
static int menu_pref_w = 640, menu_pref_h = 480;

void vnovel_menu_set_pref_geometry(int w, int h)
{
	if (w > 0 && h > 0)
	{
		menu_pref_w = w;
		menu_pref_h = h;
	}
}

/* The last render handed to the core. Declared here because the shutdown path
 * invalidates it; written by the capture callback in vnovel_menu_draw. */
static const unsigned char *menu_drawn_pixels = NULL;
static int menu_drawn_w = 0, menu_drawn_h = 0, menu_drawn_pitch = 0;
/* The menu's alpha plane: 1 byte per pixel, stride = width (see vramhdl.h -
 * _VRAMHDL carries `alpha` separately from `ptr`). It is what says which pixels
 * the menu owns; without it the compositor cannot tell menu from untouched VRAM. */
static const unsigned char *menu_drawn_alpha = NULL;
static int menu_drawn_alpha_pitch = 0;

int vnovel_menu_is_open(void)
{
	return menu_open;
}

/* Whether the frame path should draw the menu this frame.
 *
 * One trigger, and it is the user's: the hotkey. The core's menu is its own
 * overlay, drawn at the present boundary, and a game's `menu.visible` has
 * nothing to do with it - see the note in the platform MenuItem's SetVisible.
 * Tying the two together meant the host's menu could only be reached when a
 * game happened to ask for a window menu bar, which is not the same thing at
 * all: a title that never mentions `visible` still has a working menu here, and
 * one that asks for a bar does not put ours on screen.
 *
 * The frame path's shadow-buffer decision also consults this: the menu
 * composites onto the core's copy of the frame, so if the shadow branch were
 * skipped the menu would be invisible, not merely absent. */
static int menu_visible_hotkey = 0;

int vnovel_menu_should_draw(void)
{
	return menu_open && menu_visible_hotkey;
}

/* The hotkey. The only way the menu appears, and a user action, so it is not
 * overridden by anything a script does. */
void vnovel_menu_toggle(void)
{
	if (!menu_open)
	{
		/* Nothing open yet: open the root page and show it. */
		if (vnovel_menu_open(menu_pref_w, menu_pref_h))
		{
			menu_visible_hotkey = 1;
		}
		else
		{
			fprintf(stderr, "menu: hotkey could not open the root page\n");
		}
		return;
	}
	menu_visible_hotkey = !menu_visible_hotkey;
}

/* Create the menu's font handles and icon tables. Called once at start-up;
 * harmless to call twice.
 *
 * The font manager must be initialised first - menubase_create() opens two font
 * handles immediately - and it needs a face name, because the menu measures
 * text. The core passes its own path, which is what the engine uses too. */
void vnovel_menu_init(const char *font_path)
{
	if (menu_created)
		return;

	if (fontmng_init() != SUCCESS)
	{
		fprintf(stderr, "menu: fontmng_init failed, menu disabled\n");
		return;
	}
	if (font_path && *font_path)
		fontmng_setdeffontname(font_path);

	if (menubase_create() != SUCCESS)
	{
		fprintf(stderr, "menu: menubase_create failed, menu disabled\n");
		return;
	}

	menu_created = 1;
}

void vnovel_menu_shutdown(void)
{
	if (!menu_created)
		return;
	if (menu_open)
	{
		menusys_close();
		menu_open = 0;
	}
	if (menu_sys_created)
	{
		menusys_destroy();
		menu_sys_created = 0;
	}
	menubase_destroy();
	/* The cached render points into the VRAM the menu just freed; a later open
	 * must not composite a stale page before the new one has drawn. */
	menu_drawn_pixels = NULL;
	menu_drawn_alpha = NULL;
	menu_drawn_w = menu_drawn_h = menu_drawn_pitch = 0;
	menu_drawn_alpha_pitch = 0;
	menu_created = 0;
}


/* the game's own menu entries
 *
 * Held HERE, in mutable storage, and turned into the menu's own tree on demand:
 * menusys_create() takes a `const MSYSITEM *` and keeps it, so a tree that a game
 * can add to cannot be the static one above -- the entries are collected first
 * and the menu is rebuilt from them.
 *
 * Rebuilding is not free (it frees and re-creates the menu), so it happens at
 * vnovel_menu_end_entries(), which is once per game, not once per entry.
 *
 * The title a game shows may contain the engine's text marker (PONScripter's
 * '^' under UTF-8), which the renderer consumes and never draws. Stripped here
 * because the marker is the ENGINE's dialect and this is the core's menu: a
 * label arriving with it would show a stray '^' to the reader. */
#define MENU_GAME_MAX_ENTRIES  128
#define MENU_GAME_MAX_DEPTH    8

typedef struct {
	int  id;          /* the engine's code, or -1 for a heading */
	int  depth;       /* 0 = top level; d belongs to the nearest shallower entry */
} MENU_GAME_ENTRY;

static MENU_GAME_ENTRY menu_game[MENU_GAME_MAX_ENTRIES];
static int  menu_game_count = 0;
static int  menu_game_building = 0;
static char menu_game_labels[MENU_GAME_MAX_ENTRIES][96];
/* The code of the game entry the reader picked, one-shot (see
 * vnovel_menu_take_pick). Declared here because menu_cmd, above its reader,
 * writes it. */
static int menu_pick = 0;

/* The engine's text marker, stripped from a label. PONScripter uses '^' for
 * UTF-8 and '`' for CP932; the core sees only the label, so both are removed
 * when they LEAD a line, which is the only place the marker is meaningful. */
static const char *menu_clean_label(const char *in)
{
	if (in && (in[0] == '^' || in[0] == '`'))
		return in + 1;
	return in ? in : "";
}

void vnovel_menu_begin_entries(void)
{
	menu_game_count = 0;
	menu_game_building = 1;
}

void vnovel_menu_add_entry(const char *label, int id, int depth)
{
	if (!menu_game_building || menu_game_count >= MENU_GAME_MAX_ENTRIES)
		return;
	MENU_GAME_ENTRY *e = &menu_game[menu_game_count];
	snprintf(menu_game_labels[menu_game_count], sizeof(menu_game_labels[0]),
	         "%s", menu_clean_label(label));
	e->id = id;
	e->depth = (depth < 0) ? 0 : (depth > MENU_GAME_MAX_DEPTH ? MENU_GAME_MAX_DEPTH : depth);
	menu_game_count++;
}

/* The item pool the tree is emitted into.
 *
 * The vendored walker (appends(), menusys.c) advances with `item++` and chains
 * siblings with `next` until it meets a MENU_DELETED flag -- so a run of
 * siblings has to be its own CONTIGUOUS array, ended by a sentinel. A single
 * flat array of all entries therefore cannot express this tree: pointing a
 * parent's `child` at the next entry would make the walker swallow every entry
 * after it, since `item++` only stops at a sentinel.
 *
 * So the pool holds one terminated block per run (one per parent, plus the
 * top level), and a run's items are laid out before its children's runs. A run
 * costs its children plus one sentinel, so entries + runs <= 2*entries + 1. */
#define MENU_GAME_POOL  (MENU_GAME_MAX_ENTRIES * 2 + 2)

static MSYSITEM menu_game_items[MENU_GAME_POOL];
static int menu_game_pool_used = 0;
/* The tree as indices into menu_game[], linked once and then emitted: the pool
 * layout is written in two passes (run first, children after), so the links
 * cannot be followed as the pool is filled. */
static int menu_game_first_child[MENU_GAME_MAX_ENTRIES];
static int menu_game_last_child[MENU_GAME_MAX_ENTRIES];
static int menu_game_next_sib[MENU_GAME_MAX_ENTRIES];
static int menu_game_top = -1;
static int menu_game_last_top = -1;
/* Where each entry's menu item ended up in the pool, so a parent can point its
 * child at it after the child's run has been written. */
static int menu_game_slot[MENU_GAME_MAX_ENTRIES];
/* Shown on the Game page when the game declares no menu. One item, carrying the
 * terminator itself (see menu_game_emit_run for why there is no row after it). */
static const MSYSITEM menu_items_no_game[] = {
		{OEMTEXT("(the game declares no menu)"),	NULL,	0,	MENU_GRAY | MENU_DELETED}};

/* Dump a run of pool items as `menutree:` lines, two spaces per level.
 *
 * Headless-safe: it prints the tree the loader produced, without opening the menu,
 * so a test fixture can assert the nesting. The library's own menusys_dump
 * answers the different question of what appends() built from these arrays. */
static void menu_dump_run(const MSYSITEM *run, int depth)
{
	/* The walk mirrors appends() in menusys.c, and the ORDER matters: an item's
	 * subtree is visited BEFORE the terminator is tested. The last sibling carries
	 * MENU_DELETED, so testing it at the top of the loop would stop at the last
	 * entry and hide its whole subtree -- which is exactly what a first version
	 * of this did (a dump that printed only the first two of twelve entries). */
	for (const MSYSITEM *it = run; it; it++) {
		fprintf(stderr, "menutree: %*s%s id=%u flag=0x%04x\n",
		        depth * 2, "",
		        (it->string && it->string[0]) ? it->string : "(no text)",
		        (unsigned)it->id, (unsigned)it->flag);
		if (it->child)
			menu_dump_run(it->child, depth + 1);
		if (it->flag & MENU_DELETED)
			break;
	}
}

/* Write one run -- `first` and its siblings -- into the pool, then the runs of
 * each item's children. Returns the pool index of the run's first item, or -1.
 *
 * The order matters: a run is written whole before any child run, because the
 * walker needs the siblings adjacent. Recursing first would interleave a child's
 * subtree between two siblings and break the run. */
static int menu_game_emit_run(int first)
{
	if (first < 0)
		return -1;

	int start = menu_game_pool_used;
	int last = start;

	for (int i = first; i != -1; i = menu_game_next_sib[i]) {
		if (menu_game_pool_used >= MENU_GAME_POOL)
			return -1;
		MSYSITEM *it = &menu_game_items[menu_game_pool_used];
		it->string = menu_game_labels[i];
		it->child  = NULL;
		/* The id is the entry's SLOT in menu_game[], not its code: a code is the
		 * engine's own number and can be any int, while a MENUID is 16 bits, so
		 * tagging the code directly would truncate a large one. The slot is
		 * bounded by the entry count, and menu_cmd maps it back to the code. */
		it->id   = (MENUID)(MENU_GAME_ID_FIRST + i);
		/* Three cases, and the flag distinguishes them so the menu tells the
		 * truth about what it can do:
		 *
		 *   a code        selectable, reports the code to the engine
		 *   no code, children   a heading: opens its submenu, sends nothing
		 *   no code, no children  inert: nothing to do, so drawn greyed rather
		 *                         than pretending to be selectable
		 *
		 * MENU_NOSEND, not MENU_GRAY, for the heading: a greyed item never
		 * opens its child (menusys.c gates childopn on that flag), so grey
		 * there would make the submenu unreachable. */
		it->flag = (menu_game[i].id >= 0) ? 0
		         : (menu_game_first_child[i] >= 0) ? MENU_NOSEND
		         : MENU_GRAY;
		menu_game_slot[i] = menu_game_pool_used;
		last = menu_game_pool_used;
		menu_game_pool_used++;
	}

	/* The run's LAST item carries MENU_DELETED, because appends() appends an
	 * item and only THEN tests the flag -- so the terminator has to be the last
	 * real item, not a separate entry after it. A separate sentinel row gets
	 * appended like any other and shows up as a blank menu row, which is exactly
	 * what a dump of the built tree showed. append1() strips the bit from the
	 * built item (`flag & ~MENU_DELETED`), so it costs nothing but the stop.
	 *
	 * This is also the idiom the library's own tables use: s_exit's last item is
	 * `{str_sysc, NULL, SID_CLOSE, MENU_DELETED}`. */
	menu_game_items[last].flag |= MENU_DELETED;

	for (int i = first; i != -1; i = menu_game_next_sib[i]) {
		if (menu_game_first_child[i] == -1)
			continue;                    /* a leaf */
		int cs = menu_game_emit_run(menu_game_first_child[i]);
		menu_game_items[menu_game_slot[i]].child =
			(cs >= 0) ? &menu_game_items[cs] : NULL;
	}

	return start;
}

/* Turn the collected entries into the menu's tree and rebuild the menu.
 *
 * Nesting: an entry at depth d belongs to the nearest entry before it at a
 * smaller depth -- most often exactly d-1, but the levels need not be
 * contiguous. That is the script's own rule for `insertmenu`'s third argument
 * ("insertion into a submenu dug that many levels deep"), and it is what puts
 * Wave's depth-2 options under the depth-1 Wave heading.
 *
 * A HEADING is signalled by a negative id (the engine has no code for it: it is
 * the script's `SUB`, which only opens a submenu). See vnovel_menu_add_entry. */
void vnovel_menu_end_entries(void)
{
	menu_game_building = 0;

	if (menu_game_count == 0) {
		menu_items[1].child = menu_items_no_game;
		return;
	}

	/* Link the entries into a tree of indices. `open[d]` is the most recent
	 * entry at depth d; it is the parent of the next entry deeper than d, and
	 * anything deeper is closed once a shallower entry arrives. */
	int open[MENU_GAME_MAX_DEPTH + 1];
	for (int d = 0; d <= MENU_GAME_MAX_DEPTH; d++)
		open[d] = -1;

	for (int i = 0; i < menu_game_count; i++) {
		menu_game_first_child[i] = -1;
		menu_game_last_child[i]  = -1;
		menu_game_next_sib[i]    = -1;

		int d = menu_game[i].depth;
		int p = (d > 0) ? open[d - 1] : -1;

		if (p < 0) {
			if (menu_game_top < 0)
				menu_game_top = i;
			else
				menu_game_next_sib[menu_game_last_top] = i;
			menu_game_last_top = i;
		} else {
			if (menu_game_first_child[p] < 0)
				menu_game_first_child[p] = i;
			else
				menu_game_next_sib[menu_game_last_child[p]] = i;
			menu_game_last_child[p] = i;
		}

		open[d] = i;
		for (int k = d + 1; k <= MENU_GAME_MAX_DEPTH; k++)
			open[k] = -1;                /* a shallower entry closes the deeper run */
	}

	menu_game_pool_used = 0;
	int top = menu_game_emit_run(menu_game_top);
	if (top < 0) {
		fprintf(stderr, "menu: the game's menu did not fit (%d entries)\n",
		        menu_game_count);
		menu_items[1].child = menu_items_no_game;
		return;
	}

	/* The page points STRAIGHT at the top run. No wrapper item: a `Game` page
	 * whose only child was another `Game` was what a dump of the built tree
	 * showed, and the extra level made the reader open two submenus to reach the
	 * game's own entries. */
	menu_items[1].child = &menu_game_items[top];

	/* A dump of the tree as handed over, so a test fixture can assert it without
	 * opening the menu (which the headless harness never does). It answers "what
	 * did the loader make of the script's depths"; the library's own
	 * menusys_dump answers the separate question "what did appends() build from
	 * it". Off unless the core's `menu` debug bit is set. */
	if (CORE_DEBUG_ON(DEBUG_FLAG_MENU))
		menu_dump_run(&menu_game_items[top], 0);

	/* Rebuild, because menusys_create keeps the pointer it was given and the
	 * tree above has changed. Only when the menu already exists: before that,
	 * the next open builds from the page's child anyway. */
	if (menu_sys_created) {
		if (menu_open) { menusys_close(); menu_open = 0; }
		menusys_destroy();
		menu_sys_created = 0;
	}
}

int vnovel_menu_take_pick(void)
{
	int p = menu_pick;
	menu_pick = 0;
	return p;
}

/* Called when the user activates an item.
 *
 * A MENU_GRAY row is inert - the menu will not invoke its command - so the
 * Session rows never arrive here; they are reported, not acted on. What can
 * arrive is Close, and anything a future page adds above MENU_ID_FIRST_DYNAMIC. */
static void menu_cmd(MENUID id)
{
	if (id == MENU_ID_CLOSE)
	{
		vnovel_menu_close();
		return;
	}
	/* A GAME entry: the id is the engine's own code for it, and what it means is
	 * the engine's business -- the core reports the pick and the engine acts. So
	 * there is deliberately no knowledge of "save" or "load" here. The menu
	 * closes first, because the game is about to draw over the frame. */
	if ((int)id >= MENU_GAME_ID_FIRST)
	{
		/* The id is the entry's slot (see menu_game_emit_run); the CODE is what
		 * the engine published and what it will act on, so that is what is
		 * reported. A heading has no code and reached here only if it was somehow
		 * activated, so it reports nothing. */
		int slot = (int)id - MENU_GAME_ID_FIRST;
		if (slot >= 0 && slot < menu_game_count && menu_game[slot].id >= 0)
			menu_pick = menu_game[slot].id;
		vnovel_menu_close();
		return;
	}
}

/* One live session value, by index into menu_info. Returns the slot so a caller
 * can build the string in place, which is what vnovel_menu_set_info does. */
static char *menu_info_slot(int which)
{
	if (which < 0 || which >= MENU_INFO_COUNT)
		return NULL;
	return menu_info[which];
}

extern "C" void vnovel_menu_set_info(int which, const char *text)
{
	char *slot = menu_info_slot(which);
	if (!slot || !text)
		return;
	/* Truncated rather than refused: a long path is still useful shortened, and
	 * the row is a fixed buffer because the menu keeps its own copy. */
	snprintf(slot, sizeof(menu_info[0]), "%s", text);
}

/* Push the current values into a built menu. Called on every open, so a value
 * that changed since the last time the bar was up - a new game, a different
 * save path - is what the user sees.
 *
 * Rows are located by id, which is why the table's ids and menu_info's indices
 * are the same numbers: the loop below is the whole of the mapping. */
static void menu_refresh_pages(void)
{
	for (int i = 0; i < MENU_INFO_COUNT; i++)
		menusys_msg(SMSG_SETTEXT, (MENUID)(MENU_INFO_FIRST + i),
		            (INTPTR)menu_info[i]);
}

/* Open the menu bar.
 *
 * menubase_open() alone is NOT enough to make a menu appear: it only records a
 * menu number, and no page content exists for it. The content comes from
 * menusys_create() (build the item list) followed by menusys_open() (enter it),
 * which is how NP2kai itself drives it - menubase_open(1) is called *from*
 * menusys_open. Opening a page id directly therefore produced an open menu with
 * nothing in it, which is what made it invisible.
 *
 * `w`/`h` are the destination geometry. The menu asks the host for this
 * through scrnmng_entermenu() and reads it in menubase_open(), so it must be
 * set before the open, not derived afterwards. */
int vnovel_menu_open(int w, int h)
{
	if (!menu_created)
		return 0;

	vnovel_menu_set_geometry(w, h, 32);

	if (!menu_sys_created)
	{
		if (menusys_create(menu_items, menu_cmd, MICON_NULL, OEMTEXT("")) !=
		    SUCCESS)
		{
			fprintf(stderr, "menu: menusys_create failed\n");
			return 0;
		}
		menu_sys_created = 1;
		/* What the library BUILT, not what was handed to it. */
		menusys_dump();
	}

	if (menusys_open(0, 0) != SUCCESS)
	{
		fprintf(stderr, "menu: menusys_open failed\n");
		return 0;
	}

	/* After the menu exists and before it draws: the rows were created from
	 * menu_info's initial text, and this is what makes the bar show the current
	 * session rather than the strings it was built with. Cheap - a handful of
	 * small strcpy into the menu's own storage - and it is why a value captured
	 * at load time is still correct after a device reset. */
	menu_refresh_pages();

	menu_open = 1;
	/* The first draw must render, so drop any cache from a previous open; the
	 * menu renders on entry, so vnovel_menu_draw() will fill it. */
	menu_drawn_pixels = NULL;
	menu_drawn_alpha = NULL;
	menu_drawn_w = menu_drawn_h = menu_drawn_pitch = 0;
	menu_drawn_alpha_pitch = 0;
	return 1;
}

void vnovel_menu_close(void)
{
	if (!menu_open)
		return;
	menusys_close();
	menu_open = 0;
	menu_drawn_pixels = NULL;
	menu_drawn_alpha = NULL;
	menu_drawn_w = menu_drawn_h = menu_drawn_pitch = 0;
	menu_drawn_alpha_pitch = 0;
}

/* Feed one frame of pointer input to the menu. `x`/`y` are in frame pixels and
 * `btn` follows NP2kai's own convention (sdl/taskmng.c). The menu handles its
 * own navigation from these, so this is the whole of the pointer contract. */
int vnovel_menu_input(int x, int y, int btn)
{
	if (!menu_open || !vnovel_menu_should_draw())
		return 0;

	/* NP2kai's own convention (sdl/taskmng.c): 0 = move, 1 = left down,
	 * 2 = left up. A click at btn 2 outside every window closes the menu, which
	 * is why the up edge matters and not only the down. */
	menubase_moving(x, y, btn);

	/* After the menu, closing itself is reflected here: a click outside closes
	 * it, and the same click must not then reach the game. */
	return 1;
}

/* Presses accumulated by the core's keyboard callback since the last frame.
 * The callback is edge-driven, so it cannot miss a tap; the menu is fed once per
 * frame from here. */
static unsigned menu_latched_keys = 0;

void vnovel_menu_latch_key(unsigned mask)
{
	menu_latched_keys |= mask;
}

unsigned vnovel_menu_take_latched_keys(void)
{
	unsigned m = menu_latched_keys;
	menu_latched_keys = 0;
	return m;
}

/* Route one frame of menu navigation, as a VNOVEL_MENU_* mask.
 *
 * The bits are the core's, not NP2kai's, so inputmng.h stays inside this file.
 * They are *presses*, not held state: menusys_key treats a bit as a command
 * (KEY_ENTER opens the focused submenu, LEFT/RIGHT move the highlight), so the
 * caller sends each press once. Needed as well as the pointer because a device
 * may have no mouse at all. */
void vnovel_menu_key_mask(unsigned mask)
{
	unsigned keys = 0;

	if (!menu_open || !vnovel_menu_should_draw() || !mask)
		return;

	if (mask & VNOVEL_MENU_UP)     keys |= NP2_KEY_UP;
	if (mask & VNOVEL_MENU_DOWN)   keys |= NP2_KEY_DOWN;
	if (mask & VNOVEL_MENU_LEFT)   keys |= NP2_KEY_LEFT;
	if (mask & VNOVEL_MENU_RIGHT)  keys |= NP2_KEY_RIGHT;
	if (mask & VNOVEL_MENU_OK)     keys |= KEY_ENTER;
	if (mask & VNOVEL_MENU_CANCEL) keys |= KEY_SKIP;

	menubase_key(keys);
}

/* Let the menu advance one frame. */
void vnovel_menu_proc(void)
{
	if (menu_open)
		menubase_proc();
}

/* Draw the menu and hand the result to the caller, so the core can put it on
 * the frame. Returns the pixel data or NULL when no menu is up.
 *
 * The buffer is the menu's own VRAM, which menubase keeps in the global
 * `menuvram` (menubase.h) - the same thing upstream's own scrnmng_menudraw
 * reads. The menu renders on entry and on change, not every frame, so reading
 * it here is enough; no per-frame callback is involved.
 *
 * The cache matters: on an idle frame nothing new is drawn. Reporting NULL
 * there would make the menu flicker off, so the last render keeps being
 * composited until the menu is closed. `menuvram` outlives every frame while
 * the menu is open and is dropped on close. */
const unsigned char *vnovel_menu_draw(int *w, int *h, int *pitch,
                                    const unsigned char **alpha,
                                    int *alpha_pitch)
{
	/* The same gate as composite_menu, repeated at the source so no caller can
	 * reach a page that nothing asked to be shown. The menu is opened on the
	 * game's `menu.visible` request - that is what builds its item tree - but
	 * the bar is drawn only on the hotkey, so "open" and "on screen" are two
	 * different things here and this is the one that decides the second. */
	if (!menu_open || !vnovel_menu_should_draw())
		return NULL;

	if (menuvram && menuvram->ptr && menuvram->width > 0 &&
	    menuvram->height > 0 && menuvram->bpp > 0)
	{
		menu_drawn_pixels = (const unsigned char *)menuvram->ptr;
		menu_drawn_w = menuvram->width;
		menu_drawn_h = menuvram->height;
		menu_drawn_pitch = menuvram->width * (menuvram->bpp / 8);
		menu_drawn_alpha = (const unsigned char *)menuvram->alpha;
		menu_drawn_alpha_pitch = menuvram->width;
	}

	if (!menu_drawn_pixels)
		return NULL;

	if (w)           *w = menu_drawn_w;
	if (h)           *h = menu_drawn_h;
	if (pitch)       *pitch = menu_drawn_pitch;
	if (alpha)       *alpha = menu_drawn_alpha;
	if (alpha_pitch) *alpha_pitch = menu_drawn_alpha_pitch;
	return menu_drawn_pixels;
}

} // extern "C"
