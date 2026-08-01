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
| `--rules-only` | Stop each auction the moment no rule matches, instead of calling `suggestContract()`. No simulation is run for the guess step; `Bidding`/`Contract`/`Score`/`IMPs vs Par` columns are omitted and no end-of-run summary is logged. Par Bid/Par Score (from the per-deal SDA) are unaffected. |

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
| `tnode.cpp/.h` | Parse tree node type for rule expressions; also owns the `$Name` -> node lookup, a hash map keyed per definition tree (rather than a linear scan) since one process may load several independent rules files (see `biddingSystem` below) |
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
            ├── if no match: call suggestContract() (skipped in `--rules-only` mode, which
            │   stops the auction there instead)
            │       ├── run SDA with only the known hand fixed
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
| Par Bid, Par Score | Single-dummy par contract and expected score |
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
