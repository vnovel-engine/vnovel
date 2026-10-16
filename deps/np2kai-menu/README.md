# np2kai-menu

NP2kai's menu system, vendored, for the core's native menu (see the roadmap
item that replaces the `MenuItem.tjs` stub).

| | |
|---|---|
| Upstream | https://github.com/AZO234/NP2kai |
| Local checkout | `~/dev/NP2kai` |
| Licence | MIT, (c) 2017 AZO (`LICENSE`) |

## What is here, and in what order

Vendored bottom-up, because each layer is useless without the one below it:

| layer | files | state |
|---|---|---|
| `common/` | `rect.{c,h}`, `resize.{c,h}` | vendored, compiles |
| `embed/vramhdl.{c,h}` | the VRAM abstraction the menu draws on | vendored, compiles |
| `embed/fontmng.{c,h}` | the SDL font manager the menu measures text with | vendored, compiles |
| `embed/menubase/` | the menu itself | **not yet vendored** |

Resource blobs `ank10.res` / `ank12.res` are `fontmng`'s built-in font fallback
and are vendored beside it.

## The adaptation layer

`include/` is the whole of the difference between upstream's code and this tree.
The vendored sources under `common/` and `embed/` are **unmodified**, so they can
be diffed against upstream when it moves.

`include/np2kai_types.h` + `include/compiler.h` provide what upstream's
`compiler_base.h` would, without dragging in the rest of it:

- **Types on real fixed widths.** Upstream defines `typedef long int32_t` - the
  platform's `long`, which on LP64 is 64-bit. Including that alongside the real
  `<stdint.h>` is a conflicting redefinition *and* a silent width error, so this
  header maps NP2kai's own spellings (`UINT8`, `UINT32`, `BOOL`, ...) onto
  `<stdint.h>` and deliberately does not define `int32_t`. The vendored files
  only ever use the UINTN/INTN spellings, so nothing is lost.
- **Memory and arithmetic macros** (`ZeroMemory`, `CopyMemory`, `_MALLOC`,
  `MIN`/`MAX`) from `compiler_base.h` and `common/_memory.h`, in their untraced
  variants - tracing is an upstream debugging facility.
- **Trace hooks as no-ops.** Upstream's `sdl/trace.h` compiles them out unless
  `TRACE` is defined, and the SDL target does not define it.
- **`milstr_ncpy`** as a plain bounded copy. Upstream's is multibyte-aware - it
  refuses to cut a kanji in half - but the one use in these files copies a font
  *file name*, which is ASCII. **If this is ever used for game text, this
  shortcut is wrong** and the real `milstr` table has to come with it; the
  comment in the header says so at the definition.

`include/SDL_ttf/SDL_ttf.h` is a one-line forwarder: `fontmng.c` asks for
`<SDL_ttf/SDL_ttf.h>` (NP2kai's SDL1 layout) while this tree keeps the header in
`src/sdl-libretro/`.

## Build requirements

- `USE_SDL=1` for `fontmng.c`: this tree's SDL_ttf is the SDL1-era API
  (`SDL_Color`, `Uint16`) even though the engine side is SDL2 - the same
  arrangement the rest of the core uses.
- `SUPPORT_SDL_TTF` to select the TTF path.
- `SUPPORT_16BPP`/`24BPP`/`32BPP`, matching upstream's SDL target, which enables
  all three depth paths together.
