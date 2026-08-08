# Bidder — Design Document

## Purpose

Bidder evaluates bridge bidding systems by simulation. Given one or more bidding system
definition files and hand constraints, it generates random deals matching those constraints,
simulates single-dummy analysis (SDA) over many random opponent holdings, and reports how
well each system performs at finding the optimal contract. Multiple systems can be compared
side-by-side in a single run.

---

## Command-Line Interface

```
bidlab [options] reps [RuleN [RuleE [RuleS [RuleW]]]]
```

| Option | Description |
|--------|-------------|
| `-d dir` | Working directory for all file I/O (default `.`) |
| `-i file[,file]...` | One or more bidding system `.txt` files; multiple files are compared side by side |
| `-o file` | Main CSV output file (default `output.csv`) |
| `-v file` | Detail CSV: per-bid expected scores for N and S on each deal |
| `-p file` | Input PBN file: use pre-specified deals instead of random generation |
| `-b file` | BBO LIN output file (default `bbo.lin`) |
| `-nchecks N` | Number of random EW holdings to simulate per deal (default 128) |
| `-P ruleName` | Constraint on partner's hand (named rule from the system file) |
| `-s seed` | RNG seed for reproducibility (default: time-based) |
| `-L level` | Log level: `error`\|`warning`\|`info`\|`debug` (default `info`) |
| `--rules-only` | Stop each auction the moment no rule matches, instead of calling `suggestContract()`. No simulation is run for the guess step; `Bidding`/`Contract`/`Score`/`IMPs vs Par` columns are omitted and no end-of-run summary is logged. The per-deal SDA that computes `Par Bid`/`Par Score` is also skipped, and those columns are omitted too — this is the more expensive of the two simulations, so `--rules-only` runs are correspondingly faster. |
| `--validate` | Check each `-i` system's rule tree offline for authoring mistakes instead of dealing/bidding — see [System Validation](#system-validation---validate-mode) below. Also always prints static/structural system stats (bid counts by round, etc. — see [System Stats](#system-stats) below); this isn't gated by a flag, since it's a byproduct of the same tree walk. No `reps`/rule args, `-o`, `-b`, `-v`, or `-nchecks` are needed; the run never reaches deal generation. |
| `--self-test` | Exercise `combineRule()`/`negateRule()`'s `NULL`-handling directly and exit — no `-i`, no rules file, no dealing. Prints `[self-test] PASSED` and exits 0, or logs the specific failing assertion and exits 1. Exists because, with `$ANY` now built-in (see `hand-spec.md`), no live code path calls these with a `NULL` operand anymore, so nothing else in a normal run exercises that handling. |
| `--stats` | Print dynamic (runtime) system stats at the end of a normal run — currently rule coverage by round (how often `nextBid()` matched a rule vs. fell through to `suggestContract()`'s guess). See [System Stats](#system-stats) below. Off by default; unlike the static stats above, this is real per-run behavior, not free structural data, so it's opt-in. Works with `--rules-only` (and is often paired with it, since rule coverage alone doesn't need the simulation `--rules-only` skips). |

Positional arguments after options:
- `reps` — number of deals to process
- `RuleN RuleE RuleS RuleW` — hand constraints per seat, as rule names from the system file (default `$ANY`). If only one or two rules are given, they apply to N and S; E and W get `$ANY`.

---

## Rule Language

Bidding system definitions are plain-text files parsed by the shared Flex/Bison grammar
(same format as Dealer's rule files). Two layers of convention are defined:

**Hand constraints** follow the standard `$Name := expr;` form used by both programs.
These describe which hands qualify for a given call.

**Bidding sequences** use a special naming convention: rules whose names begin with `$.`
followed by a dot-separated bid sequence define conventions. For example:

```
$.1S.2S. := (Hearts >= 3) AND (Points >= 6);
```

This rule fires when the auction so far is `1S-2S-` and the hand satisfies the constraint.

Rules are loaded from the file(s) specified by `-i`. Multiple comma-separated files
load independent systems that are evaluated in parallel on the same deals.

---

## Architecture

### Class Overview

```
main()
  └── biddingSystem          loads .txt file, builds convention tree
        └── convention       tree node: one bid in a sequence, with an associated rule
  └── auction                orchestrates one deal: deal creation, bidding, output
        ├── bidderDeal       extends oneDeal (shared); generates/validates the deal
        ├── handScores       accumulates DDS scores indexed by (seat, bid)
        └── biddingSystem    walks convention tree per bid
```

**Shared components** (in `shared/`):

| File | Role |
|------|------|
| `oneDeal.cpp/.hpp` | Base deal class: four `aHand` objects, deal generation, PBN/LIN serialization |
| `pack.cpp/.hpp` | Card pack, Fisher-Yates shuffle, RNG (GSL Mersenne Twister) |
| `fnscpp.cpp` | Hand evaluation: HCP, suit lengths, key-card counts, shape/pattern matching |
| `fns_common.cpp` | C-linkage evaluation helpers shared with the parser |
| `parse_rules.cpp/.h` | Reads a rules file, runs it through `majMinExpand`, parses the result |
| `majMinExpand.hpp/.cpp` | Maj/Min/OMaj/OMin/BMaj/BMin macro expansion and bid-sequence legality checking (see `hand-spec.md`); shared by Bidder and Dealer |
| `bid.hpp` | `bid` type (integer 0–39), strain/level accessors, vulnerability enum — used by `majMinExpand` and Bidder's convention-tree builder alike |
| `tnode.cpp/.h` | Parse tree node type for rule expressions; also owns the `$Name` -> node lookup, a hash map keyed per definition tree (rather than a linear scan) since one process may load several independent rules files (see `biddingSystem` below); `combineRule()`/`negateRule()` (Bidder-only) live here too |
| `simplify.hpp/.cpp` | `simplifyRule()`: folds redundant comparisons and propagates context into `OR` branches, hooked into `combineRule()` — see `hand-spec.md`'s "Rule Simplification". No Bidder-specific dependency, usable by Dealer too, though nothing there calls it yet |
| `rawScore.cpp/.h` | Bridge scoring: raw trick-count → contract score |
| `translations.c/.h` | PBN and BBO LIN serialization of hands and boards |
| `bridge.l / bridge.y` | Flex/Bison grammar for the rule language |

**Bidder-specific** (in `src/`):

| File | Role |
|------|------|
| `bidlab.cpp` | `main()`, `auction`, `biddingSystem`, `convention`, `handScores`, `funcStats`/`funcTimer` |
| `bidderDeal.hpp/.cpp` | `bidderDeal`: extends `oneDeal` with N/S/E/W save/load helpers |
| `fnscpp.cpp` | Hand evaluation (copy of shared file, compiled per-project) |
| `io.c` | `print_time_estimate()`: elapsed/remaining time formatting |

---

## Core Loop

```
for each rep:
    createDeal()
        ├── generate random deal matching all four hand rules
        ├── capture BBO LIN record (before pack is cleared)
        ├── run SDA over totHandsToCheck random EW holdings → totScores
        ├── compute par contract (setSDAPar)
        └── write hand + par info to output CSV

    for each biddingSystem:
        bidHand(system)
            ├── walk convention tree: at each step, find first child whose rule matches the current hand
            ├── if a match is found: record bid, advance to next bidder
            │       accumulated rules[bidder] gains the matched rule AND the negation of
            │       every sibling rejected first (see hand-spec.md's "Negative Inference
            │       from Rejected Siblings") -- e.g. bidding 2S over 2H also records
            │       "NOT (Hearts >= 4)", not just "Spades >= 4"
            ├── if no match: call suggestContract() (skipped in `--rules-only` mode, which
            │   stops the auction there instead)
            │       ├── run SDA with only the known hand fixed -- partner's simulated hand
            │       │   is dealt subject to partner's accumulated rules[], so the negative
            │       │   inference above directly improves this simulation's fidelity
            │       └── pick highest-scoring available bid
            └── outputResults(): append auction-so-far (and, unless `--rules-only`,
                bidding sequence/contract/score) to CSV
```

### Bid Encoding

A `bid` is an integer:
- `bidPass = 4`
- Level-1 calls start at 5: `1C=5, 1D=6, 1H=7, 1S=8, 1N=9`
- `bidMaxBid = 39` (7NT)
- `bidInvalid = -1`, `bidNotFound = 3`

Strain order: C=0, D=1, H=2, S=3, NT=4 (within each level).

### Single-Dummy Analysis (SDA)

Bidder uses DDS `CalcAllTablesPBN` in batches of up to 32 deals. For each deal being
evaluated, it:
1. Saves a snapshot of the remaining pack.
2. Repeatedly reshuffles the unknown cards to generate random opponent holdings.
3. Accumulates per-bid scores in `handScores`.
4. Restores the pack snapshot after each batch.

The score for a contract is the sum of raw bridge scores over all simulated holdings,
divided by `totHandsToCheck` to give an expected value.

---

## System Validation (`--validate` mode)

`--validate` walks each `-i` system's convention tree offline — no deals, no DDS,
no CSV output — and flags likely rule-authoring mistakes at every decision point
(a tree node with 2+ children). It never reaches `createDeal()`/`bidHand()`;
`main()` runs `validateSystem()` for each system and returns immediately after.

For each decision point it samples random hands satisfying the precondition
needed to reach that point (reconstructed the same way the live auction builds
`rules[bidder]`: `combineRule`-accumulating that seat's own rules along their
own turns, tracked as two independent North/South accumulators since the two
seats' rules are never combined with each other, *and* -- like the live
auction -- folding in the negation of every sibling rejected on the way to
each bid, per hand-spec.md's "Negative Inference from Rejected Siblings"),
up to `VALIDATE_TARGET_SAMPLES` (3000) matching hands or `VALIDATE_MAXTRIES`
(2,000,000) attempts, and reports:

| Finding | Meaning |
|---------|---------|
| `OVERLAP` | More than one sibling's rule matches the same sampled hand. `findMatchingChild()`'s first-match-wins silently picks one at runtime — this makes the resulting ambiguity visible to the rules author instead. |
| `GAP` | No sibling matches a reachable hand. At runtime this falls through to `suggestContract()`'s simulated guess instead of an authored bid. |
| `UNREACHABLE` | No sampled hand satisfies the path leading to this node at all after `VALIDATE_MAXTRIES` tries — usually dead code, because an ancestor rule already rules it out. Recursion stops below an unreachable node, since nothing under it is reachable either. |
| `DUPLICATE` | Two siblings have byte-identical rule text — almost always a copy-paste mistake, and free to detect (no sampling needed). |

`findMatchingChild()` (the sibling-matching logic) is shared verbatim between
the live auction (`nextBid()`) and `--validate`'s tree walk, so the two can
never disagree about what "reachable"/"matches" means.

Findings are logged as `[validate] OVERLAP/GAP/UNREACHABLE/DUPLICATE at <path>: ...`
warnings (an empty path means the opening bid), each with a sample offending
hand and, for `OVERLAP`, the names of the tied siblings. A one-line summary per
system (`decisionPoint`/`overlap`/`gap`/`unreachable`/`duplicate-text` counts)
is logged at the end. `--validate` always exits 0 — it's a diagnostic report,
not a pass/fail gate; a nonzero-findings run isn't treated as a build failure.

---

## System Stats

Descriptive metadata about a system, split into two kinds depending on
whether it's knowable from the rule file alone or only from an actual run:

**Static (structural)** — always printed by `--validate`, unconditionally,
no separate flag. It's a free byproduct of the same tree walk `--validate`
already does (`walkValidate()`/`validateSystem()`), so there's no reason to
gate it separately. `validateSystem()` still logs findings (`OVERLAP`/
`GAP`/`UNREACHABLE`/`DUPLICATE`) per system as it walks, same as always —
but the structural numbers and the `decisionPoints`/`overlaps`/`gaps`/
`unreachable`/`duplicates` counts that used to print as a one-line summary
per system are collected instead and printed once, together, as a single
table (`printStatsTable()`) after every `-i` system has been walked: one
row per stat, one **column per system**, so comparing several systems'
structure side by side doesn't mean scrolling back and forth between
separate per-system blocks:

```
System structure & validation summary:
                       camel_spec.txt  bws_input.txt
Opening bids (North)               16             18
Responses (South)                 114            111
...
Decision points                    31             92
Overlap                            25             36
Gap                                21             54
Unreachable                         0              7
Duplicate-text                      2              3
```

| Row | Meaning |
|-----|---------|
| `Opening bids (North)`, `Responses (South)`, `Opener's rebid (North)`, `Responder's rebid (South)`, `Opener's 2nd rebid (North)`, `Responder's 2nd rebid (South)` | Count of `$.`-sequence rules defined at that depth (1–6). A round is only included as a row if at least one of the compared systems has a nonzero count there. |
| `Round 7+ (North/South)` | Combined count for depth 7 and beyond — rare enough not to name individually (row included only if nonzero for at least one system). |
| `Total bid-sequence rules` | Sum of the above, per system. |
| `Hand-property rules` | Rules that aren't `$.`-prefixed (e.g. `$balanced`, `$ntop`) — not part of the convention tree, counted separately by walking the flat definition list (`biddingSystem::countHandPropertyRules()`), deduplicated by name so a redefined rule counts once. |
| `Pure path waypoints` | Depth>0 nodes with no rule of their own — exist only as a prefix for a deeper `$.`-sequence (see `biddingSystem::processRule`). |
| `Max auction depth` | Deepest node actually visited by the walk. |
| `Decision points`, `Overlap`, `Gap`, `Unreachable`, `Duplicate-text` | Same counts the old one-line-per-system summary reported, now table rows. |

Column width is derived from each system's name and its widest value, so it
adapts to however the `-i` files were named/pathed on the command line —
tests that check this table's output match label and value with flexible
whitespace between them, not exact column alignment.

**Important**: these counts only include rules the walk actually *visits* —
since `walkValidate()` stops recursing below an `UNREACHABLE` decision point
(see above), a rule that's textually present in the file but sits below one
is silently excluded from every count here, the same way it's excluded from
live play. This is deliberate: the report reflects what the system can
actually do, not what the file textually contains.

**Dynamic (runtime)** — gated behind `--stats`, since it's real per-run
behavior gathered while dealing/bidding, not free structural data. Printed
at the end of a run as a table (`printRuleCoverageTable()`, sharing the
same `printTable()` layout helper `printStatsTable()` uses — see above), one
column per `-i` system, using the same round labels as the static report:

```
System rule coverage (--stats):
                             testinput.txt
Opening bids (North)        30/30 (100.0%)
Responses (South)           27/30 (90.0%)
...
Overall                     82/112 (73.2%)
```

Each cell is `matched/total (pct%)` for that round and system — `guessed`
isn't spelled out separately the way it was before this became a table
(it's `total - matched`), to keep cells compact enough for side-by-side
comparison. For each round, how often `nextBid()` found a matching rule vs.
fell through to `suggestContract()`'s guess (tallied in `auction::nextBid()`
into `matchedByRound`/`guessedByRound`, indexed by system and round). This
is the empirical, ground-truth counterpart to `--validate`'s `GAP`
percentage, which only *predicts* the same thing from 3000 sampled hands
per decision point — the two are worth comparing. Unlike the static table,
rounds beyond 6 are printed individually here (`Round 7 (North)`,
`Round 8 (South)`, ...), not lumped into one bucket — losing which specific
deep round is under-covered would defeat the point of a per-round
breakdown. The bookkeeping itself runs unconditionally (a few integer
increments per bid, negligible next to a DDS solve); only whether it's
*printed* depends on `--stats`. Works with `--rules-only` — the
`guessed[]` tally happens before `--rules-only`'s early return in
`nextBid()`, so rule coverage can be checked without paying for the
simulation `--rules-only` skips.

**Deferred, not yet implemented**: macro-expansion counts (how many
Maj/Min-macro usages were resolved, split into name-forks vs.
body-duplications, plus illegal forks pruned) would need new counters added
to `shared/majMinExpand.cpp`, which currently only has debug-level traces,
not accumulated counts. Also deferred: surfacing rejection-sampling cost
(`dealAndCheck`/`checkHand` attempt counts, especially in
`suggestContract()`'s partner-hand simulation) and DDS call count/timing
under `--stats` — the DDS timing exists already as a `funcStats` instance
but is currently only visible via `-L debug`, not tied to `--stats` at all.

---

## Data Flow

```
input/
  system.txt        ← bidding system rules and hand constraints
  [system2.txt]     ← optional second system for comparison

bidlab -i system.txt[,system2.txt] -o results.csv -v details.csv reps RuleN RuleS

output/
  results.csv       ← one row per deal: hand, vulnerability, par, then per-system: bidding/contract/score
  details.csv       ← one row per deal: hand, bidding sequence, then expected score for every bid × seat
  bbo.lin           ← BBO-importable LIN file, one board per deal
```

### results.csv columns

| Column | Content |
|--------|---------|
| Hand | PBN notation for N and S hands |
| Vul | `N` (not vulnerable) or `V` (NS vulnerable) |
| N Pts, N Ctls, N KC S/H/D/C, N S/H/D/C | North hand summary |
| S Pts … S shape | South hand summary |
| Par Bid, Par Score | Single-dummy par contract and expected score; omitted entirely under `--rules-only` (the SDA that computes them isn't run) |
| Auction X | Auction so far when `suggestContract()` was invoked for system X, or the whole final auction if a rule ended it naturally without ever guessing. Under `--rules-only`, this is the only per-system column, since every auction is treated as incomplete. |
| Bidding X, Contract X, Score X, IMPs vs Par X | Per-system columns (one set per `-i` file); omitted entirely under `--rules-only` |

---

## Output Files

| File | Option | Content |
|------|--------|---------|
| `output.csv` | `-o` | Main results: hand, par, per-system bidding/contract/score |
| `details.csv` | `-v` | Per-bid expected N/S scores for every deal |
| `bbo.lin` | `-b` | BBO LIN format: one `qx|o{n}|...` record per board |

---

## Build

```bash
make          # Debug build → bin/msys2/Debug/bidlab.exe  (Windows)
              #            → bin/linux/Debug/bidlab        (Linux)
make Release  # Release build
make clean    # Remove platform build artefacts
make clean-all # Remove all build artefacts
```

See [Makefile](../Makefile) for platform detection, GSL static linking, and DLL copy logic.

### Troubleshooting: "Cannot create temporary file in C:\WINDOWS\"

If `make` fails partway through with a message like this from `g++`/`as`, it
means the compiler couldn't resolve a writable temp directory from its own
process environment and fell back to the (unwritable, for a normal user)
Windows directory — it's not a problem with this Makefile or with `TMP`/`TEMP`
as seen in your interactive shell. It shows up specifically when the build is
launched from a wrapped/restricted shell (some CI runners, some sandboxed
terminal integrations) whose child processes don't fully inherit the
invoking shell's environment. It does not happen in a normal MSYS2 UCRT64
shell or VS Code's integrated terminal. If you hit it, run the build from a
plain MSYS2 shell (or, on Windows, from a plain PowerShell/cmd window with
`C:\msys64\ucrt64\bin` on `PATH`) instead of whatever wrapped shell produced
the error.
