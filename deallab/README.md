# Deallab

A command-line bridge hand generator and analysis tool. It generates random deals
satisfying user-defined constraints, then optionally performs double-dummy analysis
(DDA) or single-dummy simulation (SDA) using the Bo Haglund
[DDS library](https://github.com/dds-bridge/dds) v2.9.0.

## Features

- Flexible rule language for specifying hand constraints (HCP, suit lengths, shape, specific cards)
- Double-dummy analysis: computes optimal trick counts for all strains and declarers
- Single-dummy simulation: fixes a known hand and simulates many opponent holdings
- Par contract and IMP scoring
- Multiple output formats: per-player hand files, CSV analysis files
- Batch mode: analyze a file of pre-specified hands

## Prerequisites

- GCC / G++ (tested with MSYS2 UCRT64 on Windows)
- GNU Make
- Flex and Bison
- [DDS library](https://github.com/dds-bridge/dds) (`dds.dll` / `dds.lib`)
- [GSL (GNU Scientific Library)](https://www.gnu.org/software/gsl/) for the Mersenne Twister RNG

On Windows with MSYS2:
```bash
pacman -S make mingw-w64-ucrt-x86_64-gcc flex bison mingw-w64-ucrt-x86_64-gsl
```

## Building

```bash
make          # Debug build → bin/msys2/Debug/deallab.exe  (Windows)
              #            → bin/linux/Debug/deallab        (Linux)
make Release  # Release build
make clean    # Remove platform build artefacts
make clean-all # Remove all build artefacts
```

## Usage

```
deallab [options] reps [RuleN [RuleE [RuleS [RuleW]]]]
deallab [options] -F handsFile
```

### Options

| Option | Description |
|--------|-------------|
| `-l` | Long-form output (one suit per line) in allhands.txt |
| `-S` | Single-dummy analysis (default is double-dummy) |
| `-G` | Generate hands only, no analysis |
| `-V` | Vulnerable (NS) |
| `-d directory` | Set working directory (default `.`) |
| `-i rulesfile` | Rule definitions file (default `input.txt`) |
| `-p prefix` | Prefix for output file names |
| `-f filter` | Strains to skip, e.g. `11100` skips S/H/D (default `00000`, analyze all) |
| `-D N\|S\|E\|W` | Declarer for SDA (default N) |
| `-E n` | Number of EW hands to simulate per NS pair in SDA (default 128) |
| `-F handsFile` | Input file of hands/rules for batch analysis |

### Rules

If two rules are given they apply to North and South; East and West get `$Any`.
If four rules are given they apply to North, East, South, West respectively.
Omitted rules default to `$Any`.

### Examples

Generate 100 deals where North has a standard 1C opener and South has a response:
```bash
deallab 100 Opener Resp
```

Double-dummy analysis, 500 deals:
```bash
deallab 500 Opener Resp
```

Single-dummy analysis with 256 EW simulations per deal:
```bash
deallab -S -E 256 100 Opener Resp
```

Generate hands only (no analysis):
```bash
deallab -G 100 Opener Resp
```

## Rule Language

Rules are defined in `input.txt` (or the file specified by `-i`).
Each rule has the form `$Name := expr;` and rules are separated by `;`.
The file ends with `end`.

### Keywords

| Keyword | Meaning |
|---------|---------|
| `Points` | HCP total |
| `Spades`, `Hearts`, `Diamonds`, `Clubs` | Suit length |
| `Sa`, `Sk`, `Sq`, `Ha`, `Hk`, ... | Specific card presence (e.g. `Sa` = ace of spades) |
| `Spts`, `Hpts`, `Dpts`, `Cpts` | HCP in a specific suit |

### Operators

`AND`, `OR`, `NOT`, `<`, `>`, `<=`, `>=`, `?=` (equal), `!=`,
`n TO m` (range, equivalent to `>= n AND <= m`).

### Shape and Pattern

- `shape [4,3,3,3]` — hand has this shape in any suit order
- `pattern [5-3-3-2]` — hand has exactly this distribution (S-H-D-C)

### Example `input.txt`

```
$Any    := (Points >= 0);
$ntop   := (shape [4,3,3,3] OR [4,4,3,2] OR [5,3,3,2]);
$1N     := (15 TO 17 Points) AND $ntop;
$Opener := (11 TO 19 Points) AND (Clubs > 5) AND NOT $1N;
$Resp   := (Points > 5);
end
```

## Output Files

| File | Content |
|------|---------|
| `DDA.csv` | Per-deal points, suit lengths, trick counts, par contract |
| `SDA.csv` | Per-deal N/S summary, par bid, average tricks, IMP table |
| `northHands.txt` | North's hand for each deal |
| `southHands.txt` | South's hand |
| `eastHands.txt` | East's hand |
| `westHands.txt` | West's hand |
| `allHands.txt` | All four hands per deal |

## Project Structure

```
deallab/
├── src/
│   ├── deallab.cpp       # Entry point and orchestration
│   ├── calcScores.cpp    # Bridge scoring, par, IMPs
│   ├── ddsinfo.hpp       # DDS batch management and scoring classes
│   ├── dealerDeal.cpp/.hpp # Dealer-specific deal subclass
│   ├── dealerIO.c        # Time estimation output
│   ├── fnscpp.cpp        # Hand evaluation, rule engine
│   ├── hands.cpp/.h      # DDS test data and utilities
│   ├── master.cpp        # DDS library integration
│   └── csvparser.cpp/.hpp# CSV tokenizer
├── doc/
│   ├── Doxyfile          # Doxygen configuration
│   └── design.md         # Detailed design document
├── test/                 # Regression test inputs and reference outputs
├── output/               # Generated output files
├── Makefile
└── README.md
```

Shared components (in `../shared/`): `oneDeal`, `pack`, `fnscpp`, `rawScore`, `translations`, `parse_rules`, `tnode`, `bridge.l/.y`, `consts.h`, `handInfo.hpp`.

## License

MIT
