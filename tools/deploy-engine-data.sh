#!/bin/sh
# deploy-engine-data.sh — install the core's non-compiled engine data where the
# frontend expects to find it.
#
# Usage: tools/deploy-engine-data.sh <target-dir>
#
# The core is one file, but the KiriKiri engine needs a small set of assets at
# runtime: the TJS KAG parser (this build has no native KAGParser plugin, so the
# polyfill parses every scenario), plugin shims, the plugin override table, and
# a fallback font.
#
# They are DATA, so they belong in the frontend's system ("BIOS") directory:
#
# They go in a per-core subdirectory of the frontend's system ("BIOS")
# directory, the same shape the BIOS directory already uses for cores needing
# several files (PPSSPP/, dolphin-emu/):
#
#     Recalbox / Pi        /recalbox/share/bios/vnovel  -> /mnt/recalbox/bios/vnovel
#     Windows (same share) K:\Recalbox\bios\vnovel
#     RetroArch elsewhere  <system_directory>/vnovel
#
# The core resolves it with no configuration: at startup it asks the frontend
# for its system directory and, if <system_directory>/vnovel/system_polyfill/
# exists, points KRKRSDL2_DATA_PATH there; KRKRSDL2_OVERRIDE_PATH is set to the
# same directory so overrides.txt is found. Absent that, the engine falls back
# to the compiled-in default (/usr/share/krkrsdl2), where a packaged install
# puts its copy.
#
# Two files are load-bearing and easy to break:
#
#   PluginStubs.tjs   must keep its UTF-8 BOM. Without it the engine cannot
#                     decode the file, which aborts the whole polyfill before
#                     any of its globals are installed — every stub goes
#                     missing, and the failure surfaces much later as an
#                     unrelated script error.
#   overrides.txt     must be present, or the engine will not recognise a plugin
#                     as a builtin and will try to load the game's own Windows
#                     PE instead, which aborts startup.
#
# Both are verified below, because a silent failure here looks like a rendering
# bug rather than a missing file.

set -eu

CORE_DIR=$(cd "$(dirname "$0")/.." && pwd)
SRC="$CORE_DIR/deps/krkrsdl2/data"
# The user-selectable font faces live with the other vendored fonts; they are
# DATA for the installed tree, not something compiled into the core.
FONTS_SRC="$CORE_DIR/resources/fonts"

TARGET=${1:-/mnt/recalbox/bios/vnovel}

[ -d "$SRC" ] || { echo "no such source: $SRC" >&2; exit 1; }
# The target may not exist yet on a fresh install, so create it — but only if
# its parent does, so a typo cannot silently create a whole tree somewhere
# unintended.
[ -d "$(dirname "$TARGET")" ] || { echo "no such parent: $(dirname "$TARGET")" >&2; exit 1; }
mkdir -p "$TARGET/system_polyfill"

cp "$SRC/overrides.txt" "$SRC/mona.ttf" "$TARGET/"
cp "$SRC"/system_polyfill/*.tjs "$TARGET/system_polyfill/"

# The selectable faces (core option `vnovel_font_face`) go beside overrides.txt,
# which is the directory the core publishes to the engines as vnovel_font_dir.
# They are NOT embedded in the core, so a missing file here costs the user the
# choice of that face and nothing else — the option then falls back to the game's
# own font. All are SIL OFL 1.1; the licence texts stay in the source tree rather
# than the install to keep the deployed directory to what runs.
SELECTABLE_FACES="ComicNeue-Regular.ttf OpenDyslexic-Regular.otf
                  AtkinsonHyperlegible-Regular.ttf EBGaramond-Variable.ttf
                  BIZUDPGothic-Regular.ttf"
for f in $SELECTABLE_FACES; do
	[ -r "$FONTS_SRC/$f" ] || { echo "missing vendored font: $FONTS_SRC/$f" >&2; exit 1; }
	cp "$FONTS_SRC/$f" "$TARGET/"
done

# Remove a startup.tjs this script used to install. It was a 39-byte placeholder
# (`Debug.message("Hello world!")`) and it is actively harmful: the data
# directory is added as an engine auto-path (ScriptMgnIntf.cpp:675), so a
# startup.tjs here SHADOWS the game's own by base name and the game never
# boots — the engine runs the placeholder, prints "Hello world!" and stops.
# An upgrade must therefore delete it, not merely stop copying it.
rm -f "$TARGET/startup.tjs"

# Verify byte-for-byte: a partial copy is worse than none, because the core
# starts and then fails somewhere unrelated.
echo "verifying:"
fail=0
# The two bundled data files come from the engine data dir; the selectable
# faces come from resources/fonts, where they are vendored. Verified against
# their own source, so a wrong-source copy cannot pass.
check() { # <source-dir> <relative-path>
	if cmp -s "$1/$2" "$TARGET/$2"; then
		printf '  ok       %s\n' "$2"
	else
		printf '  DIFFERS  %s\n' "$2"
		fail=1
	fi
}
check "$SRC" overrides.txt
check "$SRC" mona.ttf
for f in $SELECTABLE_FACES; do check "$FONTS_SRC" "$f"; done
for f in KAGParser.tjs LayerCompat.tjs MenuItem.tjs PluginStubs.tjs Util.tjs; do
	check "$SRC" "system_polyfill/$f"
done

# The BOM check. `cmp` above already proves the bytes match the source; this
# states the requirement explicitly so a future edit to the source that strips
# the BOM is caught here rather than in a distant failure.
if [ "$(head -c 3 "$TARGET/system_polyfill/PluginStubs.tjs" | od -An -tx1 | tr -d ' \n')" = "efbbbf" ]; then
	printf '  ok       PluginStubs.tjs keeps its UTF-8 BOM\n'
else
	printf '  MISSING  UTF-8 BOM on PluginStubs.tjs (load-bearing)\n'
	fail=1
fi

for f in overrides.txt system_polyfill/PluginStubs.tjs; do
	[ -r "$TARGET/$f" ] || { printf '  UNREADABLE %s\n' "$f"; fail=1; }
done

if [ "$fail" -ne 0 ]; then
	echo "engine data is not usable in $TARGET" >&2
	exit 1
fi


echo "installed into $TARGET"
