# Repository working conventions

## Debugging: prefer gdb over probe builds

When investigating engine behaviour, build with debug symbols and drive the
running process with gdb. Probes are a fallback, not the first tool.

```
make debug              # -g -O0 -fno-inline, engine probes OFF (OPTION_DEBUG_PROBES)
```

Runtime diagnostics are not a build option: they are the debug mask, one
`VNOVEL_DEBUG` word sliced by module (see `src/vnovel_debug.h` and the
`Diagnostics` section of CONTROLS.md). The engine's own compile-time probes are
`OPTION_DEBUG_PROBES` (pinned OFF in the Makefile's CMake flags;
`KRKR_CMAKE_EXTRA` can turn them back on).

`-fno-inline` is part of it deliberately: without it, small accessors and
one-line methods (`SetFace`, `GetPoint`, `rect.get_width()`, …) are inlined and
carry no symbol, and a gdb command that calls into one aborts the whole script
with `Cannot evaluate function -- may be inlined`, silently truncating the run.

Reasons, each learned the hard way in this repo:

- Probes carry their own bugs and blind spots. Per-probe `static int __n`
  counters are consumed from process start, so a probe can appear silent even
  while the code it guards runs constantly; and a probe that prints a derived
  field (e.g. `DrawFace`) can be inconclusive when two distinct inputs map to
  the same output value.
- A breakpoint answers questions the probe was not written to ask, at no
  rebuild cost. Rebuilds here take minutes; a gdb command file is edited in
  seconds and rerun immediately.
- gdb resolves object identity, which is what most of these investigations
  actually need: *which* instance, *which* call path, *how many* times. A probe
  that prints counts cannot distinguish "never written" from "written then
  reset"; a hardware watchpoint can.

Notes on making gdb work here:

- `set breakpoint pending on` is still needed, but the reason is not a dlopen'd
  engine: the engine is linked into `vnovel_libretro.so`, and its symbols do not
  exist until that shared object is loaded. There is no second `.so` to attach
  to. See tools/gdb.md.
- A breakpoint on a hot path (anything called per frame or per input poll, e.g.
  `tTJSNI_BaseLayer::GetProvincePixel`, `OnMouseMove`) **stalls the VM** and
  the screen goes black. For per-poll values use a *conditional* breakpoint
  (the condition is evaluated without stopping) or — if the value cannot be
  had from registers — `printf` into the log ("log, don't break"), not a plain
  breakpoint.
- Never call an inlined/void method from a gdb `printf` (e.g.
  `ProvinceImage->GetPoint(x,y)`): if it faults, gdb abandons the whole script
  with `The program being debugged was signaled while in a function called from
  GDB` and silently stops. Read raw fields instead — for an 8bpp province
  image, `((unsigned char*)ProvinceImage->Bitmap->Bits)[y*PitchStep + x]`
  (`tTVPBitmap` in `visual/win32/LayerBitmapImpl.h`; `Bits`, `Width`, `Height`,
  `PitchStep` are public).
- Break on out-of-line functions or on `file:line`. Header-inlined accessors
  (for example `tTJSNI_BaseLayer::SetFace`) have no symbol and cannot be
  broken on; find the out-of-line function that calls them
  (`UpdateDrawFace`, or the native property setter body).
- A subsequent normal build relinks an `-O3` binary with no symbols. Re-run
  `make debug` before any further gdb session.

## Running the game and driving input

The window is an 800x600 Xvfb `:97`. Our core runs via
`tools/krun.sh <tag> <secs> <env…>` / `tools/kstop.sh`; the reference is
`Fate.exe` under `WINEPREFIX=/home/matcha/.wine-fate` on the same display.

`tools/run-sanglot.sh` runs Le sanglot des cigales (PONScripter) the same way
for text-seam work, with the socket and optionally the browser client wired up.
It differs from the two above on purpose: it inherits `DISPLAY` (a game you
cannot see is no use for looking at text) and passes
`--appendconfig` with `pause_nonactive = "false"`, because RetroArch otherwise
stops calling `retro_run` on defocus and the stream goes quiet. It runs the
ORIGINAL install — nothing about the script is patched; reach text with
`ADVANCE=N` (SPACE every N frames), by clicking the window, or through the
socket once text is flowing.

Input is injected with the **`clickd` server that runs under systemd**
(`fate-harness/tools/clickd.py` — a *different file* from this repo's
`tools/clickd.py`, which is the uinput variant). It listens on
`http://192.168.1.66:8777`:

```
POST /click  {"x":..,"y":..,"button":1|3}     click
POST /move   {"x":..,"y":..}                  hover only (no click)
GET  /hovers                                  recent hover positions
GET  /queue                                   pending clicks for an agent
POST /pad    {"btn":"A"|"START"|..,"action":"tap"|..}
```

Coordinates are display pixels. `systemctl --user restart clickd.service` after
editing it.

Traps, each of which cost a session:

- **Never click blind at link coordinates while capturing.** The disclaimer's
  link rows select an answer: "…I do not agree" runs `@eval kag.shutdown()` and
  the game exits, giving a black capture. To *reach* the disclaimer, click a
  fixed safe point mid-window (e.g. 400,250). To *test* a link, click it
  deliberately and expect a state change.
- **A black capture usually means the game exited, or the core is paused** —
  the core pauses on losing focus. Check the log for the script reaching the
  screen, and check the process is alive, before suspecting the renderer.
- The engine only complies once it is actually past the disclaimer: verify
  `Disclaimer.ks` lines appear in the run log before trusting a frame.
- A run that reaches the right screen with no clicks at all will sit on the
  disclaimer forever; the disclaimer is *not* auto-advancing.

## Reference comparison runs

The Wine reference (`Fate.exe`, English install, `WINEPREFIX=/home/matcha/.wine-fate`)
and our core must be driven the same way for frame comparisons to mean
anything. Capture the disclaimer by launching, waiting for the script to reach
the disclaimer, then clicking a fixed safe point (mid-window, well above the
link rows); clicks that land on the link rows select an answer and the script
branches or exits.

## The development socket

`VNOVEL_DEV_PORT=<port>` (in the run's own environment — a trailing argument to
`krun.sh`/`onrun.sh`, since those put it after `env -u`) makes the core listen on
127.0.0.1 with the text stream going out and navigation intents coming in. Client:
the terminal client (`vnovel-client/terminal/vnovel-dev.py <port>`). Records published before a client attaches are
buffered and released on accept, so it is safe to attach late.

Two intents, and the order a reader would use them in:

- `continue` — advance past a text pause (the advance key, pushed through the
  engine's own input path).
- `select N` — take the Nth choice the page offers, as a click would, but through
  the engine rather than the input layers. The engine answers accept/refuse on
  stderr (`devsocket: select N -> …`).

A page reaches the socket when the look-ahead PARSES it, which on KiriKiri is
before the typewriter has built the message layer's link set: `select` then is
refused (correctly) and needs a few `continue`s first. `VNOVEL_DEBUG=krkr:select`
reports which guard refused on the KiriKiri side.
