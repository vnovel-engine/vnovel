#!/bin/bash


# Test harness for the vnovel libretro core.
# Runs headless via Xvfb (virtual framebuffer).
#
# Usage:
#   ./run_tests.sh              # run all tests headless
#   ./run_tests.sh --golden     # create/update golden references
#   NO_XVFB=1 ./run_tests.sh    # run with visible window (debugging)

CORE="${CORE:-$(realpath "$(dirname "$0")/../vnovel_libretro.so")}"
TESTS_DIR="$(cd "$(dirname "$0")" && pwd)"
RA="${RA:-retroarch}"
MAX_FRAMES="${MAX_FRAMES:-300}"
TEST_WIDTH="${TEST_WIDTH:-640}"
TEST_HEIGHT="${TEST_HEIGHT:-480}"
# The engine isolates saves per game folder (libretro.cpp derives a per-game
# subdir so two games cannot overwrite each other's save1.dat), so snapshots
# land in <save root>/<game folder>, not one flat directory. RetroArch's save
# root is <savefile_directory>/<library_name> ("vnovel", from the .info).
RA_SAVE_ROOT="${RA_SAVE_ROOT:-$HOME/.config/retroarch/saves/vnovel}"
PASS=0
FAIL=0
GOLDEN=0
XVFB_PID=""

for arg in "$@"; do
    case "$arg" in
        --golden) GOLDEN=1 ;;
    esac
done

# Start Xvfb for headless operation
if [ -z "$NO_XVFB" ] && command -v Xvfb >/dev/null 2>&1; then
    # Redirect Xvfb's own stdout/stderr: it must not inherit the command
    # substitution's pipe, or bash waits for that pipe to close - which Xvfb
    # holds open for its whole life - and the harness hangs before the first
    # test.
    XVFB_PID=$(Xvfb :99 -screen 0 1280x720x24 >/dev/null 2>&1 & echo $!)
    export DISPLAY=:99
    export WAYLAND_DISPLAY=""  # force X11, not Wayland
    sleep 1
fi

cleanup() {
    [ -n "$XVFB_PID" ] && kill "$XVFB_PID" 2>/dev/null || true
}
trap cleanup EXIT

run_test() {
    local name="$1"
    local game="$2"
    local expect_file="$3"

    # `<name>` is "ons/01_basic"; the save folder is the game directory's
    # basename, which is what libretro.cpp derives from the ROM path.
    local RA_SAVE_DIR="$RA_SAVE_ROOT/$(basename "$name")"

    echo -n "  $name ... "

    rm -f "$RA_SAVE_DIR"/*.bmp "$RA_SAVE_DIR"/*.txt 2>/dev/null
    
    ONS_TEST_WIDTH="$TEST_WIDTH" ONS_TEST_HEIGHT="$TEST_HEIGHT" \
    timeout 10 "$RA" -L "$CORE" "$game" --verbose \
        --max-frames="$MAX_FRAMES" 2>/tmp/nova_test.log || true
    
    if grep -q "SIGSEGV\|SIGABRT\|core dumped" /tmp/nova_test.log 2>/dev/null; then
        echo "FAIL (crash)"
        FAIL=$((FAIL + 1))
        return
    fi
    
    if [ -f "$expect_file" ]; then
        while IFS= read -r line; do
            [ -z "$line" ] && continue
            if ! grep -q "$line" /tmp/nova_test.log 2>/dev/null; then
                echo "FAIL (expected: $line)"
                FAIL=$((FAIL + 1))
                return
            fi
        done < "$expect_file"
    fi
    
    local snapshots=$(ls "$RA_SAVE_DIR"/*.bmp 2>/dev/null | wc -l)
    local dumps=$(ls "$RA_SAVE_DIR"/*.txt 2>/dev/null | wc -l)
    
    local refdir="$(dirname "$game")/reference"
    
    if [ "$GOLDEN" -eq 1 ]; then
        mkdir -p "$refdir"
        cp "$RA_SAVE_DIR"/*.bmp "$refdir/" 2>/dev/null
        cp "$RA_SAVE_DIR"/*.txt "$refdir/" 2>/dev/null
        echo "GOLDEN (snapshots:$snapshots dumps:$dumps)"
        PASS=$((PASS + 1))
        return
    fi
    
    if [ -d "$refdir" ] && [ -n "$(ls -A "$refdir" 2>/dev/null)" ]; then
        local ok=true
        for ref_bmp in "$refdir"/*.bmp; do
            [ -f "$ref_bmp" ] || continue
            local bname=$(basename "$ref_bmp")
            if [ ! -f "$RA_SAVE_DIR/$bname" ]; then
                echo "FAIL (missing snapshot: $bname)"
                FAIL=$((FAIL + 1))
                return
            fi
            cmp -s "$ref_bmp" "$RA_SAVE_DIR/$bname" || ok=false
        done
        for ref_txt in "$refdir"/*.txt; do
            [ -f "$ref_txt" ] || continue
            local bname=$(basename "$ref_txt")
            if [ ! -f "$RA_SAVE_DIR/$bname" ]; then
                echo "FAIL (missing dump: $bname)"
                FAIL=$((FAIL + 1))
                return
            fi
            diff -q "$ref_txt" "$RA_SAVE_DIR/$bname" >/dev/null 2>&1 || ok=false
        done
        $ok && echo "PASS (snapshots:$snapshots dumps:$dumps)" || echo "FAIL (mismatch)"
    elif [ $snapshots -gt 0 ] || [ $dumps -gt 0 ]; then
        echo "PASS (snapshots:$snapshots dumps:$dumps, no ref — run with --golden)"
    else
        echo "PASS (no snapshots/dumps)"
    fi
    PASS=$((PASS + 1))
}

echo "=== ONScripter-EN Tests ==="
for test_dir in "$TESTS_DIR"/ons/*/; do
    [ -f "$test_dir/nscript.dat" ] || continue
    name=$(basename "$test_dir")
    expect="$test_dir/test.expect"
    run_test "ons/$name" "$test_dir/nscript.dat" "$expect"
done

echo ""
echo "=== PONScripter Tests ==="
for test_dir in "$TESTS_DIR"/pons/*/; do
    [ -f "$test_dir/pscript.dat" ] || continue
    name=$(basename "$test_dir")
    expect="$test_dir/test.expect"
    run_test "pons/$name" "$test_dir/pscript.dat" "$expect"
done

echo ""
echo "=== Summary ==="
echo "  PASS: $PASS"
echo "  FAIL: $FAIL"
[ $FAIL -eq 0 ] && echo "All tests passed." && exit 0 || exit 1
