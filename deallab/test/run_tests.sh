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

# ── Maj/Min/OMaj/OMin/BMaj/BMin macro-expansion convergence check ────────────
# See majmin/. deallab shares the exact same preprocessor as bidlab (see
# shared/majMinExpand.cpp) and is expected to expand a "$."-shaped auction
# name into the same concrete forked rules, even though deallab has no
# auction/bidding concept itself — it just uses the results as ordinary
# named hand constraints. Not baseline-diffed — diffed against the same
# golden file bidlab's regression test uses.
rm -f majmin/input.txt.expanded.txt
"$DEALER" -i majmin/input.txt -p majmin/conv -G -s 0 1 Any Any >/dev/null 2>majmin/positive.stderr || true
# Debug builds always define DEBUG2, so "[majMinExpand] ..." trace lines are
# expected here — only flag genuinely unexpected stderr output (errors/warnings).
if grep -v '^\[majMinExpand\]' majmin/positive.stderr | grep -q .; then
    echo "Maj/Min regression test FAILED: unexpected stderr output:"
    cat majmin/positive.stderr
    exit 1
fi
if diff --strip-trailing-cr -u majmin/expected_expanded.txt majmin/input.txt.expanded.txt; then
    echo "Maj/Min regression test PASSED"
else
    echo "Maj/Min regression test FAILED: expanded output does not match expected_expanded.txt"
    exit 1
fi

# ── ":&" / ":|" definition-modifier checks ────────────────────────────────────
# See andor/. Same shared bridge.y/bridge.l/majMinExpand.cpp as bidlab, so the
# same three checks apply here: the Maj/Min name-fork + ":&" convergence
# check (diffed against the same golden file bidlab's andor test uses),
# ":&" on an undefined name failing at load time, and a plain ":=" redefinition
# warning rather than failing. Not baseline-diffed — bidlab/test/andor/input.txt
# already covers the behavioral (does ":&"/":|" produce the same bids as hand-
# duplicating the condition) side; deallab has no equivalent auction concept.
ANDOR_OK=1
rm -f andor/maj_fork.txt.expanded.txt
"$DEALER" -i andor/maj_fork.txt -p andor/conv -G -s 0 1 Any Any >/dev/null 2>andor/maj_fork.stderr || true
if grep -v '^\[majMinExpand\]' andor/maj_fork.stderr | grep -q .; then
    echo "andor regression test FAILED: unexpected stderr output for andor/maj_fork.txt:"
    cat andor/maj_fork.stderr
    ANDOR_OK=0
fi
if ! diff --strip-trailing-cr -u andor/expected_maj_fork_expanded.txt andor/maj_fork.txt.expanded.txt; then
    echo "andor regression test FAILED: andor/maj_fork.txt expanded output does not match expected_maj_fork_expanded.txt"
    ANDOR_OK=0
fi

rm -f andor/undefined_target.txt.expanded.txt
UNDEF_EXIT=0
"$DEALER" -i andor/undefined_target.txt -p andor/conv -G -s 0 1 Any Any >/dev/null 2>andor/undefined_target.stderr || UNDEF_EXIT=$?
if [ "$UNDEF_EXIT" -eq 0 ]; then
    echo "andor regression test FAILED: andor/undefined_target.txt should be rejected at load time (got exit 0)"
    ANDOR_OK=0
fi
if ! grep -q "requires an earlier definition of \$.1N." andor/undefined_target.stderr; then
    echo "andor regression test FAILED: andor/undefined_target.txt: expected an 'earlier definition' error"
    ANDOR_OK=0
fi

rm -f andor/redefine.txt.expanded.txt
REDEFINE_EXIT=0
"$DEALER" -i andor/redefine.txt -p andor/conv -G -s 0 1 Any Any >/dev/null 2>andor/redefine.stderr || REDEFINE_EXIT=$?
if [ "$REDEFINE_EXIT" -ne 0 ]; then
    echo "andor regression test FAILED: andor/redefine.txt should load successfully (warning, not error), got exit $REDEFINE_EXIT"
    ANDOR_OK=0
fi
if ! grep -q "warning: redefining \$WEAK" andor/redefine.stderr; then
    echo "andor regression test FAILED: andor/redefine.txt: expected a redefinition warning"
    ANDOR_OK=0
fi

if [ "$ANDOR_OK" -eq 1 ]; then
    echo "andor regression test PASSED"
else
    exit 1
fi

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
