# Controls: the core's input contract

This document is the single source of truth for what every input does. It
exists because the same physical action must mean one thing to the user, while
the three engines behind this core expect different keys for it.

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
| Y | Save |
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
| Save | RCtrl | ONS's save accelerator |
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
| Save | `s` | KAG's save accelerator |
| Hide Text Window | right mouse | KAG "toggle text boxes" |
| Focus previous/next | Up/Left, Down/Right | arrow keys drive KAG focus |

## Key remap options

`onscripter-en_key_lookback`, `_key_menu`, `_key_advance` (and the others in
`key_remap.h`) let the user pick which **keyboard** key triggers a semantic
action, for hosts whose keyboard is bound to the game rather than the pad.
They are applied to **both** engines: the translation happens wherever keyboard
events enter the core, so a remapped key reaches the right engine action
regardless of which engine is running.

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
