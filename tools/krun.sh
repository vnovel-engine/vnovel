#!/bin/bash
# One experiment arm for the KiriKiri2/KRKR2 core.
#
# ALWAYS stops any running retroarch before starting, and reports whether
# exactly one is left running afterwards, so a stale instance can never be
# mistaken for the run under test. The log lands at /tmp/<tag>.log.
#
# Usage: krun.sh <tag> <secs> [env-or-args...]
#   krun.sh fresh1 30 KRKR2_ENGINE_COMPAT=2.19 KRKR2_SCRIPT_OVERRIDE=/tmp/ovr
#   krun.sh probe  40 KRKR2_ALPHA_PROBE=1 KRKR2_LAYER_DUMP=/tmp/ldump
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
  "$@" retroarch -L /home/matcha/dev/vnovel-libretro/vnovel_libretro.so \
  data.xp3 >> "$log" 2>&1 </dev/null &
disown
sleep "$secs"
n=$(pgrep -xc retroarch 2>/dev/null); n=${n:-0}
if [ "$n" -ne 1 ]; then echo "WARNING: expected 1 retroarch, found $n"; fi
echo "running=$n log=$log"
