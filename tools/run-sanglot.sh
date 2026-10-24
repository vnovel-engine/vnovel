#!/bin/bash
# One run of Le sanglot des cigales (PONScripter) under the core, on YOUR
# desktop, with the text-stream socket (and optionally the browser client)
# wired up.
#
# The game is run UNPATCHED — the logo, the scenario-select menu and the cold
# open happen as shipped. Nothing about the script is edited: driving a real
# game is the point, and a patched script stops being the thing under test.
# Reaching text is therefore a matter of INPUT, which this script offers three
# ways to supply (see DRIVING IT below).
#
# WHY THIS EXISTS
#
# A real PONScripter title is the only thing that exercises the text seam
# end-to-end, and running one by hand has traps that each cost a session. This
# owns them:
#
#   * DISPLAY. Inherited from your session by default, NOT forced to an Xvfb
#     display: a game you cannot see is not a useful test. Override with
#     DISPLAY=… if you want it elsewhere.
#   * No pause on defocus. RetroArch's `pause_nonactive` stops the core when the
#     window loses focus, which silently freezes the stream the moment you click
#     to a terminal. Passed off with --appendconfig here rather than by editing
#     your retroarch.cfg.
#   * The ROM path ABSOLUTE, and the cwd inside the install: libretro.cpp
#     derives the per-game save directory from the ROM path, and the engine
#     opens pscript.dat / arc.nsa by NAME relative to the working directory.
#   * The socket port is a TRAILING environment argument (`env … VAR=… retroarch
#     …`), which is the only form that reaches the process.
#
# Usage:
#   tools/run-sanglot.sh                    # 60 s, socket on 8777, your desktop
#   tools/run-sanglot.sh 120                # longer
#   tools/run-sanglot.sh 120 8799           # another socket port
#   WEB=1 tools/run-sanglot.sh              # also start the browser client
#   ADVANCE=90 tools/run-sanglot.sh         # press SPACE every 90 frames
#   DISPLAY=:97 tools/run-sanglot.sh        # force a specific display
#
# DRIVING IT
#
# The unpatched game needs input before it shows text. Three options, in
# increasing fidelity:
#
#   1. ADVANCE=N — the core presses SPACE every N frames (ONS_TEST_AUTODVANCE).
#      A blunt instrument that gets through logos and clickwaits; use it to
#      reach text quickly.   e.g. ADVANCE=90 tools/run-sanglot.sh
#   2. Just click. The window is on your desktop, so the game's own menu works
#      normally while the stream runs in parallel.
#   3. The browser client (WEB=1) plus the terminal client: once text is
#      flowing, `continue` and `select N` drive it through the socket.
#      Both now live in the vnovel-client repository (a DEVWIRE=1 build is
#      required for the socket to exist at all).
#
# Env:
#   CORE     core .so to load (default: the repo's vnovel_libretro.so)
#   GAME     install DIRECTORY (default below); its pscript.dat is the ROM
#   SECS     how long to run (arg 1 wins)
#   PORT     the socket port (arg 2 wins)
#   ADVANCE  frames between SPACE presses, 0 = off (default 0)
#   WEB=1    start the browser client's serve.py (from vnovel-client) and print
#            the URL; CLIENT_DIR names that checkout (default: ../vnovel-client)
#   WEBPORT  browser port (default PORT+1)
#   DRIVE=N  after starting, also run the terminal client and send `continue`
#            every N seconds, so a run produces text without a human clicking.
#            It is the quickest way to see the wire protocol work end to end.
#   TEXT_RUN_LOG  also write the stream to a file (VNOVEL_TEXT_RUN_LOG)
#   DISPLAY  display to use (default: inherited)
#
# Always run tools/kstop.sh afterwards and confirm it prints "stopped".
set -u

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/.." && pwd)"
cfgdir="${XDG_RUNTIME_DIR:-/tmp}/vnovel-sanglot"
mkdir -p "$cfgdir"

SECS="${1:-${SECS:-60}}"
PORT="${2:-${PORT:-8777}}"
CORE="${CORE:-$root/vnovel_libretro.so}"
GAME="${GAME:-/home/matcha/VN/Le sanglot des cigales}"
ADVANCE="${ADVANCE:-0}"

[ -f "$CORE" ] || { echo "no core at $CORE (build it, or set CORE=…)" >&2; exit 1; }
[ -f "$GAME/pscript.dat" ] || { echo "no pscript.dat in $GAME (set GAME=…)" >&2; exit 1; }
[ -e "$GAME/arc.nsa" ] || { echo "no arc.nsa in $GAME" >&2; exit 1; }

"$here/kstop.sh" >/dev/null || { echo "could not clear retroarch" >&2; exit 1; }

# The pause-on-defocus override: an extra config layered on top of yours, so
# your retroarch.cfg is never modified. Without it, clicking to a terminal
# stops the core and the stream goes quiet — which reads as the seam being
# broken rather than the frontend pausing.
override="$cfgdir/no-pause.cfg"
cat > "$override" <<'EOF'
# Written by tools/run-sanglot.sh. Layered over your own config so the core
# keeps running while the window is not focused: the point of this run is to
# watch the stream while working in another window.
pause_nonactive = "false"
video_fullscreen = "false"
EOF

rom="$GAME/pscript.dat"
log="/tmp/sanglot.log"
: > "$log"

# DISPLAY is inherited unless the caller set one: this is meant to be visible.
env_args=("VNOVEL_DEV_PORT=$PORT")
[ -n "${TEXT_RUN_LOG:-}" ] && env_args+=("VNOVEL_TEXT_RUN_LOG=$TEXT_RUN_LOG")
[ "$ADVANCE" != 0 ] && env_args+=("ONS_TEST_AUTODVANCE=$ADVANCE")

# Background children, but NEVER setsid+disown: a disowned process leaves this
# script's process group, so the user cannot ^C the run and is left with stray
# python3/retroarch processes to hunt down. Everything here stays in the script's
# own group and the trap below kills the lot, so ^C (or the script's own exit)
# cleans up.
#
# `cd "$GAME"` in a subshell: the engine opens pscript.dat / arc.nsa by name
# relative to its working directory, and the ROM path is absolute.
pids=()
# Teardown on the way OUT, whatever the reason: a ^C, a kill, or the script
# finishing. Everything this script started is its child, so it stops it -- the
# alternative is what the previous version did (setsid + disown), which orphaned
# the processes and left the user hunting for stray retroarch/python3 with no way
# to ^C them at all. Use a longer SECS if you want the run to outlast the wait.
cleanup() {
	for p in "${pids[@]:-}"; do kill "$p" 2>/dev/null; done
	# The core is killed by name as well: machine-gun a child that ignores TERM.
	"$here/kstop.sh" >/dev/null 2>&1 || true
}
trap cleanup EXIT
trap 'exit 130' INT TERM

( cd "$GAME" && exec env "${env_args[@]}" \
	retroarch --appendconfig "$override" -L "$CORE" "$rom" ) \
	>> "$log" 2>&1 </dev/null &
pids+=($!)

if [ "${WEB:-0}" = 1 ]; then
	webport="${WEBPORT:-$((PORT + 1))}"
	client="${CLIENT_DIR:-$(cd "$root/.." && pwd)/vnovel-client}"
	if [ ! -f "$client/browser/serve.py" ]; then
		echo "WEB=1: no browser client at $client/browser/serve.py" >&2
		echo "  set CLIENT_DIR to the vnovel-client checkout." >&2
	else
		python3 "$client/browser/serve.py" "$PORT" \
			--port "$webport" > /tmp/sanglot-web.log 2>&1 &
		pids+=($!)
		echo "browser client: http://127.0.0.1:$webport/  (log: /tmp/sanglot-web.log)"
	fi
fi

# Optional: drive the game, so a headless run produces text. The terminal client
# is the smallest thing that speaks the protocol and does the pressing itself, on
# one connection -- which matters: the core takes a single consumer, so a driver
# that reconnected per press would lose the stream between them.
if [ "${DRIVE:-0}" != 0 ]; then
	client="${CLIENT_DIR:-$(cd "$root/.." && pwd)/vnovel-client}"
	if [ -f "$client/terminal/vnovel-dev.py" ]; then
		python3 "$client/terminal/vnovel-dev.py" "$PORT" \
			--drive "$DRIVE" --quiet \
			> /tmp/sanglot-drive.log 2>&1 &
		pids+=($!)
		echo "driving: continue every ${DRIVE}s (log: /tmp/sanglot-drive.log)"
	else
		echo "DRIVE=$DRIVE but no terminal client at $client/terminal/vnovel-dev.py" >&2
	fi
fi

# Wait out the run.
end=$(( $(date +%s) + SECS ))
# A ONE-SECOND POLL, not `wait -n`.
#
# `wait -n` looks like the tidy answer -- return on the first child or a signal --
# and it is a trap: it blocks until a child actually EXITS, and the whole point of
# this wait is that the run outlives it (retroarch is alive for the full SECS).
# Measured: the loop sat in `wait -n` until the core died on its own, so the
# script never reached its report and never stopped on time.
#
# A short sleep polls the clock and, just as importantly, is INTERRUPTIBLE: bash
# runs a pending trap as soon as the current command returns, so a ^C is acted on
# within a second rather than after the whole run.
while [ "$(date +%s)" -lt "$end" ]; do
	sleep 1
done

# Gathered while the run is STILL UP: the teardown trap stops retroarch on exit,
# so a count taken after it would always read 0 and the report would be useless.
n="$(pgrep -xc retroarch 2>/dev/null || true)"
[ -n "${n:-}" ] || n=0
running_children="${#pids[@]}"
save="$HOME/.config/retroarch/saves/vnovel/$(basename "$GAME")"
echo "running=$n log=$log socket=127.0.0.1:$PORT display=${DISPLAY:-<inherited>}"
echo "save=$save"

if grep -qm1 '^PAGE' "$log" 2>/dev/null; then
	echo
	echo "first page seen:"
	grep -m1 '^PAGE' "$log" | cut -c1-160
else
	echo
	echo "no PAGE yet: the game is on the logo or the scenario-select menu, which"
	echo "publishes no text. Drive it, then watch:"
	echo "    ADVANCE=90 $0 $SECS $PORT        # SPACE every 90 frames"
	echo "    python3 ${CLIENT_DIR:-$(cd "$root/.." && pwd)/vnovel-client}/terminal/vnovel-dev.py $PORT"
	echo "or just click the window — it is on your desktop."
fi
echo
echo "stream + intents: python3 ${CLIENT_DIR:-$(cd "$root/.." && pwd)/vnovel-client}/terminal/vnovel-dev.py $PORT"
echo "stop it with $here/kstop.sh"
