#!/usr/bin/env bash
# Regression test for Bidder
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

# ── Locate bidlab binary ──────────────────────────────────────────────────────
BIDDER=""
case "$(uname -s)" in
    Linux*) candidates="../bin/linux/Debug/bidlab" ;;
    *)      candidates="../bin/msys2/Debug/bidlab.exe ../bin/Debug/bidlab.exe" ;;
esac
for candidate in $candidates; do
    if [ -f "$candidate" ]; then
        BIDDER="$candidate"
        break
    fi
done

if [ -z "$BIDDER" ]; then
    echo "ERROR: Could not find bidlab Debug binary. Build the project first."
    exit 1
fi
echo "Using bidlab: $BIDDER"

# ── Test parameters ───────────────────────────────────────────────────────────
# -nchecks 32: fast enough for regression
# 3 reps with North=$.1N., East=$Any, South=$NotPass, West=$Any
ARGS="-s 0 -i testinput.txt -o results.csv -nchecks 32 3 .1N. NotPass"

# ── Run ───────────────────────────────────────────────────────────────────────
rm -f results.csv
echo "Running bidlab $ARGS ..."
if [ "$VERBOSE" = "1" ]; then
    "$BIDDER" $ARGS || true
else
    "$BIDDER" $ARGS >/dev/null 2>&1 || true
fi

if [ ! -f results.csv ]; then
    echo "ERROR: bidlab did not produce results.csv"
    exit 1
fi

# ── Baseline mode ─────────────────────────────────────────────────────────────
if [ "$BASELINE_ARG" = "--baseline" ]; then
    cp results.csv baseline.csv
    echo "Baseline saved to baseline.csv ($(wc -l < baseline.csv) lines)"
    exit 0
fi

# ── Compare ───────────────────────────────────────────────────────────────────
if [ ! -f baseline.csv ]; then
    echo "ERROR: No baseline.csv found. Run with --baseline after a verified-good build."
    exit 1
fi

if diff --strip-trailing-cr -u baseline.csv results.csv; then
    echo "Bidder regression test PASSED"
else
    echo "Bidder regression test FAILED (see diff above)"
    exit 1
fi
