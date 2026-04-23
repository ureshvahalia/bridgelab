#!/usr/bin/env bash
# Master regression test runner for both BRIDGE projects.
# Usage:
#   ./run_tests.sh            — run all tests, report pass/fail
#   ./run_tests.sh --baseline — regenerate baselines for both projects
#   ./run_tests.sh -v         — run tests with program output visible (not suppressed)
#
# Prerequisites:
#   - Bidder Debug build:  bidlab/bin/msys2/Debug/bidlab.exe
#   - Dealer Debug build:  deallab/bin/msys2/Debug/deallab.exe
#
# Both sub-scripts always pass -s 0 to the binary for reproducible results.

set -uo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BASELINE=""
VERBOSE=0
for arg in "$@"; do
    case "$arg" in
        --baseline) BASELINE="--baseline" ;;
        -v)         VERBOSE=1 ;;
    esac
done

EXTRA_ARGS=()
[ -n "$BASELINE" ] && EXTRA_ARGS+=("--baseline")
[ "$VERBOSE" = "1" ] && EXTRA_ARGS+=("-v")

FAILED=0

run_project_tests () {
    local name="$1"
    local dir="$2"
    echo ""
    echo "════════════════════════════════"
    echo "  $name"
    echo "════════════════════════════════"
    if bash "$dir/run_tests.sh" "${EXTRA_ARGS[@]}"; then
        echo "$name: PASSED"
    else
        echo "$name: FAILED"
        FAILED=$((FAILED + 1))
    fi
}

run_project_tests "Bidlab" "$SCRIPT_DIR/bidlab/test"
run_project_tests "Deallab" "$SCRIPT_DIR/deallab/test"

echo ""
if [ "$FAILED" -eq 0 ]; then
    echo "All regression tests PASSED."
else
    echo "$FAILED project(s) FAILED."
    exit 1
fi
