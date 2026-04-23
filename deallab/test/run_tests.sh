#!/usr/bin/env bash
# Regression test for Dealer
# Usage:
#   ./run_tests.sh            — run tests and compare with baseline
#   ./run_tests.sh --baseline — generate a new baseline (after a verified-good build)
#   ./run_tests.sh -v         — run tests with program output visible (not suppressed)
#
# Always passes -s 0 to the binary so results are reproducible.

set -euo pipefail
cd "$(dirname "$0")"

VERBOSE=0
BASELINE_ARG=""
for arg in "$@"; do
    case "$arg" in
        --baseline) BASELINE_ARG="--baseline" ;;
        -v)         VERBOSE=1 ;;
    esac
done

# ── Locate deallab binary ────────────────────────────────────────────────────
DEALER=""
case "$(uname -s)" in
    Linux*) candidates="../bin/linux/Debug/deallab" ;;
    *)      candidates="../bin/msys2/Debug/deallab.exe ../bin/Debug/deallab.exe" ;;
esac
for candidate in $candidates; do
    if [ -f "$candidate" ]; then
        DEALER="$candidate"
        break
    fi
done

if [ -z "$DEALER" ]; then
    echo "ERROR: Could not find deallab Debug binary. Build the project first."
    exit 1
fi
echo "Using deallab: $DEALER"

# ── Helpers ───────────────────────────────────────────────────────────────────
run_test () {
    local label="$1"; shift
    echo "--- $label ---"
    if [ "$VERBOSE" = "1" ]; then
        "$DEALER" -s 0 "$@" || true
    else
        "$DEALER" -s 0 "$@" >/dev/null 2>&1 || true
    fi
}

# ── Run tests ─────────────────────────────────────────────────────────────────
rm -f testresults.txt test1_north.txt test2_north.txt test4_north.txt test5_north.txt test6_north.txt testdetails.csv

run_test "Test 1" -i testinput.txt -p test1 4 1S
{ echo "Test 1:"; echo "======="; cat test1_north.txt; } >> testresults.txt

run_test "Test 2" -i testinput.txt -p test2 4 1S 1S1N
{ echo "Test 2:"; echo "======="; cat test2_north.txt; } >> testresults.txt

run_test "Test 4" -i testinput.txt -p test4 4 1S Any 1S1N Any
{ echo "Test 4:"; echo "======="; cat test4_north.txt; } >> testresults.txt

run_test "Test 5" -i testinput.txt -p test5 4 AK732.Q86.95.A63 Any 1S1N Any
{ echo "Test 5:"; echo "======="; cat test5_north.txt; } >> testresults.txt

run_test "Test 6" -i testinput.txt -p test6 4 AK732.Q86.95.A63 Any Q54.T753.AJ6.Q84 Any
{ echo "Test 6:"; echo "======="; cat test6_north.txt; } >> testresults.txt

run_test "Test A" -i testinput.txt -F testhands.csv -p testdetails 16
{ echo "Test A:"; echo "======="; cat testdetailsSDA.csv; } >> testresults.txt

# ── Baseline mode ─────────────────────────────────────────────────────────────
if [ "$BASELINE_ARG" = "--baseline" ]; then
    cp testresults.txt baseline.txt
    echo "Baseline saved to baseline.txt ($(wc -l < baseline.txt) lines)"
    exit 0
fi

# ── Compare ───────────────────────────────────────────────────────────────────
if [ ! -f baseline.txt ]; then
    echo "ERROR: No baseline.txt found. Run with --baseline after a verified-good build."
    exit 1
fi

if diff --strip-trailing-cr -u baseline.txt testresults.txt; then
    echo "Dealer regression test PASSED"
else
    echo "Dealer regression test FAILED (see diff above)"
    exit 1
fi
