# Render comparisons

Produced by `tools/scene-harness/`.

- `render-resolution-bracket.png` — the same Tsukihime scene at native 640×480,
  800×600, 960×720, 1024×768, 1280×960 and 1600×1200, each downscaled to
  640×480. Higher internal resolutions give crisper glyph edges.
- `render-font-check.png` — the game's own font (left) against EB Garamond
  (right) at 640×480; the different line wrap is the face's metrics.
- `render-gallery-preview.jpg` — a slice of the Tsukihime/Fate matrix across
  resolutions, faces, sizes and tints.

Regenerate with:

```sh
GAMES="tsukihime|fate" RESOLUTIONS="640x480|1280x960|1920x1440" \
  SIZES="100|150" FONTS="EB Garamond|OpenDyslexic" TINTS="normal|strong red" \
  tools/scene-harness/gallery.sh /tmp/gallery
```
