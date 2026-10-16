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

TARGET=${1:-/mnt/recalbox/bios/vnovel}

[ -d "$SRC" ] || { echo "no such source: $SRC" >&2; exit 1; }
# The target may not exist yet on a fresh install, so create it — but only if
# its parent does, so a typo cannot silently create a whole tree somewhere
# unintended.
[ -d "$(dirname "$TARGET")" ] || { echo "no such parent: $(dirname "$TARGET")" >&2; exit 1; }
mkdir -p "$TARGET/system_polyfill"

cp "$SRC/overrides.txt" "$SRC/startup.tjs" "$SRC/mona.ttf" "$TARGET/"
cp "$SRC"/system_polyfill/*.tjs "$TARGET/system_polyfill/"

# Verify byte-for-byte: a partial copy is worse than none, because the core
# starts and then fails somewhere unrelated.
echo "verifying:"
fail=0
for f in overrides.txt startup.tjs mona.ttf \
         system_polyfill/KAGParser.tjs system_polyfill/LayerCompat.tjs \
         system_polyfill/MenuItem.tjs system_polyfill/PluginStubs.tjs \
         system_polyfill/Util.tjs; do
	if cmp -s "$SRC/$f" "$TARGET/$f"; then
		printf '  ok       %s\n' "$f"
	else
		printf '  DIFFERS  %s\n' "$f"
		fail=1
	fi
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
