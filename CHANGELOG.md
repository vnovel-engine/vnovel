# Changelog

Releases of the VNovel libretro core. Each entry says what changed for a user
of the core; per-engine detail is in each engine fork's own `CHANGELOG.md`.

## Unreleased

### Added

- **Page accumulation for the text plane.** KiriKiri publishes its text one
  *line* at a time — each publication is a line-height box — while ONScripter and
  PONScripter publish the whole (screen-sized) text surface at once. A consumer
  that wants "the page" therefore could not just keep the latest publication: on
  KiriKiri that is only the most recent line. The engines can now report where
  each published surface sits in the window (`plane_x`/`plane_y`, via the bridge's
  `vnovel_request_text_plane_splice`), and the core accumulates every publication
  into its own page buffer at that position. One code path: on the two
  page-publishing engines the placement is 0/0 by definition, so the formula
  collapses to "the plane is the page". Off unless a consumer asks, and the
  engines skip the per-publication work when it is off.
- **A regression test for the captured text plane.** The suite asserted nothing
  about the plane, so it stayed green with capture entirely broken — which is
  how the bottom-up-copy crash below reached a real game. The `textplanedump`
  script command (ONS and PON) asks the core to write its copy of the plane to
  a BMP in the save directory, and the harness compares it like any other
  snapshot. `tests/ons/03_text` now asserts the plane's pixels against a
  reference.
- **A `Text Debug View` option** (`off`, `pip`) for working on the capture: a
  translucent picture-in-picture in the bottom-right showing the captured text
  accumulated into a page — the dialogue alone, no artwork. It is a view only,
  and it is what requests page accumulation. See CONTROLS.md.
- **Text look-ahead: a page's text reaches the core before the engine draws it.**
  A walk ahead of the typewriter publishes each page through the bridge as a
  `VNOVEL_RUN_PARSE` run — text, `<pause>`/`<clear>` tokens, `<link:PAYLOAD>`
  labels, and `<<choice>>`/`<<flow>>` where determination ends — on all three
  engines: KiriKiri (a TJS walk hooked into `KAGParser.getNextTag`), ONScripter
  and PONScripter (a shared templated C++ walk in each run loop). Verified on Fate
  (`PAGE(832)` is line 1 of the run log, the first drawn glyph line 2) and on the
  ONS/PON fixtures.
- **The test harness checks that the core contains the KiriKiri engine.** A plain
  `make` after `make KRKR_ENABLE=1` overwrites the same `vnovel_libretro.so` with
  an ONScripter/PONScripter-only build; the KiriKiri test leg then failed with a
  symptom ("engine never reached KAG") that named the scenario rather than the
  cause. It now reports `has no KiriKiri engine — rebuild with KRKR_ENABLE=1`.

### Added

- **Per-game text drivers for the look-ahead.** The greedy walk reads text from
  `ch`/`r`, which is right for a title that writes dialogue inline and is why
  Fate/stay night needs nothing, but a title whose dialogue is MACRO-FORMATTED
  emits no `ch` at all — Fata Morgana's lines arrive as `[emb exp=mp.text]`
  inside a macro body — so the walk published nothing for it. A driver is the
  seam: `vnovelRegisterTextDriver(["<game id>"], fn)` returns text, page
  metadata, a stop, or "not mine", is consulted ONLY for tags the generic walk
  declined (so it can add to the dialect and never change what already works),
  and ships as `scenario/vnovel_textdriver.tjs` in the game's own override tree
  — data, not a branch in a shared loop. Absent, the generic walk applies
  unchanged: a title with no driver is unaffected.
- **A speaker token, `<<speaker:NAME>>`,** emitted inline where the speaker
  changes. In the page's existing token stream rather than a new field in the
  run ABI, so no consumer has to version anything: one that does not know the
  token still parses the page and loses only the name. Fata Morgana is the first
  producer.

### Added

- **KiriKiri games that ship as ONE executable with the archive embedded.** A
  title may distribute as a single .exe with the XP3 appended after its last
  section (the engine's own `XOPT_EMBED_AREA_` marker); such a game has no
  `data.xp3` on disk, so engine detection decided "not KiriKiri" and the run died
  in ONScripter with "no game script found". The core now recognises the layout
  and hands the executable to the engine, which reads the archive IN PLACE — no
  extraction, no copy of a multi-hundred-MB archive. Verified on 'NORA! V1.02',
  which boots to its title screen.

### Fixed

- **The ONScripter/PONScripter look-ahead shredded Japanese text.** The
  inline-command consumer scanned a line BYTE BY BYTE, deciding character length
  by "is this byte >= 0x80" — false for Shift_JIS, where a two-byte character's
  trail byte can be ASCII range (40 CP932 kanji end in `@`, 40 in `\`). A trail
  byte equal to a command character was consumed as that command, the text marker
  was dropped, and the orphaned lead byte was then paired with the next page byte
  and rendered `?`. Measured on 'Boku To Iu Mono' (the first Japanese ONS script
  the walk had seen; every fixture is ASCII, where the assumption holds). The
  walk now tests for commands only at ASCII positions and decodes multibyte
  characters whole, using the engine's own decoder for the length.
- **The scenario auto-path assumed `data.xp3`.** It was `cwd + "/data.xp3"`,
  which names a file that does not exist for an embedded-archive title, so
  opening a scenario failed with `Cannot find storage .../data.xp3`. It now
  derives from the project directory the engine actually selected, which is
  correct for both layouts.
- **The look-ahead's macro-argument stack never rewound.** The parser pushes
  macro arguments at `KAGParser.tjs:1667`, which is not behind the
  `processSpecialTags` gate, but the matching `macropop` pop IS — and the walk
  blanks that gate to see control tags as data. So the walk's stack grew without
  bound and `getMacroTopNoAddRef()` kept returning a stale frame, which broke
  every expression evaluated against `mp` (a macro's own arguments). Generic:
  every macro-using title leaked, not just the one that surfaced it. The walk now
  balances the pop itself.
- **A look-ahead double free on any ONScripter script with a `clickstr`.** The
  walk parses a private handler that *borrows* the live script image
  (`ScriptHandler::borrowFrom`). It correctly shared `script_buffer` and
  `label_info`, and correctly forgot them before destruction — but not
  `clickstr_list`, which borrowFrom also shares (it points into the borrowed
  script). `reset()`, called from the private handler's destructor, does
  `delete[] clickstr_list`, so every walk freed the LIVE handler's clickstring
  list; the engine's later use, or its own destructor, then freed it again:
  `free(): double free detected in tcache 2`, aborting the process. Reproduced
  on Tsukihime (whose `*define` sets `clickstr` before the first walk) and fixed
  by also clearing `clickstr_list` in `unhookBorrowedScript()`. PONScripter is
  unaffected — it stores the clickstring list as a `std::set` it copies.
- **The engine was silently built with its debug probes on.** KiriKiri's probe
  flag is a CMake cache value, so once a run passed
  `KRKR_CMAKE_EXTRA="-DOPTION_DEBUG_PROBES=ON"`, every later build reused the
  cached ON and compiled the engine's ad-hoc stderr probes into a release build
  with nothing in the output saying so. The flag is now pinned `OFF` on every
  configure (`KRKR_CMAKE_EXTRA` still wins if set explicitly).
- **A page is published once, and only if it has text.** The ONScripter and
  PONScripter walk recorded its covered position *after* restoring the
  interpreter, so it stored where the walk entered rather than where it stopped:
  every line inside the page re-entered the walk and re-published the page — up
  to ~50 copies, each one line shorter. The KiriKiri walk published a text-free
  `<<flow>>` page for every control-flow tag it stopped at, putting eight empty
  pages ahead of the real first page during the game's boot. Both fixed; a page
  with no characters is not a page (a `<<choice>>` with no text still is).
- **`TextLookAhead.tjs` had no UTF-8 BOM**, unlike every sibling polyfill. The
  engine reads a BOM-less script as Shift_JIS, so the file's UTF-8 Japanese
  comments were misdecoded and one sequence made the whole file fail to load —
  silently disabling the entire look-ahead. BOM added, matching the others.

- **The ONS/PON look-ahead parses a private copy, not the live engine.** It used
  to run the engine's tokenizer on the running handler, which perturbed the game
  and forced hand-written save/restore of the token buffer, the command type and
  the interpreter position. It now parses a second handler that borrows the
  script image read-only (`borrowFrom`/`unhookBorrowedScript`), the same shape
  KiriKiri's look-ahead already used (`assign(live)`). The live interpreter is
  never touched.
- **The look-ahead consumes the engines' inline text commands.** A text line
  carries control codes the renderer never draws (`@` click, `\` new page,
  `!sd`/`!w<n>`/`!d<n>` waits, `#rrggbb` colour, the encoding's text marker). The
  walk now consumes them the way the renderer does instead of publishing them as
  dialogue (`!sd`, `!d15000` appeared verbatim before), and emits `<pause>`/
  `<clear>` at the reader-facing ones.

- **KiriKiri published each glyph one call too early.** `TVPPresentTextPlane`
  ran *before* the glyph was blended into the surface it describes, and the host
  copies that surface synchronously, so the last glyph of every publication was
  missing — a captured line read garbled while the frame it came from was
  clean. The publish now happens after the blend, on the single path every
  branch of that function reaches.

- **The text plane crashed on a large bitmap.** The engine's bitmaps are
  bottom-up and the pointer it publishes is the *last* row, so copying
  `pitch × height` bytes forward from it read past the end of the allocation.
  Small planes survived by luck; Fate/stay night's prologue segfaulted inside
  `memcpy`. The copy now walks the signed stride a row at a time.
- **The menu bar covered the top of the picture.** It was drawn whenever a game
  asked for a menu, and Fata Morgana does ask — but in KiriKiri that request is
  about a *window* menu bar, which this target has nothing to attach to. The
  reference engine answers it by creating a menu window and never mapping it.
  The bar is now drawn only when the hotkey asks for it, on a key you choose.

### Changed

- **The menu bar's hotkey is a core option** (`Menu Hotkey`), and is **unbound
  by default** — a title that never asks for a menu bar no longer has a key
  bring one up. Replaces the hardcoded F12.
- **The menu is the core's own, and no longer tied to `menu.visible`.** A game's
  window-menu request and this overlay are unrelated; wiring one to the other
  meant the overlay could only be reached if a game happened to ask for a bar.
  `MenuItem.visible` keeps its item-level meaning, which is what the scripts
  actually use.
- A **Session** page reports the running session: the loaded archive and its
  identity, the engine, the era option as set, the build string, where saves
  land, and the override directory with whether it is even present.

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
