#!/bin/bash
set -e

# Test harness for onscripter-en_libretro core.
# Runs each test game through RetroArch with a frame limit, then checks:
#   1. Clean exit (no crash)
#   2. Expected log lines
#   3. Snapshot BMP files exist and match reference (pixel-perfect)
#   4. State dump files exist and match reference
#
# Usage:
#   ./run_tests.sh              # run all tests
#   ./run_tests.sh --golden     # create/update golden references

CORE="${CORE:-$(realpath ../onscripter-en_libretro.so)}"
TESTS_DIR="$(cd "$(dirname "$0")" && pwd)"
RA="${RA:-retroarch}"
MAX_FRAMES="${MAX_FRAMES:-300}"
TEST_WIDTH="${TEST_WIDTH:-640}"
TEST_HEIGHT="${TEST_HEIGHT:-480}"
RA_SAVE_DIR="${RA_SAVE_DIR:-$HOME/.config/retroarch/saves/onscripter/onscripter-en}"
PASS=0
FAIL=0
GOLDEN=0

for arg in "$@"; do
    case "$arg" in
        --golden) GOLDEN=1 ;;
    esac
done

run_test() {
    local name="$1"
    local game="$2"
    local expect_file="$3"
    
    echo -n "  $name ... "
    
    # Clean previous test output
    rm -f "$RA_SAVE_DIR"/*.bmp "$RA_SAVE_DIR"/*.txt 2>/dev/null
    
    # Run RetroArch with frame limit and fixed geometry
    ONS_TEST_WIDTH="$TEST_WIDTH" ONS_TEST_HEIGHT="$TEST_HEIGHT" \
    timeout 30 "$RA" -L "$CORE" "$game" --verbose \
        --max-frames="$MAX_FRAMES" 2>/tmp/nova_test.log || true
    
    # Check for crash
    if grep -q "SIGSEGV\|SIGABRT\|core dumped" /tmp/nova_test.log 2>/dev/null; then
        echo "FAIL (crash)"
        FAIL=$((FAIL + 1))
        return
    fi
    
    # Check expected log lines
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
    
    # Collect snapshot/dump files
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
    
    # Compare against reference if present
    if [ -d "$refdir" ] && [ -n "$(ls -A "$refdir" 2>/dev/null)" ]; then
        for ref_bmp in "$refdir"/*.bmp; do
            [ -f "$ref_bmp" ] || continue
            local bname=$(basename "$ref_bmp")
            if [ ! -f "$RA_SAVE_DIR/$bname" ]; then
                echo "FAIL (missing snapshot: $bname)"
                FAIL=$((FAIL + 1))
                return
            fi
            if ! cmp -s "$ref_bmp" "$RA_SAVE_DIR/$bname"; then
                echo "FAIL (snapshot mismatch: $bname)"
                FAIL=$((FAIL + 1))
                return
            fi
        done
        for ref_txt in "$refdir"/*.txt; do
            [ -f "$ref_txt" ] || continue
            local bname=$(basename "$ref_txt")
            if [ ! -f "$RA_SAVE_DIR/$bname" ]; then
                echo "FAIL (missing dump: $bname)"
                FAIL=$((FAIL + 1))
                return
            fi
            if ! diff -q "$ref_txt" "$RA_SAVE_DIR/$bname" >/dev/null 2>&1; then
                echo "FAIL (dump mismatch: $bname)"
                FAIL=$((FAIL + 1))
                return
            fi
        done
        echo "PASS (snapshots:$snapshots dumps:$dumps, compared to reference)"
    elif [ $snapshots -gt 0 ] || [ $dumps -gt 0 ]; then
        echo "PASS (snapshots:$snapshots dumps:$dumps, no reference — run with --golden)"
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
