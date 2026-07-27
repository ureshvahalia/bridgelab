# BRIDGE Code Sharing Refactoring Plan

**Status: Complete.** All phases below are implemented. This document describes
the project as it was laid out before the rename to the current `bidlab` /
`deallab` / `e:/Dealer_And_Bidder` structure — `Bidder.sav/` is now `bidlab/`,
`dealer290.vscode/` is now `deallab/`, and `e:/BRIDGE/` is now
`e:/Dealer_And_Bidder/`. Some file-level details below (e.g. `random.c`,
`bridge_ly.h`, `bidder.cpp`) reflect that earlier layout and have since been
further consolidated or removed; kept here as a historical record of the
shared-code extraction, not as current file documentation.

## Background

Two related projects share significant code:
- `Bidder.sav/`  — bridge bidding engine (compiled with `-DBIDDER`)
- `dealer290.vscode/` — bridge dealer/generator (compiled with `-DDEALER` implied, no flag needed)

The codebase already uses `#ifdef BIDDER` / `#ifdef DEALER` guards in places,
confirming the design intent was always to share code.  Estimated duplication:
30–40% of core logic.

## Shared Directory

`e:/BRIDGE/shared/`

Both projects reference it directly via Makefile changes (see Phase 2.3):
- `-I../shared` is in both compilers' include paths, so `#include "tnode.h"` etc.
  resolve directly to `shared/` without per-project stubs.
- A `$(OBJ_DIR)/%.o: $(SHARED_DIR)/%.c` pattern rule compiles `.c` files from
  `shared/` without stub source files in `src/`.
- `LEX_SRC` / `YACC_SRC` point directly at `shared/bridge.l` / `shared/bridge.y`.

## Guard Policy

Only add `#ifdef BIDDER` / `#ifdef DEALER` guards where a genuine conflict
exists (e.g. different types for the same function parameter).  Definitions
that are merely superfluous in one project are included unconditionally — the
compiler ignores what is unused.

---

## Phase 1 — Drop-in Extractions  ✅ DONE

Files that were identical or trivially reconcilable.  No functional changes.

| Shared file | Notes |
|---|---|
| `shared/dll.h` | Verbatim copy — byte-identical in both projects |
| `shared/tnode.h` | One `#ifdef BIDDER` guard for `make_leaf` parameter type (`int` vs `long long`); `setSystem` declaration kept inside existing `#ifdef BIDDER` block |
| `shared/consts.h` | Union of both versions: Bidder's `ACE/KING/QUEEN/JACK` + Dealer's `MAX_TRICKS`, `min`, `enum outputFormat`, `typedef oneHand` all present, no guards |

Side-effect: `Bidder.sav/src/globals.h` had duplicate definitions of `oneHand`
and `enum outputFormat` (now provided by `shared/consts.h`).  Those
definitions were removed from `globals.h`.

---

## Phase 2 — Near-identical C Files  ✅ DONE

No `#ifdef`s needed: all differences were bugs in one codebase that the other
had already fixed, or harmless additions.  The shared files are simply the
correct versions.

### 2.1  `shared/tnode.c`

**Bug fixed:** `make_leaf()` parameter changed from `int` to `long long` in
both projects.  The `int` in Bidder was a latent pointer-truncation bug on
Win64 (LLP64: `long` is 32-bit), where rule-name string pointers stored in
`t_val` would lose their upper 32 bits.  On Windows heap allocations typically
sit below 4 GB so it worked in practice, but was wrong.

`add_leaves()` carries the Dealer's `#ifdef DEBUG` tracing — harmless for
Bidder (which never defines `DEBUG`).

Conditional include at top (both projects use `bridge.parser.hh`; Bidder also
needs `globals.h`):
```c
#ifdef BIDDER
  #include "globals.h"
#else
  #include "bridge.parser.hh"
#endif
```

Both `src/tnode.c` files were stubs (`#include "../../shared/tnode.c"`); stubs are now deleted — see Phase 2.3.

**Also fixed:** Both `bridge.y` files cleaned up — removed the redundant
`(long)((long long)...)` double-cast pattern.  The pointer case
`make_leaf(TDEFINE, ...)` now passes `(long long)$1` directly; the shape/
pattern pack expressions use `(long long)` throughout without the outer
truncating `(long)` cast.

### 2.2  `shared/translations.c`

Three differences, all resolved without guards:

| Difference | Resolution |
|---|---|
| Dealer's `if (*str == '$') str++;` | Included — harmless for Bidder (inputs never start with `$`) |
| Bidder's bounds check `if (hp - hand >= NCARDS_IN_HAND) return NULL` inside loop | Included — Dealer had a latent buffer-overflow bug without it |
| `*(--where)` vs `*where` in `writePbnHand` | Used Dealer's `*(--where)` — removes spurious trailing space; Bidder's output was writing a space before every appended character |

Both `src/translations.c` files were stubs (`#include "../../shared/translations.c"`); stubs are now deleted — see Phase 2.3.

---

## Regression Tests  ✅ DONE

Fixed-seed deterministic output for both projects, comparable with a baseline.

### Mechanism

| Project | RNG | Seeding |
|---|---|---|
| Dealer | GSL Mersenne Twister | `pack.cpp` already calls `gsl_rng_set(rng, 0)` under `#ifdef DEBUG2` |
| Bidder | stdlib `srand`/`rand` | `random.c` now calls `srand(0)` under `#ifdef DEBUG2` |

Both Debug builds are compiled with `-DDEBUG2` by default (already present in
their respective Makefiles), so no build-system changes are needed.

### Files created

| File | Purpose |
|---|---|
| `Bidder.sav/src/random.c` | Added `#ifdef DEBUG2` block to `set_random_seed()` |
| `Bidder.sav/test/testinput.txt` | Rules file for Bidder regression test (copy of Dealer's) |
| `Bidder.sav/test/run_tests.sh` | Bidder test: runs `bidder -nchecks 32 3 1S Any 1S1N Any`, diffs `results.csv` vs `baseline.csv` |
| `dealer290.vscode/test/run_tests.sh` | Dealer test: bash equivalent of `test.bat`, diffs `testresults.txt` vs `baseline.txt` |
| `run_tests.sh` | Master script: calls both project test scripts |

### Workflow

**First time (generate baseline after a verified-good build):**
```bash
./run_tests.sh --baseline
```

**After each set of changes:**
```bash
./run_tests.sh
```

### 2.3  Makefile-based sharing — eliminate all stubs  ✅ DONE

Replaced the `#include "../../shared/..."` stub-file mechanism with direct
Makefile references.  No stub files remain in either project.

**Changes to both `Bidder.sav/Makefile` and `dealer290.vscode/Makefile`:**

| Change | Effect |
|--------|--------|
| `SHARED_DIR = ../shared` | Single variable for the shared path |
| `LEX_SRC = $(SHARED_DIR)/bridge.l` | flex reads directly from shared |
| `YACC_SRC = $(SHARED_DIR)/bridge.y` | bison reads directly from shared |
| `-I$(SHARED_DIR)` added to compiler flags | headers found without stubs |
| `$(OBJ_DIR)/%.o: $(SHARED_DIR)/%.c` pattern rule | `.c` files compiled from shared |

**Stub files deleted (14 total):**

| Project | Files removed |
|---------|---------------|
| `Bidder.sav/src/` | `tnode.h`, `consts.h`, `tnode.c`, `translations.c`, `bridge.l`, `bridge.y` |
| `Bidder.sav/lib/` | `dll.h` |
| `dealer290.vscode/src/` | `tnode.h`, `consts.h`, `dll.h`, `tnode.c`, `translations.c`, `bridge.l`, `bridge.y` |

**New shared files added:**

| File | Notes |
|------|-------|
| `shared/bridge.l` | Dealer's version (whitespace-only diff from Bidder's) |
| `shared/bridge.y` | Dealer's version — already had `%right SHAPE PATTERN` that Bidder lacked (resolves shift/reduce conflict for nested `SHAPE`/`PATTERN` prefix expressions) |

### 2.4  Pack management and RNG unification  ✅ DONE

**Problem:** Bidder used C stdlib `rand()`/`srand()` with harmful periodic
re-seeding every 20 000 calls.  Dealer already used GSL Mersenne Twister
(MT19937, period 2¹⁹⁹³⁷).

**Changes:**

| Shared file | Notes |
|---|---|
| `shared/pack.hpp` | Class declaration + `extern randCalls`, `extern shell` |
| `shared/pack.cpp` | GSL MT19937 RNG; `shell()`; Fisher-Yates pack; `lowpip()` stub; fixed `NCARDS_IN_SUIT`→`NCARDS_IN_HAND` bug in `deal_hand` |

| Bidder file | Change |
|---|---|
| `src/random.c` | Replaced with 5-line thin adapter (`thePack.*` wrappers) |
| `src/fnscpp.cpp` | Removed duplicate `shell()` definition |
| `src/globals.h` | Removed `extern int newSeeds` |
| `src/bidder.cpp` | Removed `newSeeds` from 3 diagnostic `fprintf` calls |
| `Makefile` | Added `pack.o` to OBJS; `-lgsl -lgslcblas` to PLATFORM_LIBS; shared `.cpp` pattern rule |

| Dealer file | Change |
|---|---|
| `src/pack.cpp` | Deleted (replaced by shared) |
| `src/pack.hpp` | Deleted (replaced by shared) |
| `Makefile` | Added shared `.cpp` pattern rule |

Also removed the dead `#ifdef BIDDER / #include "globals.h" / #else /
#include "bridge.parser.hh"` block from `shared/tnode.c` — neither include
was needed; `tnode.h` is self-contained.

### 2.5  bridge.* generated files moved to shared/  ✅ DONE

Previously the flex/bison generated files were written into each project's own
directory (`Bidder.sav/` root and `dealer290.vscode/src/`).  Both projects now
generate to `shared/` — since the output is purely a function of `bridge.l` and
`bridge.y`, the two projects produce byte-identical files and only one copy is
needed.

**Changes to both Makefiles:**

| Variable | Old value | New value |
|---|---|---|
| `LEX_OUT` | project-local path | `$(SHARED_DIR)/bridge.scanner.cc` |
| `YACC_OUT` | project-local path | `$(SHARED_DIR)/bridge.parser.cc` |
| `YACC_HEADER` | project-local path | `$(SHARED_DIR)/bridge.parser.hh` |

`clean` no longer deletes the generated files; new `clean-generated` target
removes `shared/bridge.{scanner,parser}.cc` and `shared/bridge.parser.hh`.

Committed generated files in `dealer290.vscode/src/` were deleted.

### 2.6  fnscpp.cpp — bug fixes and controls synchronisation  ✅ DONE

Resolved all correctness issues identified during comparison; brought both
files to the same functional state.

| Fix | File(s) | Detail |
|---|---|---|
| Vtable-safe zeroing | Bidder `fnscpp.cpp` | `memset(this, 0, sizeof(aHand))` in `deal()` and `dealFromPBN()` replaced with explicit per-member zeroing; `memset` would have corrupted the vtable pointer |
| `long long*` cast in TASSIGN | Bidder `fnscpp.cpp` | `*(int*)` → `*(long long*)` to match `t_result` type; the `int*` cast silently truncated on 64-bit |
| Controls logic ported to Dealer | Dealer `fnscpp.cpp`, `handInfo.hpp` | `controls` field, `get_controls()`, `getKeyCards()` added to `handBase` / `aHand`; `get_controls`/`key_cards` statics added; `Controls`/`controls` added to `kword_fn_list`; `kcs`/`keycards` added to `suffix_fn_list`; `controls` summed in `partnerHand` constructor |
| `#include "translations.h"` policy | Dealer `fnscpp.cpp` | Replaced `extern "C" { char* PBN2oneHand... }` with `#include "translations.h"` |
| NULL guard in `checkHand` | Both `fnscpp.cpp` | `if (cur_root == NULL) return true` added to both; NULL rule = no constraint |
| Inline byte masks | Bidder `fnscpp.cpp` | Removed named `BYTE_MASK_x` / `BYTE_SHIFT_x` constants; literals used inline (matching Dealer) |
| `suitNames` scope | Bidder `fnscpp.cpp` | Moved from file scope to local inside `write_leaf` |
| `write_node` buffer safety | Bidder `fnscpp.cpp` | Replaced `snprintf` (triggers `-Wformat-truncation`) with `sprintf(...%.500s...)` matching Dealer; max output 1006 bytes < `TNODE_LEN` (1024) |
| Include cleanup | Bidder `fnscpp.cpp` | `<cstring>` → `<string.h>`; removed redundant `"consts.h"` |

### 2.7  Top-level build and workspace  ✅ DONE

| File | Purpose |
|---|---|
| `e:/BRIDGE/Makefile` | Delegates to both sub-project Makefiles; targets: `all`, `bidder`, `dealer`, `release`, `clean`, `clean-generated`, `clean-all` |
| `e:/BRIDGE/.vscode/tasks.json` | Build all (default), Build Bidder, Build Dealer, Release, Clean, Clean generated, Full clean |
| `e:/BRIDGE/.vscode/launch.json` | `Debug bidder` and `Debug dealer` launch configurations, each building only their project as preLaunchTask |
| `e:/BRIDGE/.vscode/c_cpp_properties.json` | Two IntelliSense configurations (`Bidder` / `Dealer`) with correct defines and include paths |

---

## Phase 3 — Structural Consolidation  ✅ DONE

### 3.1  Adopt `handBase` in Bidder  ✅ DONE

Per-project `handInfo.hpp` files deleted; replaced by `shared/handInfo.hpp`
which has the `handBase` / `aHand` / `partnerHand` class hierarchy.

`shared/handInfo.hpp` declares:
- `handBase`: `points`, `controls`, `pat`, `suitPts`, `shape`; `getPoints`,
  `get_controls`, `suitLen`, `suitPoints`, `virtual haveCard`, `virtual getKeyCards`,
  `checkShape`, `checkPattern`, `checkHand`
- `aHand : handBase`: adds `h`, `cards`, `process()`; declares `copyHand` and `saveHand`
  (both in header; per-project files provide only the one they use)
- `partnerHand : handBase`: constructor sums N+S values

Suit constants `SPADES=0`, `HEARTS=1`, `DIAMONDS=2`, `CLUBS=3` added to
`shared/consts.h` (were duplicated in both `fnscpp.cpp` and dealer `hands.cpp`).

### 3.2  `shared/handInfo.hpp`  ✅ DONE (merged with 3.1)

Both per-project `handInfo.hpp` files deleted.  `shared/handInfo.hpp` is the
single source of truth.  No include-path changes required; both Makefiles
already have `-I$(SHARED_DIR)`.

### 3.3  `fnscpp.cpp` common core → `shared/fns_common.cpp`  ✅ DONE

All common functions extracted to `shared/fns_common.cpp`:
`deal`, `dealFromPBN`, `process`, `haveCard`, `getKeyCards`,
`handBase::checkShape/checkPattern/checkHand`, all statics (`get_tpts`,
`have_spot`, `suit_len`, `spade_len/heart_len/diamond_len/club_len`,
`get_controls`, `key_cards`, `suit_pts`, `check_shape`, `check_dist`),
`kword_fn_list`, `suffix_fn_list`, `match_string`, `eval_node`,
`write_leaf`, `write_node`, `test_and_or`, `partnerHand::partnerHand`.

Both Makefiles: added `$(OBJ_DIR)/fns_common.o` to OBJS.

**Per-project `fnscpp.cpp` now contains only:**

| Project | Functions kept |
|---|---|
| Bidder | `copyHand`, `writeSummaryHeader`, `writeSummary` |
| Dealer | `saveHand`, `writeSummary` |

### 3.4  `oneDeal.cpp` → `shared/oneDeal.cpp`  ✅ DONE

Both per-project `oneDeal.cpp` files deleted; replaced by `shared/oneDeal.cpp`
with `#ifdef BIDDER` / `#ifdef DEALER` guards.

**Common (no guard):** `makePBNrec()`

**`#ifdef BIDDER`:** constructor `(void*, void*, void*, void*)`,
`deal_and_check_b` helper, `dealAndCheck(bool, bool, bool, bool)`,
`enterPbn(char*)`.

**`#ifdef DEALER`:** static member definitions (`match`, `start_time`,
`handsDealt`); constructor `(const char* rulenames[])`, `writeSummaries(FILE*)`,
`printReport(int)`, `deal_and_check_d` helper, `saveNS()`,
`dealAndCheck()`.

**Dead code removed:** `oneDeal::printReport(int, int, bool)` and
`oneDeal::writeSummaries(char*, int)` were declared under `#ifdef BIDDER` in
the header but never called and never correctly defined. Both removed from
`shared/oneDeal.hpp` and not carried forward.

Also removed: `oneDeal::writeSummaryHeader(int howmany)` from header — was
declared under `#ifdef BIDDER` but never defined anywhere, never called.

### 3.5  `read_rules()` → `shared/parse_rules.cpp`  ✅ DONE

Extracted the near-identical `read_rules()` from both `bidder.cpp` and
`main.cpp` into `shared/parse_rules.cpp` + `shared/parse_rules.h`.

The shared version includes `fclose(yyin)` (Dealer's original omitted it,
leaving a file-handle leak).

Both Makefiles: added `$(OBJ_DIR)/parse_rules.o` to OBJS.

`Bidder.sav/src/bidder.cpp`: replaced the inline `read_rules()` definition
and the `extern int yyparse()` / `extern FILE* yyin` pair with
`#include "parse_rules.h"`.

`dealer290.vscode/src/main.cpp`: replaced the static inline `read_rules()`
definition with `#include "parse_rules.h"`.

### Quick wins  ✅ DONE

| Item | Detail |
|---|---|
| `extern "C"` → `#include "translations.h"` | Done in `dealer290/src/oneDeal.cpp` |
| `handInfo` class in `master.cpp` | Confirmed NOT dead — used by `dealInfo.hi[DDS_HANDS]`; no action taken |

---

## What Stays Separate (not shared)

| File(s) | Reason |
|---|---|
| `bid.cpp`, `bidder.cpp` | Bidder-specific domain logic |
| `bridgeScores.cpp` | Dealer-specific IMP scoring |
| `random.c` | Thin adapter: delegates to `shared/pack.cpp` via `thePack.*` |
| `main.cpp` / `dealerIO.c` | Application entry points |
| `hands.cpp` / `hands.h` | Dealer-specific DDS hand setup |
| `csvparser.*` | Dealer-specific input format |
| `io.c` | Bidder-specific time estimation / reporting |
| `bridge_ly.h` | Bidder-specific compiler variable indices |
| `ddsinfo.hpp` | Dealer-specific DDS solver interface |
| `portab.h`, `debug.h` | Third-party DDS library support files |
