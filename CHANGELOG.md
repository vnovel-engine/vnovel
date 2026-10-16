# Changelog

Releases of the VNovel libretro core. Each entry says what changed for a user
of the core; per-engine detail is in each engine fork's own `CHANGELOG.md`.

## 0.3.0 — 2026-10-16

Runs pre-2.23-era KiriKiri titles (Fate/stay night first) and adds the
text-plane seam. Per-engine detail is in each engine fork's own `CHANGELOG.md`.

### Added

- **Text-plane rendering.** A game's text is now captured into a surface of its
  own and published alongside the frame, instead of existing only inside the
  picture. All three engines publish through one engine-neutral seam
  (`TVPSetTextPlaneCallback` → `krkr_text_plane_cb` → the core's sink), and
  `KRKRSDL2_TEXT_PAGE=1` presents the captured page in place of the frame. The
  purpose is that text can be restyled, reflowed, scrolled or shown a page at a
  time independently of the artwork — what a reader or a page-based (NDS-class)
  port needs. KiriKiri publishes from the single funnel every glyph is drawn
  through, so no "which layer is the text layer" rule is needed; ONScripter-EN
  and PONScripter publish their sentence text.
- **A native menu.** F12 toggles a menu built from the game's own `MenuItem`
  tree, and `menu.visible` set from script drives it (both directions). The
  menu is the Win32-like `MenuItem`/`Window.menu` contract the scripts already
  expect, backed by a vendored menu implementation. It is composited at the
  present boundary and never composed into the engine's rendering layer, so the
  engine's framebuffer and dirty-rect tracking are left alone.
- **Per-game engine compatibility.** The KiriKiri engine era is chosen per game
  from a table keyed by the game's identity — the crc32 of its `data.xp3`, the
  same fingerprint `vnovel.rdb` and `tools/hash-game.py` use — with `2.19` and
  `modern` presets. Every switch stays individually settable and a level is only
  a named preset, because a published title can carry one backported behaviour
  without the rest. `auto` (the default) consults the table; an explicit
  `2.19`/`modern` overrides it for the session and `KRKRSDL2_COMPAT_<id>` pins a
  single title without a rebuild. Each run logs which source supplied the mask.
- **Save-format compatibility.** The `structconst`/`structcrlf` switches make
  the engine write the save wire format a pre-2.23 title expects; they are bound
  as one format, because a file carrying one era's prefix and the other's
  newline is readable by neither. A Fate/stay night save written by the core is
  byte-identical to the original engine's, and the original engine loads it.
- **Visual-effects shims.** The Windows DLLs a title loads for effects —
  `drawer.dll`, `firespark.dll`, `extrans.dll` — are replaced by built-in CPU
  shims, selected by flag when a script would load a DLL. Scenes that called
  these names previously threw and aborted the scenario.
- **A user resource-override directory** (`<gamedir>/.overrides/`) for patches
  and replacements. Precedence is user > game files > TJS shims, and overrides
  are applied unconditionally as the highest priority.
- **A debug facility.** `make debug` builds a separate tree with the same
  feature set, with debug symbols and the engine's diagnostic probes; the core
  exposes the probe flags and the engine's `-forcelog` important-log sink, so
  compatibility provenance, override disposition and probe output are readable
  from `<datapath>/krkr.console.log`.

### Fixed

- **Clicks after the first were delivered to the wrong layer.** The shift/button
  flags posted with a mouse event were sampled *before* the button transition was
  recorded, so a button-up still claimed the button was held. The layer manager
  consults exactly that bit to release mouse capture, so capture was never
  released and every later click routed to the first layer clicked. Link clicks
  now land on the layer under the cursor.
- **The message text was overpainted by an opaque fill** under the modern
  layer-compositing algorithm. The pre-2006 algorithm is now selectable (the
  `2.19` preset), which drops that fill.
- **FAudio asserted** when the device requested a 1024-frame quantum; the mixer
  accepts it.
- **The ONS/PON shadow buffer was sized wrong**, which could corrupt or drop
  frames at a forced resolution.
- **User overrides never applied.** The override-directory walker widened each
  file name through `std::wstring` and handed it to code expecting `char16_t`,
  so every key collapsed to a stray character and no lookup could match. The
  whole channel was inert.
- A duplicated UTF-8 BOM in `Util.tjs`.
- **The test harness reported false failures**: it resolved the core path
  against the caller's working directory, so running it from anywhere but its
  own directory made RetroArch silently ignore `--libretro`.

### Changed

- `make` and `make debug` carry the same feature set; debug and release objects
  and binaries no longer share a tree.
- KiriKiri input is delivered through the core's own clock, and the engine's
  `-forcelog` logging is enabled per run.

## 0.2.0 — 2026-10-11

First release under the **VNovel** name. The core was called
`onscripter-en-libretro` when it covered ONScripter-EN alone; it now covers the
three engines most visual novels are built with, so the name follows.

### Added

- **Game identification.** The core ships a libretro database (`vnovel.rdb`)
  that names known games by the hash of the script file it already uses for
  engine detection — `nscript.dat`, `pscript.dat` or `data.xp3`. RetroArch
  scans then show *Tsukihime* instead of `nscript.dat`.
  - `tools/hash-game.py` fingerprints a game folder and derives its name the
    way the engines do (`game.id`, `ons.cfg gameid=`, `;gameid`, `versionstr`,
    `caption`; KiriKiri falls back to `setup.ini` and the launcher name).
  - `tools/make-rdb.py` writes the database; `make rdb` regenerates it.
  - Contributions are collected in
    [vnovel-database](https://gitlab.com/matchaxnb/vnovel-database).
- **KiriKiri (KAG) engine**, so Fate/stay night and The House in Fata Morgana
  run alongside the ONScripter and PONScripter titles.
- An **installer** (`install.sh`) that places the core, its runtime, the engine
  modules and the database where a frontend finds them.
- Releases are now built and published by CI for Linux x86_64, Linux aarch64
  and Windows x86_64.

### Changed

- The core, its artifacts, its core-info file and its RetroArch core options
  are named `vnovel` (`onscripter_en_*` options became `vnovel_*`). **Existing
  per-core option settings are not migrated**: RetroArch keeps them under the
  old core name, so the options return to their defaults on first run.
- **Save files move.** RetroArch isolates saves per core using the core's
  library name, so the save directory becomes `…/saves/vnovel/` (previously
  `…/saves/onscripter/`), with a per-game subdirectory inside it. Copy the old
  game folder across to keep your saves.
- The C/C++ sources moved to `src/`; the build still runs `make` from the
  repository root.

### Fixed

- **Windows text rendering** and asset-override handling; saves are now
  durable on storage that drops a create-and-write in one open.
- **MP3 BGM was silent** on both ONScripter-EN and PONScripter: a priming MPEG
  stream legitimately returns no audio for its first buffers, which the
  engines read as "track finished" and restarted the music before it played.
- **PONScripter could crash** (SIGSEGV) while skipping and abort on reset.
- **KiriKiri input was doubled** — a pad press reached the game twice, and
  polled input (`System.getKeyState`) saw keys as released.
- Fast-forward time dilation froze in KiriKiri, stalling timed events while
  frames raced ahead.
- `valid_extensions` now includes `xp3`, matching the core-info file, so a
  frontend will load `data.xp3` directly.

## 0.1.0-pre1 — 2026-10-05

Pre-release. Four-module core (core + runtime + ONScripter-EN and PONScripter
engine modules), built for Linux x86_64/aarch64 and Windows x86_64/i686, with
the KiriKiri engine available separately.
