#!/bin/bash
# One run of the KiriKiri core, for manual experiments.
#
# ALWAYS stops any running retroarch before starting, and reports whether
# exactly one is left running afterwards, so a stale instance can never be
# mistaken for the run under test. The log lands at /tmp/<tag>.log.
#
# Usage: krun.sh <tag> <secs> [env-or-args...]
#   krun.sh fate 40 KRKRSDL2_DATA_PATH=$PWD/deps/krkrsdl2/data VNOVEL_DEBUG=core:present
#   krun.sh cfg  25 VNOVEL_DEBUG=krkr:province,core:clock   # diagnostics
#
# VNOVEL_DEBUG is one 64-bit mask, 8 bits per module (core/ons/pons, 16 for
# krkr): see src/vnovel_debug.h. A diagnostic that carries a value rather than
# on/off keeps its own variable (VNOVEL_TEXT_RUN_LOG, VNOVEL_PRESENT_STRIPES).
#
# Requires DISPLAY=:97 to exist (Xvfb 800x600x24) and the game data at
# /home/matcha/Fate-Eng-Install/data.xp3. Afterwards ALWAYS run kstop.sh and
# confirm it prints "stopped" before moving on.
set -u
tag=$1; secs=$2; shift 2
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
"$here/kstop.sh" >/dev/null || { echo "could not clear retroarch"; exit 1; }
log=/tmp/$tag.log
: > "$log"
cd /home/matcha/Fate-Eng-Install || exit 1
setsid env -u WAYLAND_DISPLAY -u WAYLAND_SOCKET DISPLAY=:97 \
  "$@" retroarch -L "${CORE:-/home/matcha/dev/vnovel-libretro/vnovel_libretro.so}" \
  data.xp3 >> "$log" 2>&1 </dev/null &
disown
sleep "$secs"
n=$(pgrep -xc retroarch 2>/dev/null); n=${n:-0}
if [ "$n" -ne 1 ]; then echo "WARNING: expected 1 retroarch, found $n"; fi
echo "running=$n log=$log"
