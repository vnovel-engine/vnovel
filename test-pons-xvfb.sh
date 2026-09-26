#!/bin/bash
# Isolated test launcher for onscripter-en-libretro PONScripter testing.
# Uses Xvfb (headless X11) so it never touches the user's Wayland session.
# Takes a screenshot after N seconds for visual verification.
#
# Usage: ./test-pons-xvfb.sh [seconds] [game-path]
#   seconds   - how long to run before screenshot (default: 15)
#   game-path - path to pscript.dat (default: French Higurashi)

set -e

CORE="/home/matcha/dev/onscripter-en-libretro/onscripter-en_libretro.so"
GAME="${2:-/home/matcha/VN/Le sanglot des cigales/pscript.dat}"
WAIT="${1:-15}"
RA_HOME="/tmp/ra-pons-test"
LOG="$RA_HOME/retroarch.log"
SHOT="/tmp/ra-pons-screenshot.png"

# Kill any leftovers
pkill -9 retroarch 2>/dev/null || true
pkill -9 Xvfb 2>/dev/null || true
sleep 0.5

# Start Xvfb
Xvfb :99 -screen 0 1280x960x24 >/tmp/xvfb.log 2>&1 &
XVFB_PID=$!
sleep 1

# Launch RetroArch on Xvfb with isolated config
env -u WAYLAND_DISPLAY -u XDG_SESSION_TYPE -u GDK_BACKEND -u QT_QPA_PLATFORM \
    -u CLUTTER_BACKEND -u XDG_BACKEND -u MOZ_ENABLE_WAYLAND \
    DISPLAY=:99 \
    retroarch \
    -L "$CORE" \
    "$GAME" \
    --config "$RA_HOME/retroarch.cfg" \
    --verbose 2>&1 > "$LOG" &

RA_PID=$!
echo "RetroArch PID: $RA_PID (Xvfb PID: $XVFB_PID)"
echo "Waiting ${WAIT}s for engine to initialize and render..."

sleep "$WAIT"

# Take screenshot
DISPLAY=:99 import -window root "$SHOT" 2>/dev/null && echo "Screenshot: $SHOT" || echo "Screenshot failed"

# Check state
if pgrep -x retroarch >/dev/null; then
    echo "STATUS: running"
else
    echo "STATUS: exited"
fi

# Show key log lines
echo "=== Engine ==="
grep -i "Using.*engine\|Geometry\|GL context\|display server\|display driver" "$LOG" | head -5
echo "=== Errors ==="
grep -iE "error|fatal|cannot|crash" "$LOG" | grep -v "unknown command\|define line" | head -5

# Clean up
pkill -9 retroarch 2>/dev/null || true
pkill -9 Xvfb 2>/dev/null || true
