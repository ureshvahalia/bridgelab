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

# ── Illegal-bid regression check ──────────────────────────────────────────────
# See illegal_bid/README.md. $.1S.1H. is an illegal continuation (a lower-
# ranked suit bid at the same level as the preceding bid). bidlab is expected
# to reject this rule at load time (non-zero exit) rather than silently
# accepting it and producing a contradictory "Bidding" record ("1S-1H"
# alongside a "1S" contract). Not baseline-diffed — a direct assertion, run
# in both normal and --baseline mode.
ILLEGAL_ARGS="-s 0 -i illegal_bid/input.txt -o illegal_bid/results.csv -nchecks 8 5 OpenS Resp4H"
rm -f illegal_bid/results.csv
echo "Running bidlab $ILLEGAL_ARGS ..."
ILLEGAL_EXIT=0
if [ "$VERBOSE" = "1" ]; then
    "$BIDDER" $ILLEGAL_ARGS || ILLEGAL_EXIT=$?
else
    "$BIDDER" $ILLEGAL_ARGS >/dev/null 2>&1 || ILLEGAL_EXIT=$?
fi

ILLEGAL_OK=1
if [ "$ILLEGAL_EXIT" -eq 0 ]; then
    echo "Illegal-bid regression test FAILED: bidlab accepted an illegal bid sequence (\$.1S.1H.) without error"
    ILLEGAL_OK=0
fi
if [ -f illegal_bid/results.csv ] && grep -q '1S-1H' illegal_bid/results.csv; then
    echo "Illegal-bid regression test FAILED: found illegal '1S-1H' bidding sequence in output"
    ILLEGAL_OK=0
fi
if [ "$ILLEGAL_OK" -eq 1 ]; then
    echo "Illegal-bid regression test PASSED"
else
    exit 1
fi

# ── Maj/Min/OMaj/OMin/BMaj/BMin macro-expansion checks ────────────────────────
# See majmin/. Not baseline-diffed for the negative cases (just exit-code
# assertions); the positive expansion case IS diffed against a golden file
# since it needs to match exactly.
MAJMIN_OK=1
rm -f majmin/input.txt.expanded.txt

"$BIDDER" -i majmin/input.txt -o majmin/results.csv -nchecks 4 1 Any Any >/dev/null 2>majmin/positive.stderr || true
# All log levels go to stderr (see shared/log.h), so routine "[INFO] ..."
# startup/summary lines (and "[DEBUG] [majMinExpand] ..." traces, only emitted
# with -L debug) are expected here — only flag genuinely unexpected stderr
# output (warnings/errors).
if grep -vE '^\[(INFO|DEBUG)\]' majmin/positive.stderr | grep -q .; then
    echo "Maj/Min regression test FAILED: unexpected stderr output for the positive expansion case:"
    cat majmin/positive.stderr
    MAJMIN_OK=0
fi
if ! diff --strip-trailing-cr -u majmin/expected_expanded.txt majmin/input.txt.expanded.txt; then
    echo "Maj/Min regression test FAILED: expanded output does not match expected_expanded.txt"
    MAJMIN_OK=0
fi

rm -f majmin/illegal_fork.txt.expanded.txt
"$BIDDER" -i majmin/illegal_fork.txt -o majmin/illegal_fork.csv -nchecks 4 1 Any Any >/dev/null 2>majmin/illegal_fork.stderr
ILLEGAL_FORK_EXIT=$?
if [ "$ILLEGAL_FORK_EXIT" -ne 0 ]; then
    echo "Maj/Min regression test FAILED: illegal_fork.txt should prune the bad variant and continue (exit 0), got $ILLEGAL_FORK_EXIT"
    MAJMIN_OK=0
fi
if ! grep -q "dropping illegal bid sequence \$.1S.1H." majmin/illegal_fork.stderr; then
    echo "Maj/Min regression test FAILED: expected a warning about dropping \$.1S.1H."
    MAJMIN_OK=0
fi
if [ -f majmin/illegal_fork.txt.expanded.txt ] && grep -q '\$\.1S\.1H\.' majmin/illegal_fork.txt.expanded.txt; then
    echo "Maj/Min regression test FAILED: illegal \$.1S.1H. survived into the expanded output"
    MAJMIN_OK=0
fi

for badfile in anchor_violation bmaj_unparenthesized bmaj_as_bid_token; do
    rm -f "majmin/$badfile.txt.expanded.txt"
    BADEXIT=0
    "$BIDDER" -i "majmin/$badfile.txt" -o "majmin/$badfile.csv" -nchecks 4 1 Any Any >/dev/null 2>"majmin/$badfile.stderr" || BADEXIT=$?
    if [ "$BADEXIT" -eq 0 ]; then
        echo "Maj/Min regression test FAILED: majmin/$badfile.txt should be rejected at load time (got exit 0)"
        MAJMIN_OK=0
    fi
done

# Body-side reference to a Maj/Min-forked bid-sequence name (e.g. "$.2Major."
# inside another rule's body, not its own name) must expand to a
# parenthesized OR of the concrete forks -- see hand-spec.md's "Referencing
# a Maj/Min-forked bid-sequence name from another rule's body".
rm -f majmin/bidseq_reference.txt.expanded.txt
"$BIDDER" -i majmin/bidseq_reference.txt -o majmin/bidseq_reference.csv -nchecks 4 1 Any Any >/dev/null 2>majmin/bidseq_reference.stderr || true
if grep -vE '^\[(INFO|DEBUG)\]' majmin/bidseq_reference.stderr | grep -q .; then
    echo "Maj/Min regression test FAILED: unexpected stderr output for majmin/bidseq_reference.txt:"
    cat majmin/bidseq_reference.stderr
    MAJMIN_OK=0
fi
if ! diff --strip-trailing-cr -u majmin/expected_bidseq_reference_expanded.txt majmin/bidseq_reference.txt.expanded.txt; then
    echo "Maj/Min regression test FAILED: bidseq_reference.txt expanded output does not match expected_bidseq_reference_expanded.txt"
    MAJMIN_OK=0
fi

if [ "$MAJMIN_OK" -eq 1 ]; then
    echo "Maj/Min regression test PASSED"
else
    exit 1
fi

# ── ":&" / ":|" definition-modifier checks ────────────────────────────────────
# See andor/. andor/input.txt is testinput.txt with the $.1N.2C.2H./2S. split
# and the $.1N.2S. three-way OR rewritten using ":&"/":|" instead of hand
# duplicating/chaining the conditions; run with the exact same args as the
# main regression test above, its data rows (everything but the header, which
# embeds the source filename) must match baseline.csv exactly, proving ":&"/
# ":|" produce identical bidding behavior to writing the combined condition
# out by hand.
ANDOR_OK=1
rm -f andor/results.csv
"$BIDDER" -s 0 -i andor/input.txt -o andor/results.csv -nchecks 32 3 .1N. NotPass >/dev/null 2>andor/positive.stderr || true
# All log levels go to stderr (see shared/log.h); routine "[INFO] ..."
# startup/summary lines are expected — only flag genuinely unexpected output.
if grep -vE '^\[(INFO|DEBUG)\]' andor/positive.stderr | grep -q .; then
    echo "andor regression test FAILED: unexpected stderr output for andor/input.txt:"
    cat andor/positive.stderr
    ANDOR_OK=0
fi
if [ ! -f baseline.csv ]; then
    echo "andor regression test SKIPPED equivalence check: no baseline.csv yet (run --baseline first)"
elif ! diff --strip-trailing-cr -u <(tail -n +2 baseline.csv) <(tail -n +2 andor/results.csv); then
    echo "andor regression test FAILED: andor/input.txt (:&/:| version) does not match baseline.csv (hand-duplicated version)"
    ANDOR_OK=0
fi

# The exact motivating example: $.1N.2C.2Maj. forks on name, then a later
# ":&" on one fork only. Checks the fork is preserved through expansion and
# that ":&" is preserved (not silently rewritten to ":=") on the one line
# that doesn't itself use a Maj/Min macro.
rm -f andor/maj_fork.txt.expanded.txt
"$BIDDER" -i andor/maj_fork.txt -o andor/maj_fork.csv -nchecks 4 1 Any Any >/dev/null 2>andor/maj_fork.stderr || true
if grep -vE '^\[(INFO|DEBUG)\]' andor/maj_fork.stderr | grep -q .; then
    echo "andor regression test FAILED: unexpected stderr output for andor/maj_fork.txt:"
    cat andor/maj_fork.stderr
    ANDOR_OK=0
fi
if ! diff --strip-trailing-cr -u andor/expected_maj_fork_expanded.txt andor/maj_fork.txt.expanded.txt; then
    echo "andor regression test FAILED: andor/maj_fork.txt expanded output does not match expected_maj_fork_expanded.txt"
    ANDOR_OK=0
fi

# ":&"/":|" require an earlier definition of their target — undefined target
# is a load-time error, same style as an undefined "$Name" reference.
for badfile in undefined_target undefined_target_or; do
    rm -f "andor/$badfile.txt.expanded.txt"
    BADEXIT=0
    "$BIDDER" -i "andor/$badfile.txt" -o "andor/$badfile.csv" -nchecks 4 1 Any Any >/dev/null 2>"andor/$badfile.stderr" || BADEXIT=$?
    if [ "$BADEXIT" -eq 0 ]; then
        echo "andor regression test FAILED: andor/$badfile.txt should be rejected at load time (got exit 0)"
        ANDOR_OK=0
    fi
    if ! grep -q "requires an earlier definition of \$.1N." "andor/$badfile.stderr"; then
        echo "andor regression test FAILED: andor/$badfile.txt: expected an 'earlier definition' error"
        ANDOR_OK=0
    fi
done

# Plain ":=" redefinition is allowed (unlike ":&"/":|" on a missing name) but
# warns rather than failing, since it's usually a copy-paste accident.
rm -f andor/redefine.txt.expanded.txt
REDEFINE_EXIT=0
"$BIDDER" -i andor/redefine.txt -o andor/redefine.csv -nchecks 4 1 Any Any >/dev/null 2>andor/redefine.stderr || REDEFINE_EXIT=$?
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

# ── System validation (--validate) checks ──────────────────────────────────────
# See validate/. clean.txt has one decision point that exhaustively and
# exclusively partitions every hand -- --validate should report zero
# findings. flawed.txt deliberately exercises all four finding types in one
# small file (see the comments in validate/flawed.txt for exactly how each
# one is constructed). -s 0 makes the sampled counts/paths deterministic, so
# both the finding lines and the final per-system summary line are asserted
# exactly, not just their presence.
VALIDATE_OK=1

"$BIDDER" -s 0 -i validate/clean.txt --validate >validate/clean.stdout 2>validate/clean.stderr
CLEAN_EXIT=$?
if [ "$CLEAN_EXIT" -ne 0 ]; then
    echo "validate regression test FAILED: clean.txt --validate should exit 0, got $CLEAN_EXIT"
    VALIDATE_OK=0
fi
if grep -q '\[WARN\]' validate/clean.stderr; then
    echo "validate regression test FAILED: clean.txt --validate produced unexpected warning(s):"
    cat validate/clean.stderr
    VALIDATE_OK=0
fi
if ! grep -q '1 decision point(s), 0 overlap, 0 gap, 0 unreachable, 0 duplicate-text' validate/clean.stderr; then
    echo "validate regression test FAILED: clean.txt --validate summary line did not match the expected all-zero counts:"
    cat validate/clean.stderr
    VALIDATE_OK=0
fi

"$BIDDER" -s 0 -i validate/flawed.txt --validate >validate/flawed.stdout 2>validate/flawed.stderr
FLAWED_EXIT=$?
if [ "$FLAWED_EXIT" -ne 0 ]; then
    echo "validate regression test FAILED: flawed.txt --validate should exit 0 (findings are warnings, not errors), got $FLAWED_EXIT"
    VALIDATE_OK=0
fi
for expected in \
    'DUPLICATE at (opening): 1H and 1S use identical rule text' \
    'OVERLAP at (opening):' \
    'GAP at (opening):' \
    'UNREACHABLE at 1N-2C: no hand satisfies the path here'
do
    if ! grep -qF "$expected" validate/flawed.stderr; then
        echo "validate regression test FAILED: flawed.txt --validate did not report: $expected"
        VALIDATE_OK=0
    fi
done
if ! grep -q '3 decision point(s), 1 overlap, 1 gap, 1 unreachable, 1 duplicate-text' validate/flawed.stderr; then
    echo "validate regression test FAILED: flawed.txt --validate summary line did not match the expected counts:"
    cat validate/flawed.stderr
    VALIDATE_OK=0
fi

# Negative inference from rejected siblings (see hand-spec.md's "Negative
# Inference from Rejected Siblings"): North's 2S is only reachable after 2H
# is rejected, so North's accumulated hand knowledge there is "Spades >= 4
# AND NOT (Hearts >= 4)", not just "Spades >= 4" -- making 3H's own
# "Hearts >= 4" (the only option at that decision point) impossible for
# every hand that could validly reach it. This asserts the resulting GAP is
# the deterministic 100.0% (3000/3000) the fix produces, not the natural,
# much lower rate of randomly holding both majors that a missing/broken
# negative inference would instead show (71.6% in manual testing).
"$BIDDER" -s 0 -i validate/negative_inference.txt --validate >validate/negative_inference.stdout 2>validate/negative_inference.stderr
NEGINF_EXIT=$?
if [ "$NEGINF_EXIT" -ne 0 ]; then
    echo "validate regression test FAILED: negative_inference.txt --validate should exit 0, got $NEGINF_EXIT"
    VALIDATE_OK=0
fi
if ! grep -qF 'GAP at 1N-2C-2S-3D: 3000/3000 sampled hands (100.0%) match no option' validate/negative_inference.stderr; then
    echo "validate regression test FAILED: negative_inference.txt did not report a deterministic 100.0% GAP at 1N-2C-2S-3D:"
    cat validate/negative_inference.stderr
    VALIDATE_OK=0
fi
if ! grep -q '5 decision point(s), 1 overlap, 2 gap, 0 unreachable, 0 duplicate-text' validate/negative_inference.stderr; then
    echo "validate regression test FAILED: negative_inference.txt --validate summary line did not match the expected counts:"
    cat validate/negative_inference.stderr
    VALIDATE_OK=0
fi

if [ "$VALIDATE_OK" -eq 1 ]; then
    echo "validate regression test PASSED"
else
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
