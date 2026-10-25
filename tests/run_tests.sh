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
    
    # The core's 'menu' debug bit (VNOVEL_DEBUG=menu) makes the core print the
    # menu tree it built from a game's resetmenu/insertmenu entries
    # (src/menu.cpp). Silent unless a test declares a menu, so it costs the
    # others nothing, and it is the only way to assert menu STRUCTURE headless
    # -- the harness never opens the menu.
    ONS_TEST_WIDTH="$TEST_WIDTH" ONS_TEST_HEIGHT="$TEST_HEIGHT" VNOVEL_DEBUG=core:menu \
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

# KiriKiri differs from the two above in ways that force its own loop:
#
#  - the game is an XP3 archive, not a single .dat, and it has to be built;
#    build.sh does that (and documents the Shift_JIS requirement);
#  - the engine writes its diagnostics to <save dir>/krkr.console.log, not to
#    stderr, because a script error aborts the process through
#    TVPShowScriptException -> TVPTerminateSync(1) before stderr is flushed;
#  - so the assertion reads that log, not /tmp/nova_test.log.
#
# The assertion is deliberately about the SCENARIO's own text ("KRKR ..."
# labels defined in first.ks), so it fails if KAG never reaches the scenario,
# and not merely if the process starts.
if [ -x "$TESTS_DIR/krkr/text_choice/build.sh" ]; then
    echo ""
    echo "=== KiriKiri Tests ==="
    KRKR_WORK="${KRKR_WORK:-/tmp/vnovel-krkr-test}"
    for test_dir in "$TESTS_DIR"/krkr/*/; do
        [ -x "$test_dir/build.sh" ] || continue
        name=$(basename "$test_dir")
        expect="$test_dir/test.expect"
        echo -n "  krkr/$name ... "

        rm -rf "$KRKR_WORK"
        if ! sh "$test_dir/build.sh" "$KRKR_WORK" >/dev/null 2>&1; then
            echo "FAIL (fixture build)"
            FAIL=$((FAIL + 1))
            continue
        fi

        # The core under test must actually contain the KiriKiri engine.
        #
        # Every build has it, so a core without it is a stale artifact from
        # before the engine was always-on rather than a differently-configured
        # build. It would otherwise fail with "engine never reached KAG" - a
        # symptom that names the scenario and sends you looking at the fixture,
        # the script and the look-ahead, none of which is the cause. The
        # engine's own entry point is the check: a missing symbol is the real
        # diagnosis.
        if command -v nm >/dev/null 2>&1 && \
           ! nm -C "$CORE" 2>/dev/null | grep -q 'krkr_main'; then
            echo "FAIL ($CORE has no KiriKiri engine - rebuild)"
            FAIL=$((FAIL + 1))
            continue
        fi

        # The engine writes its console log to "<data path>/krkr.console.log"
        # (DebugIntf.cpp:285), and the data path is the -datapath the core
        # passes, i.e. a directory under the frontend's save root. Which one it
        # is depends on how the frontend identified the content, so it is not a
        # fixed path.
        #
        # It is NOT deleted beforehand, and not selected by name: a real game's
        # own krkr.console.log lives under the same root (saves/vnovel/<game>/),
        # and deleting or matching that would both destroy the user's data and
        # pick a log that has nothing to do with this run. Instead the newest
        # log that postdates a stamp file is the one this run wrote.
        stamp="$KRKR_WORK/.run-stamp"
        touch "$stamp"

        # KAG boots in ~10s wall clock and MAX_FRAMES=300 (~5s) would cut it off
        # mid-boot, so this loop does NOT pass --max-frames: the timeout below
        # is the budget, and the assertion is on the log's *content*, which is
        # written before the run ends either way.
        ( cd "$KRKR_WORK" && env \
              KRKRSDL2_DATA_PATH="$TESTS_DIR/../deps/krkrsdl2/data" \
              timeout 40 "$RA" -L "$CORE" data.xp3 --verbose ) \
              2>/tmp/nova_krkr.log || true

        local_log=$(find "$RA_SAVE_ROOT" "$RA_SAVE_ROOT/.." -maxdepth 2 \
                         -name krkr.console.log -newer "$stamp" 2>/dev/null \
                    | while read -r f; do printf '%s %s\n' "$(stat -c%Y "$f")" "$f"; done \
                    | sort -rn | head -1 | cut -d' ' -f2-)

        if grep -q "SIGSEGV\|SIGABRT\|core dumped" /tmp/nova_krkr.log 2>/dev/null; then
            echo "FAIL (crash)"
            FAIL=$((FAIL + 1))
            continue
        fi

        # No log at all means the engine never reached its logging setup, which
        # is a failure in itself: an assertion that silently finds nothing is
        # how a broken fixture passes.
        if [ -z "$local_log" ] || [ ! -s "$local_log" ]; then
            echo "FAIL (no krkr.console.log — engine never reached KAG)"
            FAIL=$((FAIL + 1))
            continue
        fi

        # The log is UTF-16LE with a BOM (written by DebugIntf.cpp). Matching
        # against it directly with grep is unreliable — the file reads as
        # *binary*, and whether a plain `grep -q` reports a match depends on
        # how GNU grep classifies the buffer it happened to read. Convert
        # once, then assert on plain UTF-8 text.
        log_txt="$KRKR_WORK/krkr.log.txt"
        iconv -f UTF-16 -t UTF-8 "$local_log" > "$log_txt" 2>/dev/null \
            || tr -d '\000' < "$local_log" > "$log_txt" 2>/dev/null || true

        # A script error means KAG never ran the scenario; it is reported in
        # the engine's log, with the file and line.
        if grep -q "Script exception raised" "$log_txt" 2>/dev/null; then
            echo "FAIL (script exception — see $local_log)"
            FAIL=$((FAIL + 1))
            continue
        fi

        ok=true
        if [ -f "$expect" ]; then
            while IFS= read -r line; do
                [ -z "$line" ] && continue
                if ! grep -qF "$line" "$log_txt" 2>/dev/null; then
                    echo "FAIL (expected in scenario: $line)"
                    FAIL=$((FAIL + 1))
                    ok=false
                    break
                fi
            done < "$expect"
        fi
        $ok && { echo "PASS (scenario reached)"; PASS=$((PASS + 1)); }
    done
fi

echo ""
echo "=== Summary ==="
echo "  PASS: $PASS"
echo "  FAIL: $FAIL"
[ $FAIL -eq 0 ] && echo "All tests passed." && exit 0 || exit 1
