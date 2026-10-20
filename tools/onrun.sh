#!/bin/bash
# One run of an ONScripter/PONScripter fixture under the core, on Xvfb :97.
#
# WHY THIS EXISTS
#
# Every ad-hoc launch of the core has to get four things right, and each one
# took a session to learn:
#
#   1. DISPLAY=:97 WITH WAYLAND_DISPLAY/WAYLAND_SOCKET UNSET. Setting
#      DISPLAY alone is not enough - the core's SDL prefers Wayland when the
#      variables are present and then never reaches the Xvfb server the
#      screenshots are taken from. `env -u` (not `= ""`) is what the other
#      launchers here use.
#   2. The ROM path ABSOLUTE. libretro.cpp derives the per-game save directory
#      from the ROM's parent path, so a relative path from inside the fixture
#      directory resolves the saves to `saves/vnovel/./` and the script search
#      to `./` - the engine then reports "script error" and writes nothing,
#      while looking like a rendering failure.
#   3. The core path absolute, for the same reason in reverse.
#   4. setsid + disown with stdin from /dev/null, or the process dies with the
#      shell that started it and no log is written at all.
#
# krun.sh does the same job for the KiriKiri game; this is the ONScripter/PON
# equivalent, and the fixtures are the same ones tests/run_tests.sh uses.
#
# Usage: onrun.sh <tag> <secs> <fixture-dir> [env-or-args...]
#   onrun.sh fx 6 tests/ons/03_text
#   onrun.sh fx 6 tests/ons/03_text KRKRSDL2_TEXT_PAGE=1
#
# The log lands at /tmp/<tag>.log and the engine's artifacts (snapshots,
# plane dumps) in <save root>/<fixture basename>/. Always run tools/kstop.sh
# afterwards and confirm it prints "stopped".
set -u
tag=$1; secs=$2; fixture=$3; shift 3
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/.." && pwd)"

"$here/kstop.sh" >/dev/null || { echo "could not clear retroarch"; exit 1; }

if [ ! -f "$fixture/nscript.dat" ] && [ ! -f "$fixture/pscript.dat" ]; then
	echo "no fixture script in $fixture (run from the repo root?)" >&2
	exit 1
fi

rom=$(cd "$fixture" && pwd)/$( [ -f "$fixture/nscript.dat" ] && echo nscript.dat || echo pscript.dat )
log=/tmp/$tag.log
: > "$log"

setsid env -u WAYLAND_DISPLAY -u WAYLAND_SOCKET DISPLAY=:97 \
	ONS_TEST_WIDTH=640 ONS_TEST_HEIGHT=480 \
	"$@" retroarch -L "$root/vnovel_libretro.so" "$rom" >> "$log" 2>&1 </dev/null &
disown
sleep "$secs"

n=$(pgrep -xc retroarch 2>/dev/null); n=${n:-0}
save="$HOME/.config/retroarch/saves/vnovel/$(basename "$fixture")"
echo "running=$n log=$log save=$save"
ls "$save" 2>/dev/null
