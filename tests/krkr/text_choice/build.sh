#!/bin/sh
# Build the KiriKiri text/choice fixture archive.
#
# WHY THIS SCRIPT EXISTS
#
# The engine reads script *and* scenario text in Shift_JIS:
#
#   deps/krkrsdl2/CMakeLists.txt:209   TVP_TEXT_READ_ANSI_MBCS
#   deps/krkrsdl2/external/krkrz/base/TextStream.cpp:34
#                            DefaultReadEncoding = TJS_W("Shift_JIS")
#
# so a UTF-8 `first.ks` is read as mojibake and KAG dies somewhere unrelated
# with "Cannot convert given narrow string to wide string" — an error that
# names neither the file nor the encoding. The KAG system scripts and the
# scenario are therefore converted to CP932 here, at pack time, deliberately:
# the checked-in sources stay UTF-8 (readable, diffable, editor-friendly) and
# the archive gets what the engine expects.
#
# The KAG system scripts also have to sit at `scenario/` inside the archive:
# the archive's only auto-path is "<archive>>scenario/"
# (external/krkrz/base/ScriptMgnIntf.cpp:655), and Initialize.tjs looks up
# Config.tjs by BARE NAME (Initialize.tjs:137 Storages.isExistentStorage).
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)

out=${1:-/tmp/krkr-textchoice}
rm -rf "$out"
mkdir -p "$out/scenario"

sys="$root/tools/krkr_test/system"
for f in "$sys"/*.tjs; do
	iconv -f UTF-8 -t CP932 "$f" > "$out/scenario/$(basename "$f")"
done
cp "$sys"/*.png "$sys"/*.asd "$out/scenario/" 2>/dev/null || true

iconv -f UTF-8 -t CP932 "$here/first.ks" > "$out/scenario/first.ks"

# The bootstrap comes from the checked-in file, not a heredoc here: it carries
# the reasoning for the archive layout, and one copy cannot drift from itself.
iconv -f UTF-8 -t CP932 "$here/startup.tjs" > "$out/scenario/startup.tjs"

python3 "$root/tools/xp3pack.py" "$out/data.xp3" "$out"
