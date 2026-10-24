# Controls: the core's input contract

This document is the single source of truth for what every input does. It
exists because the same physical action must mean one thing to the user, while
the three engines behind this core expect different keys for it.

**Before changing input code, read [`INPUT-ARCHITECTURE.md`](INPUT-ARCHITECTURE.md)** —
it covers the event-vs-polled-state split that the mappings below sit on top of,
and the per-engine tables that keep injected input visible to games that *poll*
key state (`System.getKeyState`) rather than only handling events.

## The three layers
| Layer | Owner | Where |
|---|---|---|
| 1. Physical button -> RetroPad id | RetroArch (Controls menu) | RetroArch config |
| 2. RetroPad id -> **semantic action** | **this core** (the contract below) | `libretro.cpp`, `input_descs[]` |
| 3. semantic action -> engine key/mouse | per engine | ONS: `sdl-libretro/events.c` (`pad_mapping`) · KiriKiri: `engine_kirikiri_cabi.cpp` (`PAD[]`) |

Layer 2 is the contract the user sees in RetroArch's Controls screen. It must
read identically for every engine; only layer 3 may differ. Do not add a
RetroPad button without adding its row here.

## Canonical mapping (layer 2)

| RetroPad | Semantic action |
|---|---|
| D-pad Up/Down/Left/Right | Focus previous/next control (menus, save slots) |
| Left analog stick | Move the virtual pointer |
| A | Advance / Confirm |
| B | Cancel / Back |
| X | Hide Text Window |
| Y | Hold-to-skip / fast-forward (Ctrl; bounded tap — see below) |
| SELECT | Skip (toggle) |
| START | Auto mode (toggle) |
| L / R | History Back / Forward |
| L2 / R2 | History Back / Forward (same as L/R) |
| L3 / R3 | Pointer click (left / right) |
| Right analog stick | *(free; used only if the pointer source is set to it)* |

## Per-engine translation (layer 3)

Engines disagree about which key does what. Both must implement the table above;
the keys they use differ.

### ONScripter / PONScripter (`sdl-libretro/events.c`)

| Semantic | Engine key/mouse | Notes |
|---|---|---|
| Advance / Confirm | Return | (`s`, Space also advance in-engine) |
| Cancel / Back | Space | |
| Skip (toggle) | `s` | |
| Auto (toggle) | `a` | |
| History Back / Forward | PageUp / PageDown | |
| Save / Load | (system menu) | reachable via the right-click system menu; **no engine-level save key exists** |
| Hide Text Window | right mouse | ONS's native "menu" gesture doubles as hide in many games |
| Focus previous/next | Up/Left, Down/Right | arrow keys drive ONS button focus |

### KiriKiri / KAG (`engine_kirikiri_cabi.cpp`)

| Semantic | Engine key/mouse | Notes |
|---|---|---|
| Advance / Confirm | Return | Space also advances |
| Cancel / Back | Escape | |
| Skip (toggle) | `f` | KAG's skip accelerator |
| Auto (toggle) | `a` | KAG's auto accelerator |
| History Back / Forward | mouse wheel up/down | KAG backlog |
| Save / Load | (system menu / right-click) | no engine-level save key; use the menu |
| Hide Text Window | right mouse | KAG "toggle text boxes" |
| Focus previous/next | Up/Left, Down/Right | arrow keys drive KAG focus |

### Y = Ctrl hold-to-skip (both engines)

Y sends **Ctrl**, which ONS and PON both treat as the hold-to-skip /
fast-forward modifier (`ctrl_pressed_status`) — it is *not* a save key in
either engine, and no engine-level save key exists. Held Ctrl skips instantly
and is hard to control, so Y is delivered as a **bounded tap**: pressing Y skips
for `vnovel_ctrl_tap_frames` frames (default 20 ≈ ⅓ s) and then Ctrl
releases automatically even if Y stays held. A fresh press restarts the tap.
Set the option to `0` for the legacy "held = skip" behaviour. (The tap is
applied in the core's pad layer, so it is identical on both engines.)

## Key remap options

`vnovel_key_lookback`, `_key_menu`, `_key_advance` (and the others in
`key_remap.h`) let the user pick which **keyboard** key triggers a semantic
action, for hosts whose keyboard is bound to the game rather than the pad.
They are applied to **both** engines: the translation happens wherever keyboard
events enter the core, so a remapped key reaches the right engine action
regardless of which engine is running.

### The core's own menu (`Menu Hotkey`)

The `vnovel_menu_hotkey` option binds a key to toggle a menu the **core** draws
over the frame — not the game's menu. It is **F12** by default.

Two other ways in, so the bar is never unreachable:

- **click the bottom-right corner** of the frame (about 64x32 pixels, scaled with
  the frame). The click opens the menu and does not also reach the game.
- the option accepts `F12`, `F11`, `F10`, `Tab`, `Backquote`, or `none` to leave
  it unbound.

While the bar is open it takes the keyboard (arrows move, Enter activates, Escape
closes) and the game receives no input, the same way the game receives none while
the engine's own modal dialogs are up.

The bar's **Game** page is the game's own menubar, when a script declares one with
`resetmenu`/`insertmenu`: those entries are built into a tree, with the script's
submenu depths honoured. An entry whose code the engine cannot perform is drawn
greyed rather than wired to nothing. This is a different menu from the game's
**right-click** menu (`rmenu`), which the engine draws itself and which the core
does not touch — a game may have one, both, or neither.

The bar's **Session** page reports what the core actually resolved for this run:
the loaded archive and its identity, the engine, the era option as set, the
build string, where saves land, and the override directory with whether it is
even present.

### Seeing the menu tree (`VNOVEL_MENU_DUMP`)

`VNOVEL_MENU_DUMP=1` makes the core print the menu tree it built, to stderr: one
`menutree:` line per item with two spaces of indent per level, its id, and its raw
flag. Off unless set. It exists because the vendored menu walker (`appends()` in
`deps/np2kai-menu/embed/menubase/menusys.c`) re-walks the item array and can build
a tree that differs from what was handed to it — the dump shows what was actually
put in the menu. `tests/ons/06_menu_tree` asserts on it.

## Development aids

### Text Debug View (`Text Debug View`)

> For working on the text-plane capture, not for playing. Leave it `off`.

`vnovel_text_debug_view = pip` overlays a translucent picture-in-picture in the
**bottom-right** of the frame showing the text the core has captured,
accumulated into a page: the dialogue alone, over a flat dark backing, with no
artwork behind it. The frame it came from is already on screen behind the
overlay, so there is no second copy of it — the panel exists to show what was
*peeled out*, and nothing else.

It is a pure view: it changes nothing about capture or drawing, and composites
onto the core's own shadow buffer, never the engine's framebuffer. It works on
all three engines.

Its point is that capture defects are otherwise invisible. On KiriKiri the
glyph being drawn used to be published one call *before* it was blended into the
surface, and a garbled panel was how that was found.

**It is also the first consumer of page accumulation.** KiriKiri publishes its
text one *line* at a time (each publication is a line-height box), so "the
captured text" has to be assembled. Turning this option on is what asks the
engines for the placement context that makes that possible; with it off, nothing
is accumulated and the engines skip the work (see the handover, §2.5).

## Known limitations (by design)

- **Engine-native modals are pointer-only.** Confirmation and form dialogs
  drawn by the engine itself (exit-game confirm, overwrite-save form, volume
  entry) do **not** implement arrow-key focus. They require the virtual pointer
  (`pointer_enable`) and a click. Arrow-key focus covers the *game's* menus and
  buttons, not these system dialogs.
- **Pointer vs. focus are separate.** The D-pad never moves the pointer; the
  analog stick (or a real mouse/touch device) does. The D-pad moves focus.
  Pointer-only dialogs therefore need `pointer_enable = enabled` and a pad with
  a stick, or a real mouse.
- **Right-click is overloaded by game.** ONScripter games conventionally use
  right-click for their menu; KiriKiri games (e.g. The House in Fata Morgana)
  use it to toggle text boxes. Canonically X maps to right-click on both
  (layers 1-3 agree); what the *game* does with it is the game's choice and
  cannot be normalized further without per-game options.
- **X ("Hide Text Window") is a name, not a guarantee.** KiriKiri's right-click
  toggles text boxes, which matches the label exactly. ONScripter's right-click
  is its menu gesture by convention, so on ONS the label describes the common
  intent (hide text) while the engine may treat it as "open menu". A single
  layer-2 label cannot encode both; this is a game-convention fact, not a bug.
