#!/bin/sh
# run.sh — render one scene at one resolution, font, size and tint.
#
# Usage:
#   GAME=tsukihime RES=1280x960 FONT="EB Garamond" SIZE=100 TINT=normal \
#     OUT=/tmp/scene.png tools/scene-harness/run.sh
#
# The scene is a game's own graphics plus a short dialogue line, reached
# without playing: ONScripter/PONScripter games get a copy of the script with a
# `goto` to a test label (the game's assets are symlinked, never copied), and
# KiriKiri games use a test program whose first scene already has graphics and
# text. The display is an Xvfb at the requested resolution, so the capture is
# 1:1 with the engine's render.
#
# Environment:
#   GAME   tsukihime | fate | fata | higurashi | cigales   (default tsukihime)
#   RES    WxH, one of the core's Internal Resolution values (default 640x480)
#   FONT   a Font option value (default "EB Garamond")
#   SIZE   Font Size percentage, 50..300 (default 100)
#   TINT   a Font Tint value (default normal)
#   OUT    output PNG (default /tmp/scene.png)
#   DISP   X display to use (default :90)
#   WAIT   seconds to let the scene settle (default 8)
#
# Requires: Xvfb, xdotool, ImageMagick (import), python3, retroarch, the core.

set -eu

GAME=${GAME:-tsukihime}
RES=${RES:-640x480}
FONT=${FONT:-EB Garamond}
SIZE=${SIZE:-100}
TINT=${TINT:-normal}
OUT=${OUT:-/tmp/scene.png}
DISP=${DISP:-:90}
WAIT=${WAIT:-8}

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
CORE=${CORE:-$ROOT/vnovel_libretro.so}
HARNESS_CFG=${HARNESS_CFG:-/home/matcha/dev/fate-harness/ra_x11_noscale.cfg}
OPT=${OPT:-$HOME/.config/retroarch/config/vnovel/vnovel.opt}

[ -f "$CORE" ] || { echo "run: no core at $CORE" >&2; exit 1; }
[ -f "$HARNESS_CFG" ] || { echo "run: no RetroArch config at $HARNESS_CFG" >&2; exit 1; }
[ -f "$OPT" ] || { echo "run: no core options at $OPT" >&2; exit 1; }

# --- scene setup -----------------------------------------------------------
# Each setup sets GAME_DIR and GAME_FILE. ONScripter/PONScripter scenes are a
# symlink farm plus a rewritten script; KiriKiri scenes are the game's own
# archive (the test program's first scene already has graphics and text).

setup_tsukihime() {
	src=/home/matcha/VN/Tsukihime/Tsukihime/Game
	dir=/tmp/scene-tsukihime
	rm -rf "$dir"; mkdir -p "$dir"
	for f in "$src"/*; do
		[ "$(basename "$f")" = nscript.dat ] && continue
		ln -s "$f" "$dir/"
	done
	python3 - "$src/nscript.dat" "$dir/nscript.dat" <<'PY'
import sys
src, dst = sys.argv[1], sys.argv[2]
d = bytes(b ^ 0x84 for b in open(src, 'rb').read())
k = d.find(b'*start\n')
if k < 0:
    sys.exit('no *start in ' + src)
d = d[:k] + b'*start\ngoto *scenetest\n' + d[k + len(b'*start\n'):]
d += b'''
*scenetest
setwindow 18,18, 36,21, 17,17, 0, 4, 22, 1, 1, #aaaaaa, 0, 0, 639, 479
!s0
bg "image\\bg\\bg_19d.jpg",0
repaint
texton
` *pant*  *pant*
` But I'll really be in trouble if I don't get back upstairs.
` || To stay on this floor is worse.
br
` It feels like each second passing is grinding a year off my life. That's how fast I feel 'death' closing in on me.
textshow
repaint
*hold
`@
goto *hold
'''
open(dst, 'wb').write(bytes(b ^ 0x84 for b in d))
PY
	GAME_DIR=$dir; GAME_FILE=$dir/nscript.dat
}

setup_fate() {
	GAME_DIR=/home/matcha/VN/fate-fx
	GAME_FILE=$GAME_DIR/data.xp3
}

setup_fata() {
	# The real game is 394 MB and opens on a "Headphones Recommended" screen
	# that waits for a click. Symlink its files and override only first.ks:
	# the game's own startup.tjs and system/Initialize.tjs run (they set KAG
	# up and process first.ks), then first.ks keeps the game's setup calls and
	# jumps to the prologue's first dialogue scene instead of the
	# notice/logo/title sequence. The archive is untouched. The setup is the
	# game's own and takes ~40 s, so run this one with WAIT=55.
	src="/mnt/largepool/bulk/Software/The House in Fata Morgana"
	dir=/tmp/scene-fata
	rm -rf "$dir"; mkdir -p "$dir"
	for f in "$src"/*; do
		ln -s "$f" "$dir/"
	done
	# The engine's resource override directory is checked before the archive,
	# so first.ks goes there. The setup calls are the game's own (scenario/
	# first.ks, up to the *start|Title label); everything after that label is
	# the notice/logo/title sequence this skips.
	mkdir -p "$dir/.overrides"
	cat > "$dir/.overrides/first.ks" <<'EOF'
[wait time=200]
[loadplugin module=wuvorbis.dll]
[loadplugin module=AdjustMoni.dll]
[call storage="fallen_leaf.ks"]
[call storage="rain.ks"]
[call storage="system_define.ks" target=*system_define]
[call storage="macro.ks"]
[call storage="fallen_leaf.ks"]
[call storage="slider_control.ks"]
[call storage="slider.ks"]
[call storage="systembutton.ks"]
[call storage="HeartBeatPlugin.ks"]
[call storage="flash.ks"]
@iscript
Scripts.execStorage("WearFilmPlugin.tjs");
@endscript
@iscript
Scripts.execStorage("QuakePlugin.tjs");
@endscript
@iscript
Scripts.execStorage("_MotionBlurPlugin.tjs");
@endscript
[call storage="CtrlSkip.ks"]
[ctrlskip enabled=true quickflg=true]
[call storage="highSpeedLabeler.ks"]
[call storage="Macro_Text.ks"]
[call storage="Macro_CharLayers.ks"]
[call storage="Macro_Layers.ks"]
[call storage="Macro_TimeEffect.ks"]
[call storage="Macro_APimage.ks"]
[call storage="macros_sprites.ks"]
[call storage="achievements.ks"]
[clearvar]
[eval exp="sf.showpagebreak = 1"]

*test
; Jump to the game's first dialogue scene. The setup above is the game's own
; (scenario/first.ks, up to the *start|Title label); this skips the notice,
; logo and title screens and lands on the prologue's first line.
[jump storage="prologue.ks"]
EOF
	GAME_DIR=$dir; GAME_FILE=$dir/data.xp3
}

setup_cigales() {
	setup_pon_scene "/home/matcha/VN/Le sanglot des cigales" /tmp/scene-cigales
}

setup_cigales_r() {
	setup_pon_scene "/home/matcha/VN/Higu5678" /tmp/scene-cigales-r
}

# PONScripter scene: symlink the game, rewrite pscript.dat (XOR 0x84, UTF-8 with
# a BOM) with a goto to a test label that draws one of the game's own
# backgrounds and a line of text.
setup_pon_scene() {
	src=$1
	dir=$2
	rm -rf "$dir"; mkdir -p "$dir"
	for f in "$src"/*; do
		[ "$(basename "$f")" = pscript.dat ] && continue
		ln -s "$f" "$dir/"
	done
	python3 - "$src/pscript.dat" "$dir/pscript.dat" <<'PY'
import sys
src, dst = sys.argv[1], sys.argv[2]
d = bytes(b ^ 0x84 for b in open(src, 'rb').read())
k = d.find(b'*start')
if k < 0:
    sys.exit('no *start in ' + src)
e = d.find(b'\n', k)
d = d[:e + 1] + b'goto *scenetest\n' + d[e + 1:]
d += b'''
*scenetest
setwindow 31,16,22,16,26,26,0,2,20,1,1,#ffffff,0,0,639,479
!s0
bg ":c;BMP\\Efe\\si_meakasi.bmp",2
repaint
texton
^-!-~n~ It feels like each second passing is grinding a year off my life. That's how fast I feel 'death' closing in on me.
textshow
repaint
*hold
`@
goto *hold
'''
open(dst, 'wb').write(bytes(b ^ 0x84 for b in d))
PY
	GAME_DIR=$dir; GAME_FILE=$dir/pscript.dat
}

case "$GAME" in
	tsukihime)  setup_tsukihime ;;
	fate)       setup_fate ;;
	fata)       setup_fata ;;
	cigales)    setup_cigales ;;
	cigales-r)  setup_cigales_r ;;
	*) echo "run: unknown GAME: $GAME" >&2; exit 1 ;;
esac

[ -f "$GAME_FILE" ] || { echo "run: no game file at $GAME_FILE" >&2; exit 1; }

# --- core options ----------------------------------------------------------
cp "$OPT" /tmp/vnovel.opt.run.bak
restore() { cp /tmp/vnovel.opt.run.bak "$OPT"; }
trap restore EXIT INT TERM
sed -i \
	-e "s|^vnovel_font_face = .*|vnovel_font_face = \"$FONT\"|" \
	-e "s|^vnovel_font_size = .*|vnovel_font_size = \"$SIZE\"|" \
	-e "s|^vnovel_internal_resolution = .*|vnovel_internal_resolution = \"$RES\"|" \
	-e "s|^vnovel_font_tint = .*|vnovel_font_tint = \"$TINT\"|" \
	"$OPT"

# --- display ---------------------------------------------------------------
RA_PID=""; XVFB_PID=""
# A failed earlier run can leave its Xvfb behind; a second Xvfb on the same
# display then fails and the capture reads the stale screen.
pkill -f "Xvfb $DISP " 2>/dev/null || true
sleep 1
Xvfb "$DISP" -screen 0 "${RES}x24" +extension GLX +render -nolisten tcp \
	>/dev/null 2>&1 &
XVFB_PID=$!
sleep 1
cleanup() {
	kill "$RA_PID" 2>/dev/null || true
	kill "$XVFB_PID" 2>/dev/null || true
}
trap 'cleanup; restore' EXIT INT TERM

# --- run and capture -------------------------------------------------------
LOG="${OUT%.png}.log"
# Run from the game directory: the KiriKiri engine resolves a loose script
# (startup.tjs, test.ks) against the current directory before the archive, so
# the override only wins when the process starts there.
( cd "$GAME_DIR" && exec env -u WAYLAND_DISPLAY DISPLAY="$DISP" SDL_VIDEODRIVER=x11 \
	KRKRSDL2_DATA_PATH="${KRKRSDL2_DATA_PATH:-$HOME/.config/retroarch/system/vnovel}" \
	retroarch -f -L "$CORE" --appendconfig="$HARNESS_CFG" "$GAME_FILE" \
	>"$LOG" 2>&1 ) &
RA_PID=$!
# Wait for the frontend window before sending input or capturing: the Fata
# Morgana archive is 394 MB and the window appears well after the process does.
i=0
while [ "$i" -lt 30 ]; do
	DISPLAY="$DISP" xdotool search --class retroarch >/dev/null 2>&1 && break
	sleep 1
	i=$((i + 1))
done
# Advance past intro/menu screens when asked (Fata Morgana opens on a
# "Headphones Recommended" screen that waits for a click).
if [ -n "${INPUT:-}" ]; then
	i=0
	while [ "$i" -lt "$WAIT" ]; do
		DISPLAY="$DISP" xdotool key "$INPUT" 2>/dev/null || true
		sleep 2
		i=$((i + 2))
	done
else
	sleep "$WAIT"
fi
DISPLAY="$DISP" import -window root "$OUT" 2>/dev/null ||
	{ echo "run: capture failed; see $LOG" >&2; exit 1; }
echo "run: $GAME $RES $FONT ${SIZE}% $TINT -> $OUT"
