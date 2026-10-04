# Input: events vs polled state

Why this document exists: the same defect shipped **three times** in one session
(D-pad dead, A-button didn't advance text, and two near-misses the audit caught),
each time looking like a different bug. It is one rule, and it is easy to
violate by accident.

## The rule

**A program can receive input two ways, and they are maintained separately:**

| Form | What it is | API |
|---|---|---|
| **Event delivery** | "a key went down" — queued, dispatched to handlers | `SDL_PollEvent`, `onKeyDown`/`onKeyUp` (TJS), game handlers |
| **Polled state** | "is this key down *right now*?" — queried on demand | `SDL_GetKeyboardState`, `SDL_GetMouseState`, `SDL_GetModState`, `SDL_GameControllerGetButton`, `SDL_GetKeyState` (SDL1.2), TJS `System.getKeyState` |

**Real hardware updates both.** A **synthetic** input source (which is what this
core is — libretro gives us no OS input device) updates **only what it explicitly
writes**. In SDL **2**, `SDL_PushEvent` updates the event queue and **nothing
else** — the polled arrays are untouched. In SDL **1.2** the situation differs
(below).

So: **if you synthesize input, you must feed both the event stream *and* every
polled-state API the program can reach.** Games use both, for different things,
which is exactly why the failures looked arbitrary:

- KAG menu navigation polls `System.getKeyState(VK_PADUP/…)` → *state*.
- KAG text advance polls `System.getKeyState(VK_RETURN)` → *state*.
- KAG history / save / other actions listen for `onKeyDown` → *events*.

A key that works for one action and not another is the signature of this bug.

## Per-engine status

### KiriKiri / krkrsdl2 (SDL 2) — was affected, now handled

Synthetic input enters via `engine_kirikiri_cabi.cpp` (`krkr_pump_events`), which
pushes SDL2 events. The engine consumes them in `sdl_process_events`
(`deps/krkrsdl2/src/core/sdl2/SDLApplication.cpp`).

Because `SDL_PushEvent` does not update SDL2's polled arrays, the engine keeps
its **own state tables, written by the same handlers that post the TJS events**,
and the polled APIs consult them:

| Polled API | Synthetic source | Table |
|---|---|---|
| `TVPGetJoyPadAsyncState` (`getKeyState(VK_PAD*)`) | `SDL_CONTROLLERBUTTONDOWN/UP` | `sdl_pad_button_state[]` via `krkrsdl2_note_pad_button` |
| `TVPGetKeyMouseAsyncState` keyboard (`getKeyState(VK_RETURN)`, …) | `SDL_KEYDOWN/UP` | `sdl_key_state[]` via `krkrsdl2_note_key` |
| same, mouse buttons (`getKeyState(VK_LBUTTON)`) | `SDL_MOUSEBUTTON*` | `sdl_mouse_state[]` via `krkrsdl2_note_mouse_button` |
| same, modifiers (`VK_SHIFT/CONTROL/MENU`) | `SDL_KEYDOWN/UP` | `sdl_key_state[]` |

**The seam is the engine's own input handlers**: `SDL_KEYDOWN`/`SDL_KEYUP`,
`SDL_CONTROLLERBUTTON*`, and `SDL_MOUSEBUTTON*` in `sdl_process_events` are the
single place that both (a) posts the event and (b) updates the polled table.
Add a new synthetic source there and the state follows automatically. Do **not**
update one without the other.

*Residual gap:* mouse **position** (`SDL_GetMouseState` coordinates) is likewise
not updated by injected motion; it is currently unused by scripts, but the same
pattern applies if that changes.

### ONScripter-EN and PONScripter (SDL 1.2) — not affected, by construction

These do **not** need state tables, for a reason worth knowing before assuming
"the fix" should be copied to them:

1. **SDL 1.2's helpers update both.** The shim
   (`sdl-libretro/events.c`) injects via `SDL_PrivateKeyboard` /
   `SDL_PrivateMouseMotion` / `SDL_PrivateMouseButton`, and SDL 1.2's
   `SDL_PrivateKeyboard` writes `SDL_KeyState[sym]` **and** posts the event
   (`deps-overlay/deps/sdl/src/events/SDL_keyboard.c:551` and `:564`). This is
   the crucial SDL1-vs-SDL2 difference: SDL 2 **split** those two
   responsibilities, so `SDL_PushEvent` alone is no longer enough.
2. **Neither engine polls keyboard state anyway.** No `SDL_GetKeyState` /
   `SDL_GetKeyboardState` calls exist in `deps/onscripter-en` or
   `deps/ponscripter`; input is consumed purely through `SDL_WaitEvent` loops.
   Only modifier flags (`ctrl_pressed_status`, `shift_pressed_status`) are
   tracked, and they are derived from events.

So: **if these engines are ever moved to SDL 2, or if a script starts polling key
state, they become affected immediately** and will need the same table pattern.

## How to avoid regressing

1. **Adding a synthetic input?** Ask: *does anything poll this?* If yes, update a
   state table in the same handler that posts the event.
2. **Adding a polled-input API to an engine?** Ask: *does the synthetic path
   write it?* If not, add the fallback (SDL2 only — SDL1.2 helpers already do it).
3. **Symptom to recognise:** "some actions on this key work, others don't" (or
   "works in one menu, not another") ⇒ suspect event-vs-state, not the mapping.
   Check the *polling* path first: `System.getKeyState` → `TVPGetAsyncKeyState` →
   `TVPGetKeyMouseAsyncState` / `TVPGetJoyPadAsyncState`.
4. **Tests.** The natural regression test is a script-level probe: drive a known
   pad button and assert both halves —
   - the event arrives (e.g. D-pad moves focus in the title menu), and
   - the poll agrees (`System.getKeyState(VK_RETURN)` true while A is held).
   A test that only exercises the event path will keep passing while the poll
   path is broken — which is exactly how this shipped three times.

## Inventory (as of this writing)

| Engine | API | Kind | Fed by synthetic? |
|---|---|---|---|
| KiriKiri | `System.getKeyState` → `TVPGetJoyPadAsyncState` | poll | **Y** (`sdl_pad_button_state`) |
| KiriKiri | `System.getKeyState` → `TVPGetKeyMouseAsyncState` (keys) | poll | **Y** (`sdl_key_state`) |
| KiriKiri | `System.getKeyState` → `TVPGetKeyMouseAsyncState` (mouse buttons) | poll | **Y** (`sdl_mouse_state`) |
| KiriKiri | `System.getKeyState` → `TVPGetKeyMouseAsyncState` (modifiers) | poll | **Y** (`sdl_key_state`) |
| KiriKiri | `SDL_GameControllerGetButton` | poll | N/A — dead in libretro (no controller opened) |
| KiriKiri | TJS `onKeyDown`/`onKeyUp` | event | **Y** (shim pushes `SDL_KEYDOWN/UP`) |
| ONScripter | `SDL_GetKeyState` / `SDL_GetMouseState` | poll | **Y** — SDL1.2 `SDL_Private*` update both |
| ONScripter / PON | `SDL_WaitEvent` key events | event | **Y** |
| PONScripter | as ONScripter | — | **Y** |
