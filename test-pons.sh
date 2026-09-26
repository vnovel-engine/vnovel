#!/bin/bash
# Launch RetroArch on a dedicated Hyprland workspace to avoid disrupting the user's session.
# Usage: ./test-pons.sh [game-path]
# Defaults to the French Higurashi pons game.

CORE="/home/matcha/dev/onscripter-en-libretro/onscripter-en_libretro.so"
GAME="${1:-/home/matcha/VN/Le sanglot des cigales/pscript.dat}"
WORKSPACE="special:ra-test"
LOG="/tmp/retro-matcha.log"

# Move to a special workspace so it doesn't interfere with the user's layout
hyprctl dispatch movetoworkspacesilent "special:$WORKSPACE" 2>/dev/null

# Launch RetroArch windowed on the special workspace
HYPRCTL=1 retroarch \
  -L "$CORE" \
  "$GAME" \
  --verbose 2>&1 | tee "$LOG" &

RA_PID=$!

# Wait for the window to appear, then move it to the special workspace
sleep 3
hyprctl clients -j 2>/dev/null | python3 -c "
import json, sys, subprocess
clients = json.load(sys.stdin)
for c in clients:
    if 'RetroArch' in c.get('class', ''):
        addr = c['address']
        subprocess.run(['hyprctl', 'dispatch', 'movetoworkspacesilent', 'special:ra-test', f'address:0x{addr:x}'])
        # Also toggle the special workspace so user can see it
        subprocess.run(['hyprctl', 'dispatch', 'togglespecialworkspace', 'ra-test'])
        break
" 2>/dev/null

echo "RetroArch launched on workspace $WORKSPACE (pid=$RA_PID)"
echo "Toggle with: hyprctl dispatch togglespecialworkspace ra-test"
echo "Log: $LOG"
echo "Kill: kill $RA_PID"
