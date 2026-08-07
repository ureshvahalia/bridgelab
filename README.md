# BridgeLab

Two command-line tools for bridge hand analysis and bidding system evaluation.

**Deallab** generates random deals satisfying user-defined hand constraints, then performs double-dummy or single-dummy analysis using the DDS library.

**Bidlab** evaluates and compares bridge bidding systems by simulation. Given hand constraints and one or more system definition files, it generates random deals, runs single-dummy analysis over many random opponent holdings, and reports how well each system finds the optimal contract.

---

## Prerequisites

On Debian/Ubuntu/WSL:

```
sudo apt install build-essential flex bison libgsl-dev libboost-thread-dev libboost-system-dev
```

The DDS library (`lib/libdds.a`) is included as a pre-built Linux x86-64 static library and does not need to be installed separately.

---

## Building

```
make          # builds both deallab and bidlab (Debug)
make release  # Release builds
make clean    # remove build artefacts
```

The Makefiles auto-detect `linux`, `msys2`, or `cygwin` and place binaries under:
- `deallab/bin/<platform>/<target>/deallab[.exe]`
- `bidlab/bin/<platform>/<target>/bidlab[.exe]`

Examples:
- `deallab/bin/linux/Debug/deallab`
- `bidlab/bin/linux/Debug/bidlab`
- `deallab/bin/msys2/Debug/deallab.exe`
- `bidlab/bin/cygwin/Debug/bidlab.exe`

Platform-specific runtime notes:
- Linux and Cygwin builds embed a runpath relative to the executable, so `libdds` is loaded from the repository `lib/` directory when running from the build tree.
- MSYS2 builds link the GCC and C++ runtimes statically and copy `dds.dll` into the output directory, so the packaged `.exe` does not depend on MSYS2 runtime DLLs being present on `PATH`.

---

## Running the Tests

```
./run_tests.sh
```

Runs the regression suites for both programs and reports pass/fail.
To regenerate baselines after a verified-good build:

```
./run_tests.sh --baseline
```

---

## Deallab

Generate random bridge deals matching hand constraints and optionally run double-dummy (DDA) or single-dummy analysis (SDA).

### Usage

```
cd deallab/test          # or any working directory containing an input.txt
deallab [options] reps [RuleN [RuleE [RuleS [RuleW]]]]
deallab [options] -F handsFile
```

### Options

| Option | Description |
|--------|-------------|
| `-i file` | Rule definitions file (default `input.txt`) |
| `-p prefix` | Prefix for output files (default none) |
| `-S` | Single-dummy analysis (default is double-dummy) |
| `-G` | Generate hands only, no analysis |
| `-F file` | Analyse a file of pre-specified hands |
| `-E n` | EW iterations per deal in SDA mode (default 128) |
| `-D N\|S\|E\|W` | Declarer direction (default N) |
| `-f filter` | Strains to skip, e.g. `11100` skips S/H/D (default `00000`) |
| `-s seed` | RNG seed for reproducibility |

### Output Files

| File | Content |
|------|---------|
| `DDA.csv` | Per-deal trick counts and par contract for all 4 hands |
| `SDA.csv` | Per-deal N/S hand summary, par bid, average tricks, IMP table |
| `*_north.txt`, `*_south.txt`, etc. | Formatted hand output per player |
| `*BBO.lin` | Hands in BBO LIN format |

### Quick Example

```
cd deallab/test
../../bin/linux/Debug/deallab -i testinput.txt -p out 4 1S
cat out_north.txt
```

---

## Bidlab

Evaluate and compare bridge bidding systems by simulation.

### Usage

```
cd bidlab/test          # or any working directory containing the system files
bidlab [options] reps [RuleN [RuleE [RuleS [RuleW]]]]
```

### Options

| Option | Description |
|--------|-------------|
| `-i file[,file]...` | One or more bidding system files; multiple files compared side by side |
| `-o file` | Main CSV output (default `output.csv`) |
| `-v file` | Detail CSV: per-bid expected scores per deal |
| `-p file` | Input PBN file: use pre-specified deals instead of random generation |
| `-b file` | BBO LIN output (default `bbo.lin`) |
| `-nchecks n` | EW holdings to simulate per deal (default 128) |
| `-P rule` | Constraint on partner's hand |
| `-s seed` | RNG seed for reproducibility |
| `-L level` | Log level: `error`\|`warning`\|`info`\|`debug` (default `info`) |
| `--rules-only` | Stop each auction the moment no rule matches, instead of simulating a guessed contract; also skips the per-deal par computation. See [bidlab/doc/design.md](bidlab/doc/design.md) for the full column-level effect. |
| `--validate` | Check each `-i` system's rule tree offline for overlapping/gap/unreachable/duplicate rules instead of dealing; see [bidlab/doc/design.md](bidlab/doc/design.md#system-validation---validate-mode). |

### Quick Example

```
cd bidlab/test
../../bin/linux/Debug/bidlab -s 0 -i testinput.txt -o results.csv -nchecks 32 3 .1N. NotPass
```

---

## Rule Language

Both programs share the same rule definition format. Rules are named expressions in `input.txt`:

```
$1N   := (15 TO 17 Points) AND (shape [4,3,3,3] OR [4,4,3,2] OR [3,3,3,4]);
$Resp := (Points >= 6);
```

**Keywords:** `Points`, `Spades`, `Hearts`, `Diamonds`, `Clubs`, `tpts`,
suit-prefixed spot cards (`Sa`, `Sk`, `Ha`, etc.), suit functions (`Spts`, `Sl`).

**Operators:** `AND`, `OR`, `NOT`, `TO` (range), `?=` (approximately equal),
`!=`, `<=`, `>=`, comparison operators, `SHAPE [...]`, `PATTERN [...]`.

---

## Directory Structure

```
bridgelab/
  Makefile          — top-level build (delegates to deallab/ and bidlab/)
  run_tests.sh      — master regression test runner
  shared/           — source shared by both programs (rule parser, scoring, pack)
  lib/
    libdds.a        — DDS library (pre-built, Linux x86-64)
    dds.h           — DDS public header
  deallab/
    Makefile
    src/            — deallab-specific source
    doc/design.md   — architecture and design notes
    test/           — regression test inputs and baseline
  bidlab/
    Makefile
    src/            — bidlab-specific source
    input/          — example bidding system definition files
    doc/design.md   — architecture and design notes
    test/           — regression test inputs and baseline
```

---

## DDS Library

The included `lib/libdds.a` is a pre-built static library for Linux x86-64 built from
the open-source [DDS project](https://github.com/dds-bridge/dds) (Bo Haglund / Soren Hein).
It is used under the terms of the Apache 2.0 licence. Source is available at that repository.
