# vnovel Libretro Core

<img src="resources/logo.png" alt="vnovel — Visual Novels: onscripter, ponscripter, kikikiri" width="360">

A unified libretro core for playing Japanese visual novels in RetroArch.
It covers the three major engines — ONScripter-EN, PONScripter and
KiriKiri (KAG) — in a single distribution, detecting which one a game
needs from its script/data file (`nscript.dat`, `pscript.dat`, `data.xp3`).

The core ships with a libretro database (`vnovel.rdb`) that names known
games by the fingerprint of that same file, so a scan in RetroArch shows
*Tsukihime* rather than `nscript.dat`.

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

KiriKiri Z (krkrz, W.Dee / krkrsdl2 project)
  └── deps/krkrsdl2 (this project's submodule; SDL2 port)
  │     ↑ krkrsdl2 fork of github.com/krkrsdl2/krkrsdl2 by Matcha & Nova
  │     └── external/krkrz  (submodule; TVP2/KAG runtime)
  │     │     ↑ fork of github.com/krkrsdl2/krkrz by Matcha & Nova
  │     └── external/FAudio (submodule; XAudio2 reimpl)
  │           ↑ fork of github.com/FNA-XNA/FAudio by Matcha & Nova
  │           ↑ libretro audio capture (SPSC ring) by Matcha & Nova
  │
vnovel (this repo, Matcha & Nova, 2026)
  ↑ SDL→libretro bridge derived from:
  onscripter-libretro (iyzsong, archived)
  ↑ Multi-engine architecture, build system, test suite, and engine
    integration by Matcha & Nova
```

Each forked component carries a `LINEAGE.md` stating its upstream, fork point,
and local commits (see `deps/krkrsdl2/LINEAGE.md`, `…/external/krkrz/LINEAGE.md`,
`…/external/FAudio/LINEAGE.md`).

### Upstream commits

Fork points are recorded against **upstream** only. Our own commit ids are
deliberately not listed anywhere: they go stale on any history rewrite (a
squash has already invalidated them once), and the fork point is the stable
fact. Local changes are documented by subject — see each fork's `CHANGELOG.md`
and its `LINEAGE.md`.

| Component | Upstream | Commit | URL |
|-----------|----------|--------|-----|
| ONScripter-EN | lightbinder/ONScripter-en | `1b5862f` "Font functionality updates" | https://github.com/lightbinder/ONScripter-en/commit/1b5862fdc92c963e9372048c523a2cf8eab95d64 |
| PONScripter | Haeleth (fullsrc tarball) | 20111009-fullsrc | https://sekaitranslates.files.wordpress.com/2011/11/ponscripter-20111009-fullsrc1.tar.bz2 |
| SMPEG | icculus/smpeg | `1e014ee` "Updated email address" (release_0_4_5) | https://github.com/icculus/smpeg/commit/1e014ee6c6f705ad10781a83e303685ff635bfb9 |
| onscripter-libretro (SDL bridge) | iyzsong/onscripter-libretro | `bfd30f7` "libretro: Update version to 0.3." | https://github.com/iyzsong/onscripter-libretro/commit/bfd30f7 |
| krkrsdl2 (KiriKiri SDL2 port) | krkrsdl2/krkrsdl2 | `124bdf8` "cleanup: 0/nullptr/NULL comparison and control flow cleanups" (fork point) | https://github.com/krkrsdl2/krkrsdl2 |
| krkrz (TVP2/KAG runtime) | krkrsdl2/krkrz (`krkrsdl2` branch) | `b11c43a0` "feat: expose BITMAPINFO and BITMAPINFOHEADER on win32" (fork point) | https://github.com/krkrsdl2/krkrz |
| FAudio (XAudio2 reimpl) | FNA-XNA/FAudio | `e787ed7` "22.08" (fork point) | https://github.com/FNA-XNA/FAudio |

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

### Vendored SDL 1.2 patches

The SDL 1.2 source in `deps/sdl/` (vendored from `deps-overlay`) includes
the following patch:

- **`src/timer/SDL_timer.c` — `SDL_AddTimer` fix**: `SDL_SYS_TimerInit()`
  calls `SDL_SetTimerThreaded(1)`, but `SDL_timer_mutex` was only created
  in `SDL_TimerInit` if `SDL_timer_threaded` was already set *before*
  `SDL_SYS_TimerInit` ran. This caused `SDL_AddTimer` to return NULL,
  preventing real timers from being created. The fix creates
  `SDL_timer_mutex` on demand in `SDL_AddTimer` if it is NULL, and removes
  the redundant `if (!SDL_timer_threaded)` check that was blocking
  multiple timers.

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

**See [BUILDING.md](BUILDING.md) — the single build document.** It covers the
four workspaces (Linux native, Windows via mingw, macOS arm64, Raspberry Pi 5
via Recalbox/buildroot), their dependencies, and the failures each one can
produce.

Quick version, Linux:

```bash
git clone --recursive https://github.com/vnovel-engine/vnovel.git
cd vnovel
make
```

The build produces one self-contained `vnovel_libretro.so` — the core, the
runtime and all three engines linked into it, so nothing ships beside it. A host
building the core therefore needs the KiriKiri engine's own CMake dependencies
(SDL2, fontconfig, vorbisfile) available at build time.

## Where the repositories live

The project is mirrored: **GitHub is the mirror and the release channel,
GitLab is the origin.** Both are public, both hold the same history, and both
build on a tag — GitHub publishes the bundles as a Release, GitLab attaches them
to its own.

| Repository | GitHub | GitLab | What it is |
|---|---|---|---|
| vnovel | [vnovel-engine/vnovel](https://github.com/vnovel-engine/vnovel) | [matchaxnb/vnovel](https://gitlab.com/matchaxnb/vnovel) | The core itself (this repository) |
| onscripter-libretro | [vnovel-engine/onscripter-libretro](https://github.com/vnovel-engine/onscripter-libretro) | [matchaxnb/onscripter-libretro](https://gitlab.com/matchaxnb/onscripter-libretro) | `deps-overlay`: the original core's tree plus the vendored deps |
| onscripter-en | [vnovel-engine/onscripter-en](https://github.com/vnovel-engine/onscripter-en) | [matchaxnb/onscripter-en](https://gitlab.com/matchaxnb/onscripter-en) | ONScripter-EN engine, libretro patches |
| ponscripter | [vnovel-engine/ponscripter](https://github.com/vnovel-engine/ponscripter) | [matchaxnb/ponscripter](https://gitlab.com/matchaxnb/ponscripter) | PONScripter engine, libretro patches |
| smpeg | [vnovel-engine/smpeg](https://github.com/vnovel-engine/smpeg) | [matchaxnb/smpeg](https://gitlab.com/matchaxnb/smpeg) | SMPEG 0.4.5 with build fixes |
| krkrsdl2 | [vnovel-engine/krkrsdl2](https://github.com/vnovel-engine/krkrsdl2) | [matchaxnb/krkrsdl2](https://gitlab.com/matchaxnb/krkrsdl2) | KiriKiri SDL2 port, libretro engine-module support |
| krkrz | [vnovel-engine/krkrz](https://github.com/vnovel-engine/krkrz) | [matchaxnb/krkrz](https://gitlab.com/matchaxnb/krkrz) | TVP2/KAG runtime (`krkrsdl2` branch) |
| SDL | [vnovel-engine/SDL](https://github.com/vnovel-engine/SDL) | [matchaxnb/SDL](https://gitlab.com/matchaxnb/SDL) | SDL2 fork used by the engine module |
| FAudio | [vnovel-engine/FAudio](https://github.com/vnovel-engine/FAudio) | [matchaxnb/FAudio](https://gitlab.com/matchaxnb/FAudio) | XAudio2 reimplementation, libretro audio capture |
| vnovel-database | [vnovel-engine/vnovel-database](https://github.com/vnovel-engine/vnovel-database) | [matchaxnb/vnovel-database](https://gitlab.com/matchaxnb/vnovel-database) | Community game fingerprints |
| vnovel-tools-misc | [vnovel-engine/vnovel-tools-misc](https://github.com/vnovel-engine/vnovel-tools-misc) | [matchaxnb/vnovel-tools-misc](https://gitlab.com/matchaxnb/vnovel-tools-misc) | Standalone utilities and smoke launchers |

**Pick the branch that matches where you clone from.**

| Branch | Submodule URLs point at | Use it when |
|---|---|---|
| `main` | GitHub (`github.com/vnovel-engine`) | you clone from GitHub, or want the mirror |
| `main-gitlab` | GitLab (`gitlab.com/matchaxnb`) | you clone from GitLab — including the Recalbox/Buildroot packages |

The two branches are the same release, commit for commit; the only difference
between them is `.gitmodules`.

```bash
git clone --recursive https://github.com/vnovel-engine/vnovel.git      # main
git clone --recursive -b main-gitlab https://gitlab.com/matchaxnb/vnovel.git
```

Cloning `main` from GitLab still works — the submodules then come from GitHub,
which needs no account but does mean one build depends on two forges. Cloning
`main-gitlab` from GitLab keeps the whole tree on one forge, which is why the
Recalbox packages use that branch: a mirror outage or a rate limit on the other
side cannot fail an image build.

The submodules inside `deps/krkrsdl2` (`krkrz`, `SDL`, `FAudio`) use **relative**
URLs, so they resolve against whichever forge you cloned `deps/krkrsdl2` from —
no branch choice needed for them. The dependencies this project does not
maintain (`simde`, `meson_toolchains`, `zlib`) keep absolute upstream URLs.

Every dependency is tagged at the commit this release pins, so a build can name
what it used. Both forges carry every commit, so either resolution builds.

Releases are published on both, from the same tag: the **GitHub Release** holds
the bundles, and GitLab's release page links its own per-platform artifacts.
Linux bundles come as a `.tar.gz` and a `.deb`; Windows as a `.zip`; macOS as a
`.tar.gz`.

## Engine data (the KiriKiri polyfill)

The core is one file, but the KiriKiri engine needs a small set of
**non-compiled** assets at runtime. This build has no native `KAGParser` plugin,
so the TJS polyfill parses every scenario.

They live in a per-core subdirectory of the frontend's system ("BIOS")
directory, the same shape the BIOS directory already uses for cores needing
several files (`PPSSPP/`, `dolphin-emu/`):

| Platform | Directory |
|---|---|
| Recalbox / Pi | `/recalbox/share/bios/vnovel` |
| Windows (same share) | `K:\Recalbox\bios\vnovel` |
| RetroArch elsewhere | `<system_directory>/vnovel` |

```
bios/vnovel/
    overrides.txt             plugin override table (builtin shims + names)
    mona.ttf                  fallback font
    ComicNeue-Regular.ttf     |
    OpenDyslexic-Regular.otf  | the selectable faces for the `Font` core
    AtkinsonHyperlegible-...  | option; a user may replace any of them
    EBGaramond-Variable.ttf   |   (see "Fonts" below)
    BIZUDPGothic-Regular.ttf  |
    system_polyfill/
        KAGParser.tjs         the KAG scenario parser
        LayerCompat.tjs
        MenuItem.tjs
        PluginStubs.tjs       readFile, p.dll, extrans, ...
        Util.tjs
```

(`startup.tjs` must NOT be here: the data directory is an engine auto-path, so a
`startup.tjs` in it shadows the game's own by base name and the game never
boots. `tools/deploy-engine-data.sh` deletes any copy it finds.)

The five selectable faces are installed by `tools/deploy-engine-data.sh` and are
SIL Open Font License; each keeps its licence text in `resources/fonts/`. They
live here rather than inside the core so the binary stays small and a user can
drop in a face of their own under the same name. A missing or unreadable face
costs only the choice of that face — the `Font` option then uses the game's own
font.

This is arranged so that one directory serves every deployment: it ships with
the share (so it travels to the Windows box and the Pi alike), it needs no
per-machine setup, and it does not depend on replacing the core.

**The core resolves it with no configuration.** At startup it asks the frontend
for its system directory and, if `<system_directory>/vnovel/system_polyfill/`
exists, points `KRKRSDL2_DATA_PATH` there. `KRKRSDL2_OVERRIDE_PATH` is set to the
same system directory, so `overrides.txt` is found too. An explicit environment
variable wins over both, and if the `vnovel/` copy is absent the engine falls
back to the compiled-in default (`/usr/share/krkrsdl2`), which is where a
packaged install places it.

Two files are load-bearing and easy to break:

* **`PluginStubs.tjs` must keep its UTF-8 BOM.** Without it the engine cannot
  decode the file, which aborts the whole polyfill before any of its globals are
  installed — every stub goes missing, including `readFile`, and the failure
  surfaces much later as an unrelated script error.
* **`overrides.txt` must be found.** Without it the engine will not recognise a
  plugin as a builtin and will try to load the game's own Windows PE instead,
  which aborts startup. On Windows this previously failed silently because the
  override search path was split on `:`, which every absolute Windows path
  contains.

`tools/deploy-engine-data.sh` installs the set and verifies it byte-for-byte.

## Repository layout

| Path | Contents |
|---|---|
| `src/` | The core's C/C++ sources (`libretro.cpp`, the engine C-ABI wrappers, the bridge, and `sdl-libretro/`). |
| `Makefile`, `Makefile.common` | Build system. Kept at the root so `make` works from a pristine clone. |
| `sdl-vendored.h` | Generated rename manifest, tracked and reviewable. Symbol visibility is source-level (`RETRO_API`, `VNOVEL_EXPORT`), not a version script. |
| `tests/` | Headless test suite (`run_tests.sh`, golden snapshots). |
| `dat/`, `vnovel.rdb` | The game database and its source. |
| `resources/fonts/` | Embedded fallback fonts and their licences. |
| `resources/logo.png` | The project logo (README, release bundles). |
| `tools/` | Python (`make-rdb.py`, `hash-game.py`, `merge-submissions.py`, `validate-submission.py`, `embed_font.py`, `gen_sdl_vendored.py`). |
| `deploy-system.py` | Recalbox deploy (host + core + system entry + database). |
| `private/` | Author's working notes (gitignored — not shipped). |

Standalone utilities that the core does not build against (XP3 archivers, the
XP3/Fate decryption scripts, the NScripter XOR encoder, and the manual
PONScripter smoke launchers) live in a companion repository,
[vnovel-tools-misc](https://github.com/vnovel-engine/vnovel-tools-misc)
([GitLab mirror](https://gitlab.com/matchaxnb/vnovel-tools-misc)).


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

## Game database (`vnovel.rdb`)

RetroArch identifies content by hashing it and looking the hash up in a
per-system `.rdb`. Ours is named after the core (`database = "vnovel"` in
`vnovel_libretro.info`), and it fingerprints each game by the *script/data
file the core already keys engine detection off*:

| Engine | Fingerprinted file | Extension |
|---|---|---|
| ONScripter-EN | `nscript.dat` (`nscript.___` also accepted) | `dat` / `___` |
| PONScripter | `pscript.dat` | `dat` |
| KiriKiri (KAG) | `data.xp3` | `xp3` |

Hashing that one file is what makes the feature work with RetroArch's
scanner as it stands: the scanner iterates the ROM directory's files and
looks each one up, and a visual novel's `nscript.dat`/`pscript.dat`/`data.xp3`
*is* a file in that directory. A record therefore carries
`name`/`description`/`developer`/`franchise`/… plus `rom_name`, `size`,
`crc`, `md5` and `sha1` of that file, so a scan in RetroArch lists the game
as *Tsukihime* instead of `nscript.dat`.

`crc` is the indexed key, so lookups binary-search rather than walking the
whole database (equally fine on a Pi).

### Regenerating

```bash
make rdb    # dat/vnovel.dat -> vnovel.rdb
```

The seed set is small (the games this port has been tested against); adding
a game is appending a `game (...)` block to `dat/vnovel.dat` with the CRCs
of its script file and re-running `make rdb`:

```bash
# crc32 of a file (any tool; coreutils has crc32 on Arch)
crc32 'My Game/pscript.dat'
md5sum 'My Game/pscript.dat'
sha1sum 'My Game/pscript.dat'
```

`tools/make-rdb.py` is a self-contained writer for the libretro db format
(MessagePack records plus a binary-searchable index) so no RetroArch
checkout or `c_converter` build is needed.

### Contributing a game

`tools/hash-game.py` fingerprints a game folder and prints a ready-to-paste
`game (...)` block — the script/data file it hashes is the one the core
launches, and it derives the display name the same way the engines do:

```bash
tools/hash-game.py ~/VN/MyGame/Game
tools/hash-game.py ~/VN/MyGame/Game --append dat/vnovel.dat
```

Name precedence (matching ONScripter's own order):

| Order | Source | Notes |
|---|---|---|
| 1 | `game.id` | sidecar file; present on ONScripter-EN ports |
| 2 | `ons.cfg` `gameid=` | |
| 3 | `;gameid` directive | inside the script header |
| 4 | `versionstr` | `"^Title Ver1.0","^Copyright …"` |
| 5 | `caption` | the window title / About string |

KiriKiri has none of these; the tool falls back to `setup.ini` `AppName=`
(cp932-aware) and then the launcher `.exe` basename.

The `.exe` itself is **not** a source of game names: its PE version resource
names the *engine* (`ProductName = ONScripter-EN`), and the title is not
present in it at all. The title lives in the script, which is the file we
hash anyway.

### Submitting to the shared database

Contributions are collected in a companion repository,
[vnovel-database](https://github.com/vnovel-engine/vnovel-database)
([GitLab mirror](https://gitlab.com/matchaxnb/vnovel-database)), so nobody
needs to open a merge request against the core to add a game:

```bash
tools/hash-game.py ~/VN/MyGame/Game --submit -   # fragment for a merge request
tools/hash-game.py ~/VN/MyGame/Game --email -    # mail body, for mailing it in
```

Paste the fragment into `submissions/` there (or mail it). Maintainers merge
with `tools/merge-submissions.py`, which deduplicates by crc and reports name
conflicts:

```bash
tools/merge-submissions.py dat/vnovel.dat ../vnovel-database/submissions/*.dat \
    -o dat/vnovel.dat && make rdb
```

### Installing for RetroArch

Put `vnovel.rdb` where RetroArch looks for databases:

| Frontend | Path |
|---|---|
| Generic RetroArch | `<RetroArch dir>/database/rdb/vnovel.rdb` (i.e. `~/.config/retroarch/database/rdb/`) |
| Recalbox | `/recalbox/share/system/.config/retroarch/database/rdb/vnovel.rdb` (`deploy-system.py --db-only`) |

Then either scan the ROM directory (RetroArch picks the database from the
core's `.info`, matching the playlist/system named `vnovel`) or add the
`.rdb` explicitly as a database in the manual scanner. A record shown means
the fingerprint matched; a raw filename means it did not — which is the
signal that the file is an unrecorded version.

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

Both ONScripter engines share class names (DirPaths, AnimationInfo,
ScriptHandler, etc.) with different implementations, so each engine's sources
are compiled with a per-engine rename header applied by the compiler
(`-include ons-symbol-prefix.h` / `pons-symbol-prefix.h`, generated by
`tools/gen-engine-symbol-prefix.py`), together with `-fvisibility=hidden`. The
two prefixes keep the definitions apart inside the one linked core. There is no
post-link `objcopy` pass: the earlier `--redefine-syms` approach belonged to the
removed multi-module layout.

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

### Frame synchronization and pacing

The engine runs on its own thread. There is no event semaphore gating it:
`SDL_libretro_event_sem` is created and destroyed but never waited on. The
engine's own event loop supplies the pacing — PONScripter's `eventLoop()`
blocks in `SDL_WaitEvent()`, which polls via `SDL_Delay(10)`, and
`SDL_AddTimer` timers (text/animation) are driven by the 1 ms SDL timer
thread. `retro_run()` pumps input each frame and displays the current
framebuffer; `DUMMY_UpdateRects` is a no-op.

**Single game clock.** All engine time reads `ons_libretro_game_clock_ms()`
(`sdl-libretro/video.c`), which the core dilates. `retro_run()` compares the
effective rate (the Fast-Forward Speed option while FF is on, else 1.0) each
frame and calls `ons_libretro_clock_set()` only when it *changes*, so the
common case is one double compare with no store. Every timed engine event —
ONS/PON `SDL_GetTicks` timers and KiriKiri timers alike — accelerates by the
same factor, and the clock is monotonic (it never jumps backwards when FF
toggles). (The earlier per-callable `SDL_libretro_ff_mult` / `ff_offset`
approach was replaced by this single clock.)

**KiriKiri pacing.** The KiriKiri engine's own fpslimiter is a no-op and its
main loop has no pacing, so `retro_run()`'s KiriKiri path caps it at 60 fps
with an explicit `nanosleep`; FF bypasses that cap. Audio is the other half:
see *Audio submission* below — pacing and audio submission together are what
keep the KiriKiri frame time correct.

**Pause.** RetroArch stops calling `retro_run()` entirely while its menu is
open, so a flag set inside `retro_run()` is never observed by the engine.
Pause is therefore signalled by a heartbeat: `retro_run()` stamps
`SDL_libretro_frontend_hb` (monotonic ms) every frame, and `SDL_Delay()`
blocks while that heartbeat is stale (> 250 ms) or `SDL_libretro_paused` is
set. Because every engine loop reaches `SDL_Delay`, this freezes text,
animations and audio at once. `hb == 0` (before the first `retro_run`) is
treated as running, to avoid a start-up deadlock.

**Reset.** `retro_reset()` stops the engine thread (posting `SDL_QUIT`),
destroys the engine, recreates it and re-applies `last_archive_path` /
`last_save_path`. Both must be recorded for every engine at load time; an
empty archive path makes PONScripter's `DirPaths::get_all_paths()` walk an
empty container and abort.

### Audio submission (do not over-submit)

The core must hand RetroArch **about one video frame of audio per frame**, and
drop anything beyond that. RetroArch's threaded audio pipeline publishes into a
ring of only `AUDIO_PIPE_RING_FRAMES` (3) video frames and **blocks the core in
an eventcount wait** when that ring is full; a core that submits a multi-frame
backlog therefore stalls its own frame loop (observed as 150–220 ms frame times
at ~2 % CPU, a sawtooth, "cured" by fast-forward — which merely puts the ring in
its non-blocking state). A visual novel has no use for late audio, so the
surplus is discarded rather than played. The KiriKiri path accordingly submits
`ceil(rate/60) + slack` frames and advances past the rest; the ONS/PONS path
submits `(freq/60)` rounded up to the mixer's block size. Advertise the rate the
engine actually opened (`spec->freq`), not a hardcoded one.

### `retro_run()` structure

`retro_run()` is a dispatcher, not a monolith. It picks the engine path and
delegates to named helpers, so each concern is testable and readable:

| Helper | Responsibility |
|---|---|
| `stamp_frontend_heartbeat()` | stamp `SDL_libretro_frontend_hb` (pause signal) |
| `read_pointer_options()` | read the pointer core options (all engines) |
| `kiri_frame()` | the whole KiriKiri frame (input, pacing, clock, video, audio) |
| `read_frame_options()` | per-frame core-option re-read (Quick Menu hot-reload) |
| `ons_drive_clock()` | set the single game clock's rate from FF/options |
| `ons_present_frame()` | framebuffer hand-off plus shadow overlays (badges, cursor) |
| `ons_submit_audio()` | drain the mixer into one audio batch |

KiriKiri runs its own thread and frame loop, so `retro_run()` hands it the
whole frame via `kiri_frame()` and returns; ONS/PONS drive theirs inline.

### Crash diagnostics

`retro_init()` installs handlers for `SIGSEGV`/`SIGBUS`/`SIGABRT` that
write the fault address and a backtrace to `/tmp/ons_crash.log` and then
re-raise. Without them a core fault reaches the frontend only as
`Process exitcode: -11`, with no address. Symbolicate with
`aarch64-…-addr2line -f -C -e vnovel_libretro.so`.

### BGM playback and SMPEG priming

Both engines play streamed BGM through SDL_mixer's music hook and treat a
decoder callback that produces no bytes as "track finished", after which
they `stopBGM()` and restart. This is safe for Ogg but **not** for MPEG
audio: the vendored SMPEG returns 0 for the first one or two callbacks
while a stream primes, with `SMPEG_status()` still `SMPEG_PLAYING`. A
bare `== 0` check therefore restarts the BGM before it produces any audio
— silent music plus a runaway restart loop (and eventually a crash from
deleting a sample the audio thread is inside). PONScripter's `mp3callback`
guards the restart on `SMPEG_status(...) != SMPEG_PLAYING` so only a real
end-of-stream restarts the track. Any future change to the callback must
keep that guard.

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

Every submodule resolves to a fork this project maintains, mirrored under
[github.com/vnovel-engine](https://github.com/vnovel-engine) (see
[Where the repositories live](#where-the-repositories-live)). The URLs in
`.gitmodules` name the GitHub side.

| Submodule | Upstream | Fork (GitHub / GitLab) | Description |
|-----------|----------|------------------------|-------------|
| `deps/onscripter-en` | [lightbinder/ONScripter-en](https://github.com/lightbinder/ONScripter-en) | [GH](https://github.com/vnovel-engine/onscripter-en) / [GL](https://gitlab.com/matchaxnb/onscripter-en) | ONScripter-EN engine with libretro patches |
| `deps/ponscripter` | Haeleth fullsrc 20111009 | [GH](https://github.com/vnovel-engine/ponscripter) / [GL](https://gitlab.com/matchaxnb/ponscripter) | PONScripter engine with libretro patches |
| `deps-overlay` | [iyzsong/onscripter-libretro](https://github.com/iyzsong/onscripter-libretro) (archived) | [GH](https://github.com/vnovel-engine/onscripter-libretro) / [GL](https://gitlab.com/matchaxnb/onscripter-libretro) | Original libretro core + vendored deps (SDL, SDL_mixer, bzip2, libmad, libogg, libvorbis, libretro-common) |
| `smpeg` | [icculus/smpeg](https://github.com/icculus/smpeg) | [GH](https://github.com/vnovel-engine/smpeg) / [GL](https://gitlab.com/matchaxnb/smpeg) | SMPEG 0.4.5 with build fixes |
| `deps/krkrsdl2` | [krkrsdl2/krkrsdl2](https://github.com/krkrsdl2/krkrsdl2) | [GH](https://github.com/vnovel-engine/krkrsdl2) / [GL](https://gitlab.com/matchaxnb/krkrsdl2) | KiriKiri SDL2 port |
| `deps/krkrsdl2/external/krkrz` | [krkrsdl2/krkrz](https://github.com/krkrsdl2/krkrz) | [GH](https://github.com/vnovel-engine/krkrz) / [GL](https://gitlab.com/matchaxnb/krkrz) | TVP2/KAG runtime (`krkrsdl2` branch) |
| `deps/krkrsdl2/external/SDL` | [libsdl-org/SDL](https://github.com/libsdl-org/SDL) | [GH](https://github.com/vnovel-engine/SDL) / [GL](https://gitlab.com/matchaxnb/SDL) | SDL2 fork the engine module links |
| `deps/krkrsdl2/external/FAudio` | [FNA-XNA/FAudio](https://github.com/FNA-XNA/FAudio) | [GH](https://github.com/vnovel-engine/FAudio) / [GL](https://gitlab.com/matchaxnb/FAudio) | XAudio2 reimplementation with libretro audio capture |

## Input Mapping

**Canonical source: [`CONTROLS.md`](CONTROLS.md).** It defines the layer-2
contract (physical button → semantic action) and the per-engine keys. The
tables below are a summary; if they ever disagree with `CONTROLS.md`, that
file wins.

### RetroPad (layer 2 — identical on every engine)

| RetroPad | Semantic action |
|---|---|
| D-pad Up/Down/Left/Right | Focus previous / next |
| Left analog stick | Move the virtual pointer |
| A | Advance / Confirm |
| B | Cancel / Back |
| X | Hide Text Window |
| Y | Hold-to-skip / fast-forward (Ctrl; bounded tap) |
| SELECT | Skip (toggle) |
| START | Auto mode (toggle) |
| L / R | History Back / Forward |
| L2 / R2 | History Back / Forward |
| L3 / R3 | Pointer click (left / right) |

### Keyboard remap defaults (`key_remap.h`)

Default **host** keys are chosen to avoid RetroArch hotkey conflicts; the core
translates each to the engine's key before dispatch.

| Function | Default key | Engine key | Configurable |
|----------|-------------|------------|--------------|
| Advance / Confirm | `Enter` | Return | Yes |
| Menu / right-click | `Backspace` | Escape | Yes |
| Skip (toggle) | `s` | `s` | Yes |
| Auto (toggle) | `a` | `a` | Yes |
| Lookback / history | `[` | `h` | Yes |
| Page forward | `]` | `l` | Yes |
| Text speed cycle | `0` | `0` | Yes |
| Fullscreen toggle | `F11` | `f` | Yes |

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
