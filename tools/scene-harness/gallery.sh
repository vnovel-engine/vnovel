#!/bin/sh
# gallery.sh — run the scene matrix and build an HTML gallery.
#
# Usage:
#   tools/scene-harness/gallery.sh <outdir>
#
# Environment (space-separated lists; override to widen or narrow the matrix):
#   GAMES        default "tsukihime fate"
#   RESOLUTIONS  default "640x480 800x600 960x720 1024x768 1280x960 1440x1080 1600x1200 1920x1440"
#   SIZES        default "100 150 125 75 50"
#   FONTS        default "EB Garamond OpenDyslexic Atkinson Hyperlegible Comic Neue BIZ UDPGothic"
#   TINTS        default "normal"
#
# The full cross-product is large (games x resolutions x sizes x fonts x tints),
# so the defaults are a representative slice. Set the lists to run the whole
# matrix; each render is one Xvfb + one RetroArch run, sequentially.
#
# The gallery is <outdir>/index.html, grouped by game and resolution.

set -eu

OUT=${1:?usage: gallery.sh <outdir>}
HERE=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$OUT"

GAMES=${GAMES:-"tsukihime|fate|fata"}
RESOLUTIONS=${RESOLUTIONS:-"640x480|800x600|960x720|1024x768|1280x960|1440x1080|1600x1200|1920x1440"}
SIZES=${SIZES:-"100|150|125|75|50"}
FONTS=${FONTS:-"EB Garamond|OpenDyslexic|Atkinson Hyperlegible|Comic Neue|BIZ UDPGothic"}
TINTS=${TINTS:-"normal"}

slug() { printf '%s' "$1" | tr ' ' '_' | tr -cd 'A-Za-z0-9_.+-'; }

OLDIFS=$IFS
IFS='|'

for game in $GAMES; do
	case "$game" in
		fata) wait=10; input=Return ;;
		*)    wait=8;  input="" ;;
	esac
	for res in $RESOLUTIONS; do
		for font in $FONTS; do
			for size in $SIZES; do
				for tint in $TINTS; do
					name="$game-$res-$(slug "$font")-$size-$(slug "$tint")"
					[ -f "$OUT/$name.png" ] && continue
					GAME=$game RES=$res FONT=$font SIZE=$size TINT=$tint \
						OUT="$OUT/$name.png" WAIT=$wait INPUT=$input "$HERE/run.sh" ||
						echo "gallery: FAILED $name" >&2
				done
			done
		done
	done
done

# --- HTML ------------------------------------------------------------------
{
	cat <<'EOF'
<!doctype html>
<meta charset="utf-8">
<title>VNovel render matrix</title>
<style>
body { background:#181818; color:#ddd; font:14px system-ui,sans-serif; margin:24px; }
h1 { font-size:20px; } h2 { font-size:16px; margin-top:32px; color:#9cf; }
h3 { font-size:13px; color:#aaa; margin:16px 0 4px; }
.row { display:flex; flex-wrap:wrap; gap:8px; }
figure { margin:0; }
figure img { display:block; width:320px; image-rendering:auto; border:1px solid #333; }
figcaption { font-size:11px; color:#888; margin-top:2px; }
</style>
<h1>VNovel render matrix</h1>
EOF
	for game in $GAMES; do
		echo "<h2>$game</h2>"
		for res in $RESOLUTIONS; do
			echo "<h3>$res</h3><div class=\"row\">"
			for font in $FONTS; do
				for size in $SIZES; do
					for tint in $TINTS; do
						name="$game-$res-$(slug "$font")-$size-$(slug "$tint")"
						[ -f "$OUT/$name.png" ] || continue
						echo "<figure><img src=\"$name.png\" loading=\"lazy\"><figcaption>$font ${size}% $tint</figcaption></figure>"
					done
				done
			done
			echo "</div>"
		done
	done
} > "$OUT/index.html"

IFS=$OLDIFS
echo "gallery: wrote $OUT/index.html"
