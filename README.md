# ONScripter-EN Libretro Core

A unified libretro core for playing visual novels built with ONScripter-EN
and PONScripter in RetroArch. Supports both engines in a single `.so` with
runtime detection based on script file type.

## Lineage

```
ONScripter (Ogapee / Seung-gyu Park)
  └── ONScripter-EN (insani/mion → lightbinder / Seung Park)
  │     └── deps/onscripter-en (this project's submodule)
  │           ↑ libretro patches by Matcha & Nova
  │
  └── PONScripter (Haeleth / Peter Jolly)
        └── deps/ponscripter (this project's submodule, fullsrc 20111009)
              ↑ libretro patches by Matcha & Nova

onscripter-en-libretro (this repo, Matcha & Nova, 2026)
  ↑ SDL→libretro bridge derived from:
  onscripter-libretro (iyzsong, archived)
  ↑ Multi-engine architecture, build system, test suite, and engine
    integration by Matcha & Nova
```

### Upstream commits

| Component | Upstream | Commit | URL |
|-----------|----------|--------|-----|
| ONScripter-EN | lightbinder/ONScripter-en | `1b5862f` "Font functionality updates" | https://github.com/lightbinder/ONScripter-en/commit/1b5862fdc92c963e9372048c523a2cf8eab95d64 |
| PONScripter | Haeleth (fullsrc tarball) | 20111009-fullsrc | https://sekaitranslates.files.wordpress.com/2011/11/ponscripter-20111009-fullsrc1.tar.bz2 |
| SMPEG | icculus/smpeg | `1e014ee` "Updated email address" (release_0_4_5) | https://github.com/icculus/smpeg/commit/1e014ee6c6f705ad10781a83e303685ff635bfb9 |
| onscripter-libretro (SDL bridge) | iyzsong/onscripter-libretro | `bfd30f7` "libretro: Update version to 0.3." | https://github.com/iyzsong/onscripter-libretro/commit/bfd30f7 |

### Derivative work notice

The following files in this repository are derived from
[iyzsong/onscripter-libretro](https://github.com/iyzsong/onscripter-libretro) (archived)
(commit `bfd30f7`), originally targeting Japanese ONScripter:

- `libretro.cpp` — core lifecycle, heavily modified for multi-engine support
- `Makefile` — rewritten, but build structure inspired by the original
- `Makefile.common` — source lists, restructured for dual-engine build
- `sdl-libretro/audio.c` — SDL audio → RetroArch bridge
- `sdl-libretro/events.c` — RetroPad input → SDL events
- `sdl-libretro/video.c` — SDL surface → RetroArch video
- `sdl-libretro/SDL_libretro.h` — SDL libretro interface declarations
- `sdl-libretro/SDL_image.cpp` — image loading bridge
- `sdl-libretro/SDL_ttf.c` — font rendering bridge
- `sdl-libretro/SDL_config_libretro.h` — SDL configuration

The following files are original work by Matcha & Nova:

- `engine_cabi.h` — C ABI vtable for multi-engine dispatch
- `engine_ons_cabi.cpp` — ONScripter-EN C ABI wrapper
- `engine_pons_cabi.cpp` — PONScripter C ABI wrapper
- `libretro_core_options.h` — RetroArch core options
- `embed_stub.cpp` — stub for embed function
- `tests/` — automated test suite (snapshots, state dumps, golden references)

## Attribution

- **Ogapee (Seung-gyu Park)** — original [ONScripter](https://github.com/ogapee/onscripter) engine
- **Seung Park (lightbinder)** — [ONScripter-EN](https://github.com/lightbinder/ONScripter-en) English fork, based on insani/mion's work
- **Peter Jolly (Haeleth)** — [PONScripter](http://www.geocities.co.jp/Playtown-Yoyo/6130/ponscripter.html) UTF-8 rewrite
- **Song Wei (iyzsong)** — [onscripter-libretro](https://github.com/iyzsong/onscripter-libretro) original libretro core
- **Loki Entertainment Software / Sam Lantinga** — [SMPEG](https://github.com/icculus/smpeg) MPEG-1 decoder
- **Matcha & Nova** — libretro core port, multi-engine architecture, build system, test suite, and engine patches

This port was AI-assisted (Nova, powered by GLM-5.2-FP8).

## Building

```bash
# Clone with submodules
git clone --recursive https://gitlab.com/matchaxnb/onscripter-en-libretro.git

# Build for Linux x86_64
make

# Cross-compile for aarch64 (Raspberry Pi 5)
make platform=aarch64

# Cross-compile for Windows x86_64 (requires mingw)
make platform=mingw

# Debug build
make DEBUG=1
```

## Testing

```bash
cd tests
./run_tests.sh              # run all tests (headless via Xvfb)
./run_tests.sh --golden     # create/update golden reference snapshots
NO_XVFB=1 ./run_tests.sh    # run with visible window (debugging)
```

Tests use `snapshotsave` and `dumpstate` commands (added to both engines
under `__LIBRETRO__`) to capture BMP screenshots and variable/flag state
dumps at specific script points. Output is compared pixel-perfect against
golden references.

## Architecture

```
RetroArch (frontend)
  ↕ libretro API
libretro.cpp (core lifecycle, input, save dir, engine detection)
  ↕ C ABI vtable (engine_cabi.h)
  ├── engine_ons_cabi.cpp → ONScripterLabel (nscript.dat, SJIS)
  └── engine_pons_cabi.cpp → PonscripterLabel (pscript.dat, UTF-8)
  ↕
sdl-libretro/ (SDL→libretro bridge, derived from iyzsong)
  events.c   — RetroPad input → SDL keyboard/mouse events
  audio.c    — SDL audio callback → RetroArch audio_batch_cb
  video.c    — SDL surface → RetroArch video_cb
  ↕
SDL 1.2 (dummy backends only, configured by SDL_config_libretro.h)
  ↕
Engine (runs in SDL thread, symbol-isolated via objcopy --redefine-syms)
  ↕
deps: bzip2, libmad, libogg, libvorbis, SDL_mixer, SMPEG, Freetype
```

### Multi-engine symbol isolation

Both engines share 9 class names (DirPaths, AnimationInfo, ScriptHandler,
etc.) with completely different implementations. Each engine is compiled
as a static library with `-fvisibility=hidden`, then conflicting symbols
(including vtables and typeinfo — 318 total) are renamed via
`objcopy --redefine-syms` with `ons_`/`pons_` prefixes. The renamed
libraries are linked without `--allow-multiple-definition`.

### Error recovery

Both engines call `exit()` on script errors. A single
`libretro_script_error_longjmp()` function catches these via
`setjmp`/`longjmp`, allowing clean return to RetroArch instead of
terminating the process.


### Engine entry points: `executeLabel` vs `eventLoop`

The two engines have fundamentally different execution models:

- **ONScripter-EN**: `executeLabel()` is the complete main loop. It parses
  script, handles events, renders frames, and loops internally. It never
  returns during normal operation.

- **PONScripter**: `executeLabel()` is a *coroutine*. It parses script until
  it hits a `RET_WAIT` state (e.g. `!sd`/setwindow, `btnwait`, `waittimer`),
  then **returns** to the caller. The actual event loop is
  `PonscripterLabel::eventLoop()`, which calls `executeLabel`, processes
  timer/input events via `SDL_WaitEvent`, then calls `executeLabel` again.

`ons_main()` calls `vtable->eventLoop(engine)`, which dispatches to the
correct function for each engine:

| Engine | `eventLoop` wrapper | Actual function |
|--------|--------------------|------------------|
| ONScripter-EN | `ons_eventLoop` | Calls `executeLabel()` (loops internally) |
| PONScripter | `pons_eventLoop` | Calls `eventLoop()` (real event loop) |

Calling `executeLabel` directly for PONScripter causes the engine thread to
exit immediately after the first `RET_WAIT`, resulting in a black screen
with no rendering.

### Frame synchronization

The engine thread and RetroArch's `retro_run()` synchronize via the SDL
event semaphore:

1. Engine calls `SDL_WaitEvent` → `SDL_PumpEvents` → `DUMMY_PumpEvents`
2. `DUMMY_PumpEvents` blocks on `SDL_SemWait(SDL_libretro_event_sem)`
3. `retro_run()` calls `SDL_libretro_PumpEvents()` which pumps input
   and posts `SDL_SemPost(SDL_libretro_event_sem)` to unblock the engine

This throttles the engine to one event loop iteration per `retro_run()`
call (60fps). `DUMMY_UpdateRects` is a no-op — the engine renders freely
to the framebuffer, and `retro_run()` displays whatever is current.

During fast-forward, `DUMMY_PumpEvents` returns immediately (no blocking),
`SDL_GetTicks()` returns accelerated time, and `SDL_AddTimer` creates 1ms
timers. During pause, `DUMMY_PumpEvents` returns immediately and
`retro_run()` doesn't pump events, so the engine idles. Accumulated
timer events are drained on unpause.
### Geometry reporting

`retro_get_system_av_info()` is called by RetroArch before the engine
thread has initialized. The engine's `screen_width`/`screen_height` are
not yet set (PONScripter defaults to 32, ONScripter to 640×48). The
function always returns 640×480 with `max_width`/`max_height` of
1920×1440 to accommodate later resizes. The actual framebuffer
dimensions are passed via `video_cb()` each frame; RetroArch scales
automatically. `RETRO_ENVIRONMENT_SET_GEOMETRY` is not called at runtime
because on Wayland/EGL it triggers a full video driver reinit, producing
a black screen.

## Submodules

| Submodule | Upstream | Description |
|-----------|----------|-------------|
| `deps/onscripter-en` | [lightbinder/ONScripter-en](https://github.com/lightbinder/ONScripter-en) | ONScripter-EN engine with libretro patches |
| `deps/ponscripter` | [Haeleth fullsrc 20111009](https://sekaitranslates.files.wordpress.com/2011/11/ponscripter-20111009-fullsrc1.tar.bz2) | PONScripter engine with libretro patches |
| `deps-overlay` | [iyzsong/onscripter-libretro](https://github.com/iyzsong/onscripter-libretro) (archived) | Original libretro core + vendored deps (SDL, SDL_mixer, bzip2, libmad, libogg, libvorbis, libretro-common) |
| `smpeg` | [icculus/smpeg](https://github.com/icculus/smpeg) | SMPEG 0.4.5 with build fixes |

## Input Mapping

### RetroPad

| RetroPad Button | ONScripter Action |
|---|---|
| D-pad | Navigate menus |
| A | Confirm / Advance text |
| B | Skip text |
| X | Back / Menu |
| Y | Hide text window |
| L | Open save menu (right-click) |
| R | Save |
| L2 | Scroll back |
| R2 | Scroll forward |
| L3 | Reading speed |
| R3 | Quick save |
| Start | Auto mode (toggle) |
| Select | Skip mode (toggle) |


```
┌─────────────────────────────────────────────────────────────┐
│              RETROPAD CONTROLLER MAPPING                    │
├──────────┬──────────────────────────┬───────────────────────┤
│ Button   │ Action                   │ Engine Key            │
├──────────┼──────────────────────────┼───────────────────────┤
│ D-Pad    │ Navigate menus / move    │ Arrow keys            │
│ A        │ Confirm / Advance text   │ Enter                 │
│ B        │ Skip text                │ Space                 │
│ X        │ Menu (right-click)        │ Escape                │
│ Y        │ Hide text window         │ Right Ctrl             │
│ L        │ Open save menu (right-clk)│ Mouse R-Click         │
│ R        │ Save                     │ s                     │
│ L2       │ Scroll back (lookback)   │ Page Up               │
│ R2       │ Scroll forward           │ Page Down             │
│ L3       │ Reading speed            │ Tab                   │
│ R3       │ Quick save               │ q                     │
│ Start    │ Auto mode toggle         │ a                     │
│ Select   │ Skip mode toggle         │ s                     │
├──────────┼──────────────────────────┼───────────────────────┤
│ Keyboard │                          │                       │
├──────────┼──────────────────────────┼───────────────────────┤
│ Enter    │ Confirm / Advance text   │ Enter                 │
│ Backspace│ Menu (right-click)        │ → Escape              │
│ [        │ Lookback (history)       │ → h                   │
│ ]        │ Page forward             │ → l                   │
│ s        │ Skip mode toggle         │ s                     │
│ a        │ Auto mode toggle         │ a                     │
│ 0        │ Text speed cycle         │ 0                     │
│ F11      │ Fullscreen toggle        │ → f                   │
└──────────┴──────────────────────────┴───────────────────────┘
```
All buttons are remappable via RetroArch's Quick Menu → Controls.

### Keyboard

The core provides virtual key remapping to avoid conflicts with RetroArch's
default hotkey bindings. Each engine function can be assigned a key via
core options (Quick Menu → Core Options). The default keys are chosen to
not conflict with RetroArch defaults:

| Function | Default key | Engine key | RetroPad | Configurable |
|----------|------------|------------|----------|-------------|
| Advance text | `Enter` | Return | A | Yes |
| Menu / right-click | `Backspace` | Escape | X / L | Yes |
| Lookback (history) | `[` | h | L2 | Yes |
| Page forward | `]` | l | R2 | No |
| Skip mode toggle | `s` | s | Select | No |
| Auto mode toggle | `a` | a | Start | No |
| Text speed cycle | `0` | 0 | — | No |
| Fullscreen toggle | `F11` | f | — | No |
| Up / Down (menus) | Arrow keys | Up/Down | D-pad | No |
| Left / Right (menus) | Arrow keys | Left/Right | D-pad | No |

How remapping works: when you press the configured key (e.g. `[`), the core
translates it to the engine's expected key (e.g. `h`) before passing it to
the engine. RetroArch only sees the configured key, avoiding hotkey
conflicts.

### RetroArch hotkey conflicts

If you prefer to use the engine's original keys directly, you may need to
rebind conflicting RetroArch hotkeys:

| Engine key | RetroArch hotkey |
|-----------|-----------------|
| `h` | Reset |
| `f` | Toggle fullscreen |
| `l` | Hold fast-forward |
| `Space` | Toggle fast-forward |
| `Escape` | Exit emulator |
| `p` | Pause |
| `k` | Frame advance |
| `m` | Shader next |
| `n` | Shader prev |

Go to Settings → Input → Hotkeys in RetroArch to rebind, or set
"Input Hotkey Enable" to a modifier key so hotkeys only fire when held.

## License
GPLv2 (inherited from ONScripter and libmad).
SMPEG is LGPLv2; the combined work is GPLv2.
