# ONScripter-EN Libretro Core

A libretro core for playing English visual novels built with ONScripter-EN
(e.g. Tsukihime, Kagetsu Tohya) in RetroArch.

## Lineage

```
ONScripter (Ogapee / Seung-gyu Park)
  └── ONScripter-EN (insani/mion → lightbinder / Seung Park)
        └── onscripter-en-libretro (this project, Matcha & Nova, 2026)
              ↑ SDL→libretro bridge ported from:
              onscripter-libretro (original Japanese ONScripter libretro core)
```

This project retargets the original `onscripter-libretro` core from Japanese
ONScripter to ONScripter-EN, adding English text rendering, word wrapping, and
`mode_ext` support. The `deps-overlay` submodule preserves the original core
for reference and provides shared vendored dependencies.

## Attribution

- **Ogapee (Seung-gyu Park)** — original [ONScripter](https://github.com/ogapee/onscripter) engine
- **Seung Park (lightbinder)** — [ONScripter-EN](https://github.com/lightbinder/ONScripter-en) English fork, based on insani/mion's work
- **Loki Entertainment Software / Sam Lantinga** — [SMPEG](https://github.com/icculus/smpeg) MPEG-1 decoder
- **Matcha & Nova** — libretro core port, build system, input mapping, and engine patches

This port was AI-assisted (Nova, powered by Claude).

## Building

```bash
# Build for Linux x86_64
make

# Cross-compile for aarch64 (Raspberry Pi 5)
make platform=aarch64

# Cross-compile for Windows x86_64 (requires mingw)
make platform=mingw

# Debug build
make DEBUG=1
```

Clone with submodules:
```bash
git clone --recursive https://gitlab.com/matchaxnb/onscripter-en-libretro.git
```

## Architecture

```
RetroArch (frontend)
  ↕ libretro API
libretro.cpp (core lifecycle, input, save dir, EXE key detection)
  ↕
sdl-libretro/ (SDL→libretro bridge)
  events.c   — RetroPad input → SDL keyboard/mouse events
  audio.c    — SDL audio callback → RetroArch audio_batch_cb
  video.c    — SDL surface → RetroArch video_cb
  ↕
SDL 1.2 (dummy backends only, configured by SDL_config_libretro.h)
  ↕
ONScripterLabel (engine, runs in SDL thread)
  ↕
deps: bzip2, libmad, libogg, libvorbis, SDL_mixer, SMPEG
```

The ONScripter engine runs in a separate SDL thread. `retro_run()` (called at
60fps by RetroArch) pumps input events and renders the SDL surface to
RetroArch. Error recovery uses `setjmp`/`longjmp` to catch `exit()` calls.

## Submodules

```
deps/onscripter-en  → ONScripter-EN fork with libretro patches
deps-overlay        → original onscripter-libretro core + vendored deps (SDL, SDL_mixer, bzip2, libmad, libogg, libvorbis, libretro-common)
smpeg               → SMPEG 0.4.5 with build fixes
```

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
