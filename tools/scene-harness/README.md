# Scene harness

Render one game scene at one resolution, font, size and tint, and build a
gallery across the matrix.

## `run.sh`

```sh
GAME=tsukihime RES=1280x960 FONT="EB Garamond" SIZE=100 TINT=normal \
  OUT=/tmp/scene.png tools/scene-harness/run.sh
```

Games: `tsukihime`, `fate`, `fata`, `cigales`, `cigales-r`.

- `tsukihime` and the PONScripter games get a symlink farm and a rewritten
  script with a `goto` to a test label that draws one of the game's own
  backgrounds and a line of text. The original game files are never modified.
- `fate` uses the FX test program's first scene (graphics + text).
- `fata` runs the real game's intro screen, read-only: no script override.

The display is an Xvfb at the requested resolution, so the capture is 1:1 with
the engine's render. The core options are set for the run and restored after.

## `gallery.sh`

```sh
GAMES="tsukihime fate" RESOLUTIONS="640x480 1280x960" \
  SIZES="100 150" FONTS="EB Garamond" TINTS="normal" \
  tools/scene-harness/gallery.sh /tmp/gallery
```

Writes `<outdir>/index.html`, grouped by game and resolution. The full
cross-product is large (games × resolutions × sizes × fonts × tints), so the
defaults are a representative slice; set the lists to run the whole matrix.
