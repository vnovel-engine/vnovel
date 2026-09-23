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
  onscripter-libretro (iyzsong)
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
[iyzsong/onscripter-libretro](https://github.com/iyzsong/onscripter-libretro)
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

## Submodules

| Submodule | Upstream | Description |
|-----------|----------|-------------|
| `deps/onscripter-en` | [lightbinder/ONScripter-en](https://github.com/lightbinder/ONScripter-en) | ONScripter-EN engine with libretro patches |
| `deps/ponscripter` | [Haeleth fullsrc 20111009](https://sekaitranslates.files.wordpress.com/2011/11/ponscripter-20111009-fullsrc1.tar.bz2) | PONScripter engine with libretro patches |
| `deps-overlay` | [iyzsong/onscripter-libretro](https://github.com/iyzsong/onscripter-libretro) | Original libretro core + vendored deps (SDL, SDL_mixer, bzip2, libmad, libogg, libvorbis, libretro-common) |
| `smpeg` | [icculus/smpeg](https://github.com/icculus/smpeg) | SMPEG 0.4.5 with build fixes |

## Input Mapping

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

All buttons are remappable via RetroArch's Quick Menu → Controls.

## License

GPLv2 (inherited from ONScripter and libmad).
SMPEG is LGPLv2; the combined work is GPLv2.
