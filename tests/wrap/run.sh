#!/bin/bash
# Golden check for the wrap harness.
#
# Runs tests/wrap/wrap_harness over each case file and compares the break
# offsets against the recorded .golden. The point is that a wrap change shows up
# as a diff in offsets rather than as a page that still looks like text — the
# failure mode that let a broken layout change through before.
#
# Usage:
#   ./run.sh              # compare against goldens
#   ./run.sh --golden     # (re)record goldens
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
CORE="$(cd "$HERE/../.." && pwd)"
HARNESS="$CORE/tests/wrap/wrap_harness"
CELLS="${CELLS:-38}"

if [ ! -x "$HARNESS" ]; then
    echo "wrap_harness not built; run: make -C '$CORE' platform=unix wrap-harness"
    exit 2
fi

RECORD=0
[ "${1:-}" = "--golden" ] && RECORD=1

# Only offsets, so the golden does not churn when the printer's formatting
# changes — the decision under test is the break set, nothing else.
extract() {
    grep '^  breaks:' | sed 's/^  breaks: *//; s/ *$//'
}

pass=0; fail=0
for case_file in "$HERE"/cases/*.txt; do
    name="$(basename "$case_file" .txt)"
    golden="$HERE/golden/$name.txt"
    got="$("$HARNESS" --cells "$CELLS" --golden "$case_file" 2>/dev/null | extract)"

    if [ "$RECORD" = 1 ]; then
        mkdir -p "$HERE/golden"
        printf '%s\n' "$got" > "$golden"
        echo "recorded $name"
        continue
    fi

    if [ ! -f "$golden" ]; then
        echo "  $name ... no golden (run --golden)"
        fail=$((fail + 1))
        continue
    fi

    if [ "$got" = "$(cat "$golden")" ]; then
        echo "  $name ... ok"
        pass=$((pass + 1))
    else
        echo "  $name ... FAIL"
        diff <(printf '%s\n' "$(cat "$golden")") <(printf '%s\n' "$got") | sed 's/^/      /'
        fail=$((fail + 1))
    fi
done

echo "wrap: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
