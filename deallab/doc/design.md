# Bridge Dealer/Analyzer — Design Document

## Purpose

A command-line tool that generates random bridge deals satisfying user-defined
constraints, then optionally performs double-dummy analysis (DDA) or single-dummy
simulation (SDA) using the external DDS library.

---

## Architecture Overview

```
deallab.cpp       — CLI parsing, top-level orchestration
  ├── oneDeal     — Deals one set of 4 hands matching rules
  │     ├── aHand (fnscpp.cpp)  — Single hand: dealing, evaluation
  │     └── pack  — Card deck with Mersenne Twister RNG
  ├── ddsInfo     — Manages batch of PBN hands for DDS input/output
  │     └── bridgeScores.cpp      — Bridge scoring, par, IMPs
  ├── master.cpp  — DDS library integration (CalcAllTablesPBN)
  └── tnode/bridge  — Flex/Bison rule language parser and evaluator
```

---

## Data Flow

1. `bridge.l` / `bridge.y` parse `input.txt` into a tree of `tnode` rule definitions.
2. `oneDeal::dealAndCheck()` repeatedly draws from the `pack` until all 4 hands
   satisfy their assigned rules.
3. In DDA mode, deals accumulate in a `ddsInfo` batch; `ddsMain()` calls the DDS
   library in batches of 32.
4. `bridgeScores.cpp` computes raw scores, par contracts, and IMPs from trick results.
5. Results are written to `DDA.csv`, `SDA.csv`, and per-player hand files.

---

## Operating Modes

| Flag | Mode | Output |
|------|------|--------|
| *(none)* | Double-dummy analysis | `DDA.csv` + hand files |
| `-S` | Single-dummy analysis | `SDA.csv` + hand files |
| `-G` | Generate hands only | Hand files only |
| `-F file` | Analyze hands from file | `SDA.csv` or `DDA.csv` |

---

## Key Components

### Rule Language (`bridge.l`, `bridge.y`, `tnode.h`, `fnscpp.cpp`)

A small domain-specific language for specifying hand constraints.

**Lexer tokens:** `AND`, `OR`, `NOT`, `TO`, `SHAPE`, `PATTERN`, `END`, `?=`, `!=`,
`<=`, `>=`, `:=`, numbers, `$RuleName` identifiers, keyword identifiers.

**Grammar (simplified):**
```
deflist  := def (';' def)* END
def      := $Name ':=' expr
expr     := NUMBER | keyword | '(' expr ')' | expr op expr
           | NOT expr | expr TO expr expr
           | '[' n,n,n,n ']'        -- shape
           | '[' n-n-n-n ']'        -- pattern (exact distribution)
```

**Keywords:** `Points`, `Spades`, `Hearts`, `Diamonds`, `Clubs`, `tpts`,
suit-prefixed spot cards (`Sa`, `Sk`, `Ha`, etc.), suit functions (`Spts`, `Sl`).

**Evaluation:** Rules are stored as binary trees of `tnode`. `traverse_lrt()`
walks the tree left-right-top with short-circuit evaluation for AND/OR nodes.
`eval_node()` dispatches on node type, casting the opaque `void* hand` to
`aHand*` to call accessor methods.

**Example rules:**
```
$Opener := (11 TO 19 Points) AND (Clubs > 5);
$ntop   := (shape [4,3,3,3] OR [4,4,3,2]);
$1N     := (15 TO 17 Points) AND $ntop;
```

### Hand Representation (`handInfo.hpp`, `fnscpp.cpp`)

`aHand` holds:
- `h[13]` — card codes (integers 0–51, sorted ascending within hand)
- `points` — HCP total
- `pat[4]` — suit lengths (S, H, D, C)
- `suitPts[4]` — HCP per suit
- `shape[4]` — sorted copy of `pat` for shape matching

Cards are encoded as integers where suit = card / 13, rank = card % 13
(0 = Ace, 12 = 2).

### Card Pack (`pack.hpp`, `pack.cpp`)

Singleton `thePack` manages the 52-card deck.

- RNG: GSL Mersenne Twister (`gsl_rng_mt19937`), seeded with `gettimeofday()`
  microseconds. In `DEBUG2` mode, seeded with 0 for reproducibility.
- `deal_hand()`: Fisher-Yates draw of 13 cards, returned sorted.
- `save_pack()` / `restore_pack()`: snapshot/restore for SDA (fix NS, iterate EW).

### DDS Integration (`master.cpp`, `ddsinfo.hpp`, `bridgeScores.cpp`)

- `ddsInfo` accumulates PBN-format hands and stores trick results
  (`maxTricksList[hand][strain]`, 0–13).
- `ddsMain()` calls `CalcAllTablesPBN()` from the DDS library in batches of 32.
- `bridgeScores.cpp` implements:
  - `rawScore()` — full bridge scoring (part-score, game, slam, doubled, vul)
  - `dealScore::setPar()` — exhaustive search for optimal contract
  - `impTranslator::rawToImp()` — ACBL IMP conversion table
  - `ddsInfo::impsVsPar()` — average IMPs for each possible contract vs par

### Output Files

| File | Content |
|------|---------|
| `DDA.csv` | Per-deal: hand pts/suit lengths for all 4 hands, trick counts, par |
| `SDA.csv` | Per-deal: N/S hand summary, par bid, average tricks, IMP table |
| `northhands.txt`, `easthands.txt`, etc. | Per-hand formatted output |
| `bothhands.txt` | N+S hands side by side |
| `allhands.txt` | All 4 hands |

---

## Command-Line Reference

```
deallab [options] reps [RuleN [RuleE [RuleS [RuleW]]]]
deallab [options] -F handsFile

Options:
  -l            Long-form output (one suit per line) in allhands
  -S            Single-dummy analysis (default is double-dummy)
  -G            Generate hands only (no analysis)
  -V            Vulnerable (NS)
  -d directory  Set working directory (default ".")
  -i rulesfile  Rule definitions file (default "input.txt")
  -p prefix     Prefix for output files (default none)
  -f filter     Strains not to analyze, e.g. "11100" skips S/H/D (default "00000")
| -s seed       RNG seed for reproducibility (default: time-based) |
  -D declarer   N | S | E | W (default N)
  -E EWiters    Number of EW hands to iterate in SDA mode (default 128)
  -F handsFile  Input file of hands/rules for batch analysis

Rules:
  If two rules supplied, they are assigned to N and S (E and W get $Any)
  If four rules supplied, they are assigned to N, E, S, W
  Missing rules default to $Any
```

---

## Planned Enhancement: Partnership (NS) Combined Rules

### Motivation

Allow constraints on the combined N+S holding, e.g.:
- Total NS points > 23
- Combined spade length exactly 8

### Design: `handBase` / `aHand` / `partnerHand` Hierarchy

No new rule language syntax is required. The existing keywords (`Points`,
`Spades`, etc.) are reinterpreted over combined NS totals when evaluating a
partnership rule.

```
handBase  — shared fields (points, pat[], suitPts[], shape[]) and methods
              (getPoints, suitLen, suitPoints, checkShape, checkPattern, checkHand)
  ├── aHand        — existing; adds h[13], cards, deal/process/haveCard
  └── partnerHand  — new; constructor sums N+S fields, no card array
```

Key design points:

- **No virtual functions.** `aHand::deal()` does `memset(this, 0, sizeof(aHand))`
  which would corrupt a vtable. All methods are non-virtual; `partnerHand`
  relies on inheriting `handBase` methods directly.
- **`checkHand` moves to `handBase`** and is inherited by both subclasses.
  `eval_node` casts `void* hand` to `handBase*` instead of `aHand*`.
- **`haveCard` / `TSPOT`** remains special-cased to `aHand*` in `eval_node`
  since spot-card queries are meaningless for a combined hand.
  `handBase::haveCard` returns `false`.
- **`checkShape` / `checkPattern`** defined once on `handBase`, shared.

### Files Changed

| File | Change |
|------|--------|
| `handInfo.hpp` | Extract `handBase`; `aHand` and `partnerHand` derive from it |
| `fnscpp.cpp` | Move `checkShape`, `checkPattern`, `checkHand` to `handBase`; change `checker_fn` and keyword function signatures from `aHand*` to `handBase*`; add `partnerHand` constructor |
| `oneDeal.hpp` | Add `void* partnerRule`; add `setPartnerRule()` setter |
| `oneDeal.cpp` | Initialize `partnerRule = NULL`; add partnership check after main loop |
| `deallab.cpp` | Add `-P ruleName` option; call `setPartnerRule` before drivers |

### New CLI option

```
  -P partnerRule  Rule applied to combined N+S hand after individual rules pass
```

### Usage example

In `input.txt`:
```
$NSfit  := (Points > 23) AND (Spades ?= 8);
$Opener := (11 TO 19 Points) AND (Clubs > 5);
$Resp   := (Points > 5);
```

Command line:
```
deallab -P NSfit 100 Opener Resp
```

---

## Known Issues and Future Work

| Priority | Issue |
|----------|-------|
| High | `writeSummaries` only writes N and S — E/W columns in DDA.csv always empty |
| Medium | Dead `#if 0` block in `deallab.cpp` (probabilityInfo / readCases) should be removed |
| Medium | `sdaFp` / `ddaFp` are globals — should be passed as parameters for testability |
| Medium | `new char[ruleNameLen]` for rule names: fixed 64-char limit, leaked at exit |
| Low | `find_rule()` is O(n) linear scan — could use a hash map for large rule sets |
| Low | `hands.cpp` (test data) should move to a `test/` subdirectory |
| Low | `vulnerabilityCodes` enum starts at 1; zero-initialized variable is invalid |
| Low | `MAXTRIES = 500,000,000` with no progress feedback on slow/impossible rule sets |
