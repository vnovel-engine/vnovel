# wrap unit harness

Feeds a string straight into the engine's line-breaking code and prints the
break offsets, with no RetroArch, no Xvfb, no frames. It exists because the
render path is a poor place to debug wrapping: a break that is one cell off
looks like a plausible line of text, and the last attempt to change this logic
shipped a total text-render break that only a full render caught.

## What it links

The engine's own objects (`build/<platform>/deps/onscripter-en/*.ons.o`) plus
the core's support objects, and drives `ONScripterLabel::processBreaks` /
`findNextBreak` directly. Those functions read only:

* `script_h.getStringBuffer()` — the text, injectable via the mutable pointer
  `ScriptHandler::getStringBuffer()` returns;
* `string_buffer_breaks[]` — the per-offset break mask the harness allocates;
* `sentence_font` — geometry (`pitch_xy`, `sub_offset_px`, `ttf_font`).

Nothing else: no `current_page`, no draw loop, no event state. That is why the
harness can construct a bare `ONScripterLabel` (whose constructor does no SDL
work — `initSDL()` is called later, from `loadEnv`) and still exercise the real
code.

## Build

    make -C tests/wrap platform=unix

The harness is not part of `all`; it links against a normal build's objects, so
run `make platform=unix` first.

## Use

    ./tests/wrap/wrap_harness "some text to wrap"
    ./tests/wrap/wrap_harness --cells 38 "some text to wrap"
    ./tests/wrap/wrap_harness --golden tests/wrap/cases/*.txt

Each case prints the input, the break offsets, and the resulting lines, so a
diff against a golden file shows a wrap change as a wrap change rather than as
a screenshot.
