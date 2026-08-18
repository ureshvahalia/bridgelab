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

# ── Self-test: combineRule()/negateRule() NULL handling + simplify() ─────────
# No rules file needed. Covers two things that would otherwise go
# unexercised by everything else in this suite: combineRule(NULL, ...)
# directly (with $ANY always defined -- see missing_any/ -- no live code
# path passes NULL to it anymore) and simplify()'s worked examples (interval
# merging, OR-context propagation, NOT-pushdown, contradiction detection,
# keyword-alias collapsing -- see hand-spec.md's "Rule Simplification"),
# built directly with make_leaf()/match_string() rather than parsed from a
# file, and run through the exact same combineRule() path production code
# uses (simplify()'s sole hook -- see tnode.cpp).
echo "Running bidlab --self-test ..."
SELFTEST_EXIT=0
"$BIDDER" --self-test >selftest.stdout 2>selftest.stderr || SELFTEST_EXIT=$?
if [ "$SELFTEST_EXIT" -ne 0 ]; then
    echo "Self-test FAILED (exit $SELFTEST_EXIT)"
    cat selftest.stdout selftest.stderr
    exit 1
fi
for expected in '[self-test] PASSED' '[self-test] simplify checks PASSED'; do
    if ! grep -qF "$expected" selftest.stderr; then
        echo "Self-test FAILED: expected output line missing: $expected"
        cat selftest.stdout selftest.stderr
        exit 1
    fi
done
echo "Self-test PASSED"

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

# ── Missing-$ANY regression check ───────────────────────────────────────────────
# See missing_any/README.md. $ANY is now a language-level built-in (injected
# by read_rules() into every loaded file, unconditionally -- see
# shared/parse_rules.cpp), so a file that never defines it should just work,
# treating it as "always true" the same as if the file had defined it itself.
# (This used to be a crash repro, then a load-time-rejection test; it's a
# positive case now -- see the README for the history.) Run with no
# positional rule args so the CLI's own implicit "$ANY" defaulting for E/W
# is exercised too.
MISSING_ANY_ARGS="-s 0 -i missing_any/input.txt -o missing_any/results.csv -nchecks 4 20"
rm -f missing_any/results.csv
echo "Running bidlab $MISSING_ANY_ARGS ..."
MISSING_ANY_EXIT=0
if [ "$VERBOSE" = "1" ]; then
    "$BIDDER" $MISSING_ANY_ARGS 2>missing_any/stderr.txt || MISSING_ANY_EXIT=$?
    cat missing_any/stderr.txt
else
    "$BIDDER" $MISSING_ANY_ARGS >/dev/null 2>missing_any/stderr.txt || MISSING_ANY_EXIT=$?
fi

MISSING_ANY_OK=1
if [ "$MISSING_ANY_EXIT" -ne 0 ]; then
    echo "Missing-\$ANY regression test FAILED: expected exit code 0 (a missing \$ANY should no longer be an error), got $MISSING_ANY_EXIT"
    MISSING_ANY_OK=0
fi
if grep -qE '^\[(WARN|ERROR)\]' missing_any/stderr.txt; then
    echo "Missing-\$ANY regression test FAILED: unexpected warning/error output:"
    cat missing_any/stderr.txt
    MISSING_ANY_OK=0
fi
if [ ! -s missing_any/results.csv ]; then
    echo "Missing-\$ANY regression test FAILED: results.csv should contain real output"
    MISSING_ANY_OK=0
fi
if [ "$MISSING_ANY_OK" -eq 1 ]; then
    echo "Missing-\$ANY regression test PASSED"
else
    exit 1
fi

# ── $ANY-redefinition regression check ──────────────────────────────────────────
# See any_redefine/README.md. $ANY is reserved -- a file that tries to define
# its own gets silently dropped, not honored, and not warned about either
# (every rules file predating the $ANY built-in defines it themselves, since
# that used to be required; that's harmless boilerplate now, not a mistake
# worth flagging -- see the comment on injectBuiltinAny() in
# shared/parse_rules.cpp). Uses an impossible redefinition ((Points > 100))
# specifically so a wrongly-honored redefinition would make dealing hang/fail
# (no 13-card hand can ever match) rather than just silently producing
# subtly-wrong output -- so completing quickly with real results is itself
# proof the built-in meaning won.
ANY_REDEFINE_ARGS="-s 0 -i any_redefine/input.txt -o any_redefine/results.csv -nchecks 4 5"
rm -f any_redefine/results.csv
echo "Running bidlab $ANY_REDEFINE_ARGS ..."
ANY_REDEFINE_EXIT=0
if [ "$VERBOSE" = "1" ]; then
    "$BIDDER" $ANY_REDEFINE_ARGS 2>any_redefine/stderr.txt || ANY_REDEFINE_EXIT=$?
    cat any_redefine/stderr.txt
else
    "$BIDDER" $ANY_REDEFINE_ARGS >/dev/null 2>any_redefine/stderr.txt || ANY_REDEFINE_EXIT=$?
fi

ANY_REDEFINE_OK=1
if [ "$ANY_REDEFINE_EXIT" -ne 0 ]; then
    echo "\$ANY-redefinition regression test FAILED: expected exit code 0, got $ANY_REDEFINE_EXIT"
    ANY_REDEFINE_OK=0
fi
if grep -qE '^\[(WARN|ERROR)\]' any_redefine/stderr.txt; then
    echo "\$ANY-redefinition regression test FAILED: the drop should be silent, but got warning/error output:"
    cat any_redefine/stderr.txt
    ANY_REDEFINE_OK=0
fi
if [ ! -s any_redefine/results.csv ]; then
    echo "\$ANY-redefinition regression test FAILED: results.csv should contain real output (a wrongly-honored redefinition would make dealing hang/fail instead)"
    ANY_REDEFINE_OK=0
fi
if [ "$ANY_REDEFINE_OK" -eq 1 ]; then
    echo "\$ANY-redefinition regression test PASSED"
else
    exit 1
fi

# ── $ANY in-body-reference regression check ─────────────────────────────────────
# See any_body_ref/README.md. $Any is referenced from within another rule's
# own body and never defined locally at all -- find_rule() must resolve it
# directly, independent of parse order/defroot, not just via a post-parse
# lookup (which would be too late for an in-body reference resolved during
# parsing). Regression coverage for the gap the first version of the $ANY
# built-in had.
ANY_BODY_REF_ARGS="-s 0 -i any_body_ref/input.txt -o any_body_ref/results.csv -nchecks 4 5"
rm -f any_body_ref/results.csv
echo "Running bidlab $ANY_BODY_REF_ARGS ..."
ANY_BODY_REF_EXIT=0
if [ "$VERBOSE" = "1" ]; then
    "$BIDDER" $ANY_BODY_REF_ARGS 2>any_body_ref/stderr.txt || ANY_BODY_REF_EXIT=$?
    cat any_body_ref/stderr.txt
else
    "$BIDDER" $ANY_BODY_REF_ARGS >/dev/null 2>any_body_ref/stderr.txt || ANY_BODY_REF_EXIT=$?
fi

ANY_BODY_REF_OK=1
if [ "$ANY_BODY_REF_EXIT" -ne 0 ]; then
    echo "\$ANY-in-body-reference regression test FAILED: expected exit code 0, got $ANY_BODY_REF_EXIT"
    cat any_body_ref/stderr.txt
    ANY_BODY_REF_OK=0
fi
if [ ! -s any_body_ref/results.csv ]; then
    echo "\$ANY-in-body-reference regression test FAILED: results.csv should contain real output"
    ANY_BODY_REF_OK=0
fi
if [ "$ANY_BODY_REF_OK" -eq 1 ]; then
    echo "\$ANY-in-body-reference regression test PASSED"
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
# both the finding lines and the structure/summary table (see
# printStatsTable()) are asserted precisely, not just their presence --
# label+value matched with flexible whitespace between them rather than
# exact padding, since the table's column width depends on the -i path's
# length, not just the fixed values below.
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
# Static/structural stats and the decision-point summary -- always printed
# with --validate, no flag, as a table (one column per -i system; see
# printStatsTable()). clean.txt: $.1N./$.P. are both opening bids (depth
# 1), one hand-property rule ($Any), no waypoints, no findings.
# Column width depends on the -i path's length, so this matches label and
# value with flexible whitespace between them (anchored at end-of-line, so
# it can't match a value that's a substring of a longer number) rather than
# asserting exact padding.
for expected in \
    'Opening bids \(North\)[[:space:]]+2$' \
    'Total bid-sequence rules[[:space:]]+2$' \
    'Hand-property rules[[:space:]]+1$' \
    'Pure path waypoints[[:space:]]+0$' \
    'Max auction depth[[:space:]]+1$' \
    'Decision points[[:space:]]+1$' \
    'Overlap[[:space:]]+0$' \
    'Gap[[:space:]]+0$' \
    'Unreachable[[:space:]]+0$' \
    'Duplicate-text[[:space:]]+0$'
do
    if ! grep -qE "$expected" validate/clean.stderr; then
        echo "validate regression test FAILED: clean.txt structure table did not report: $expected"
        cat validate/clean.stderr
        VALIDATE_OK=0
    fi
done

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
# Static/structural stats and the decision-point summary, as a table (see
# the clean.txt block above). flawed.txt: 5 openings (1C/1D/1H/1S/1N) + 1
# response (1N.2C.) = 6 bid-sequence rules -- note this deliberately does
# NOT count $.1N.2C.2D. (depth 3): walkValidate() prunes at the UNREACHABLE
# "1N-2C" decision point above it and never visits it, so these stats only
# ever count rules that are actually reachable, not everything the file
# textually defines (see hand-spec.md).
for expected in \
    'Opening bids \(North\)[[:space:]]+5$' \
    'Responses \(South\)[[:space:]]+1$' \
    'Total bid-sequence rules[[:space:]]+6$' \
    'Hand-property rules[[:space:]]+1$' \
    'Pure path waypoints[[:space:]]+0$' \
    'Max auction depth[[:space:]]+2$' \
    'Decision points[[:space:]]+3$' \
    'Overlap[[:space:]]+1$' \
    'Gap[[:space:]]+1$' \
    'Unreachable[[:space:]]+1$' \
    'Duplicate-text[[:space:]]+1$'
do
    if ! grep -qE "$expected" validate/flawed.stderr; then
        echo "validate regression test FAILED: flawed.txt structure table did not report: $expected"
        cat validate/flawed.stderr
        VALIDATE_OK=0
    fi
done

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
for expected in \
    'Decision points[[:space:]]+5$' \
    'Overlap[[:space:]]+1$' \
    'Gap[[:space:]]+2$' \
    'Unreachable[[:space:]]+0$' \
    'Duplicate-text[[:space:]]+0$'
do
    if ! grep -qE "$expected" validate/negative_inference.stderr; then
        echo "validate regression test FAILED: negative_inference.txt structure table did not report: $expected"
        cat validate/negative_inference.stderr
        VALIDATE_OK=0
    fi
done

# Regression check for the retroactive-waypoint-mutation trap in
# precomputeSiblingNegations() (bidlab.cpp): a node first created as a
# waypoint, whose rule gets set retroactively by a later statement sharing
# its prefix, must still be correctly negated by siblings that were
# inserted into the tree before that retroactive mutation happened -- see
# validate/retroactive_sibling.txt's own header comment for the full setup.
"$BIDDER" -s 0 -i validate/retroactive_sibling.txt --validate >validate/retroactive_sibling.stdout 2>validate/retroactive_sibling.stderr
RETROSIB_EXIT=$?
if [ "$RETROSIB_EXIT" -ne 0 ]; then
    echo "validate regression test FAILED: retroactive_sibling.txt --validate should exit 0, got $RETROSIB_EXIT"
    VALIDATE_OK=0
fi
if ! grep -qF 'GAP at 1N-2D-2H: 3000/3000 sampled hands (100.0%) match no option' validate/retroactive_sibling.stderr; then
    echo "validate regression test FAILED: retroactive_sibling.txt did not report a deterministic 100.0% GAP at 1N-2D-2H:"
    cat validate/retroactive_sibling.stderr
    VALIDATE_OK=0
fi
for expected in \
    'Decision points[[:space:]]+5$' \
    'Overlap[[:space:]]+1$' \
    'Gap[[:space:]]+5$' \
    'Unreachable[[:space:]]+0$'
do
    if ! grep -qE "$expected" validate/retroactive_sibling.stderr; then
        echo "validate regression test FAILED: retroactive_sibling.txt structure table did not report: $expected"
        cat validate/retroactive_sibling.stderr
        VALIDATE_OK=0
    fi
done

if [ "$VALIDATE_OK" -eq 1 ]; then
    echo "validate regression test PASSED"
else
    exit 1
fi

# ── --stats (dynamic rule-coverage) checks ──────────────────────────────────────
# printRuleCoverageTable(): how often nextBid() matched a rule vs. fell
# through to suggestContract()'s guess, by round, as a table (one column
# per -i system; see printTable()) -- the empirical counterpart to
# --validate's GAP percentage (a sampling-based prediction of the same
# thing). -s 0 -nchecks 4 --rules-only 5 keeps this fast and deterministic;
# --rules-only also confirms rule coverage can be checked without paying for
# the (skipped) simulation. Column width depends on the -i path's length
# (see the --validate table checks above), so label+value are matched with
# flexible whitespace between them, anchored at end-of-line, rather than
# exact padding.
STATS_ARGS="-s 0 -i testinput.txt -o stats_run.csv -nchecks 4 --rules-only 5 .1N. NotPass"
rm -f stats_run.csv
STATS_OK=1

echo "Running bidlab --stats $STATS_ARGS ..."
"$BIDDER" --stats $STATS_ARGS >stats_with.stdout 2>stats_with.stderr
STATS_WITH_EXIT=$?
if [ "$STATS_WITH_EXIT" -ne 0 ]; then
    echo "stats regression test FAILED: --stats run should exit 0, got $STATS_WITH_EXIT"
    cat stats_with.stderr
    STATS_OK=0
fi
if ! grep -qF 'System rule coverage (--stats):' stats_with.stderr; then
    echo "stats regression test FAILED: --stats did not print the table title"
    cat stats_with.stderr
    STATS_OK=0
fi
for expected in \
    'Opening bids \(North\)[[:space:]]+5/5 \(100.0%\)$' \
    'Responses \(South\)[[:space:]]+4/5 \(80.0%\)$' \
    'Opener'\''s rebid \(North\)[[:space:]]+3/4 \(75.0%\)$' \
    'Responder'\''s rebid \(South\)[[:space:]]+1/3 \(33.3%\)$' \
    'Opener'\''s 2nd rebid \(North\)[[:space:]]+0/1 \(0.0%\)$' \
    'Overall[[:space:]]+13/18 \(72.2%\)$'
do
    if ! grep -qE "$expected" stats_with.stderr; then
        echo "stats regression test FAILED: --stats did not report: $expected"
        cat stats_with.stderr
        STATS_OK=0
    fi
done

# Same command, no --stats: the rule-coverage report must not appear at all.
echo "Running bidlab (no --stats) $STATS_ARGS ..."
"$BIDDER" $STATS_ARGS >stats_without.stdout 2>stats_without.stderr
STATS_WITHOUT_EXIT=$?
if [ "$STATS_WITHOUT_EXIT" -ne 0 ]; then
    echo "stats regression test FAILED: no-flag run should exit 0, got $STATS_WITHOUT_EXIT"
    STATS_OK=0
fi
if grep -q 'rule coverage' stats_without.stderr; then
    echo "stats regression test FAILED: rule-coverage report appeared without --stats:"
    cat stats_without.stderr
    STATS_OK=0
fi

if [ "$STATS_OK" -eq 1 ]; then
    echo "stats regression test PASSED"
else
    exit 1
fi

# ── Trump context / ask-templates (@, Trump<suffix>, $.?.Name., :?) ───────────
# See trumpask/. rkcb.txt: RKCB reachable via three differently-shaped
# auctions, each setting Trump at a different point, with the responses
# declared once in a $.?.RKCB. ask-template and grafted onto each
# attachment -- confirms grafting resolves Trump independently and
# correctly per attachment point. nested.txt: a grafted response itself
# attaches a further template (QASK), confirming multi-level grafting and
# that Trump still resolves through a grafted-under-a-grafted node.
# basic.txt: hand-written @/Trump with no template at all. Each neg_*.txt
# is expected to fail to load (nonzero exit) with a specific error.
TRUMPASK_OK=1

"$BIDDER" -s 0 -i trumpask/rkcb.txt --validate >trumpask/rkcb.stdout 2>trumpask/rkcb.stderr
RKCB_EXIT=$?
if [ "$RKCB_EXIT" -ne 0 ]; then
    echo "trumpask regression test FAILED: rkcb.txt --validate should exit 0, got $RKCB_EXIT"
    cat trumpask/rkcb.stderr
    TRUMPASK_OK=0
fi
if grep -q 'UNREACHABLE' trumpask/rkcb.stderr; then
    echo "trumpask regression test FAILED: rkcb.txt --validate reported UNREACHABLE:"
    cat trumpask/rkcb.stderr
    TRUMPASK_OK=0
fi
for expected in \
    'Total bid-sequence rules[[:space:]]+15$' \
    'Max auction depth[[:space:]]+5$' \
    'Decision points[[:space:]]+10$' \
    'Overlap[[:space:]]+0$' \
    'Unreachable[[:space:]]+0$' \
    'Unused templates[[:space:]]+0$'
do
    if ! grep -qE "$expected" trumpask/rkcb.stderr; then
        echo "trumpask regression test FAILED: rkcb.txt structure table did not report: $expected"
        cat trumpask/rkcb.stderr
        TRUMPASK_OK=0
    fi
done

"$BIDDER" -s 0 -i trumpask/basic.txt --validate >trumpask/basic.stdout 2>trumpask/basic.stderr
BASIC_EXIT=$?
if [ "$BASIC_EXIT" -ne 0 ]; then
    echo "trumpask regression test FAILED: basic.txt --validate should exit 0, got $BASIC_EXIT"
    cat trumpask/basic.stderr
    TRUMPASK_OK=0
fi
if ! grep -qE 'Total bid-sequence rules[[:space:]]+2$' trumpask/basic.stderr; then
    echo "trumpask regression test FAILED: basic.txt structure table did not report 2 bid-sequence rules"
    cat trumpask/basic.stderr
    TRUMPASK_OK=0
fi

"$BIDDER" -s 0 -i trumpask/nested.txt --validate >trumpask/nested.stdout 2>trumpask/nested.stderr
NESTED_EXIT=$?
if [ "$NESTED_EXIT" -ne 0 ]; then
    echo "trumpask regression test FAILED: nested.txt --validate should exit 0, got $NESTED_EXIT"
    cat trumpask/nested.stderr
    TRUMPASK_OK=0
fi
for expected in \
    'Total bid-sequence rules[[:space:]]+8$' \
    'Max auction depth[[:space:]]+6$' \
    'Decision points[[:space:]]+6$' \
    'Overlap[[:space:]]+0$' \
    'Unreachable[[:space:]]+0$'
do
    if ! grep -qE "$expected" trumpask/nested.stderr; then
        echo "trumpask regression test FAILED: nested.txt structure table did not report: $expected"
        cat trumpask/nested.stderr
        TRUMPASK_OK=0
    fi
done

"$BIDDER" -s 0 -i trumpask/unused_template.txt --validate >trumpask/unused_template.stdout 2>trumpask/unused_template.stderr
UNUSED_EXIT=$?
if [ "$UNUSED_EXIT" -ne 0 ]; then
    echo "trumpask regression test FAILED: unused_template.txt --validate should exit 0, got $UNUSED_EXIT"
    cat trumpask/unused_template.stderr
    TRUMPASK_OK=0
fi
if ! grep -qF 'UNUSED-TEMPLATE: RKCB declared' trumpask/unused_template.stderr; then
    echo "trumpask regression test FAILED: unused_template.txt did not report UNUSED-TEMPLATE for RKCB"
    cat trumpask/unused_template.stderr
    TRUMPASK_OK=0
fi
if ! grep -qE 'Unused templates[[:space:]]+1$' trumpask/unused_template.stderr; then
    echo "trumpask regression test FAILED: unused_template.txt structure table did not report 1 unused template"
    cat trumpask/unused_template.stderr
    TRUMPASK_OK=0
fi

# Each of these must be rejected at load time (nonzero exit) with the
# specific, distinguishing error message named alongside it.
NEG_CASES="neg_no_anchor:no '@' establishes Trump
neg_at_on_nt:must mark a real suit call, not Pass or notrump
neg_at_on_pass:must mark a real suit call, not Pass or notrump
neg_undeclared_template:references an undeclared ask-template
neg_template_conflict:already has an explicit definition
neg_dup_relative:declares \"5C\" more than once
neg_illegal_relative:is not a legally-ranked bid sequence"

echo "$NEG_CASES" | while IFS=: read -r fname expectedmsg; do
    negexit=0
    "$BIDDER" -s 0 -i "trumpask/$fname.txt" --validate >"trumpask/$fname.stdout" 2>"trumpask/$fname.stderr" || negexit=$?
    if [ "$negexit" -eq 0 ]; then
        echo "trumpask regression test FAILED: $fname.txt should be rejected at load time (got exit 0)"
        touch trumpask/.neg_failed
    fi
    if ! grep -qF "$expectedmsg" "trumpask/$fname.stderr"; then
        echo "trumpask regression test FAILED: $fname.txt did not report: $expectedmsg"
        cat "trumpask/$fname.stderr"
        touch trumpask/.neg_failed
    fi
done
if [ -f trumpask/.neg_failed ]; then
    rm -f trumpask/.neg_failed
    TRUMPASK_OK=0
fi

if [ "$TRUMPASK_OK" -eq 1 ]; then
    echo "trumpask regression test PASSED"
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
