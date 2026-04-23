# Hand Specification Language

Rules are written in a small domain-specific language, parsed by Flex (`bridge.l`)
and Bison (`bridge.y`), and evaluated against dealt hands by `fns_common.cpp`.

---

## Case Sensitivity

The language is **case-insensitive**.  All identifiers (rule names, keywords,
hand keywords, suit-prefixed keywords) are normalized to uppercase at parse
time, so `Points`, `points`, and `POINTS` are equivalent.  Command-line rule
name arguments are normalized the same way.  Structural keywords (`AND`, `OR`,
`NOT`, `TO`, `END`) and shape keywords (`shape`, `pattern`) already accept
mixed case; this extends that behaviour to all identifiers.

---

## Rule File Format

A rule file is a plain-text file (default `input.txt`).  It contains one or more
**definition blocks**, each terminated by the keyword `END`.  Within a block,
individual definitions are separated by `;`.  Whitespace (spaces, tabs, blank
lines) is ignored.  Comments begin with `##` and extend to the end of the line.

```
## This is a comment
$Opener := (11 TO 21 Points) AND (Spades >= 5) ;
$Strong  := Points >= 22
END
```

A rule **name** starts with `$` and may contain letters, digits, `_`, `-`, and `.`:

```
$1N
$two-spades
$my.rule_v2
```

---

## Definitions

```
$Name := expression
```

Multiple definitions in one block:

```
$A := expr1 ;
$B := expr2 ;
$C := expr3
END
```

A definition may reference any earlier definition by its `$Name`.  Forward
references are not supported.

---

## Expressions

### Integer Literals

Any non-negative integer: `0`, `5`, `15`, `37`.

### Hand Keywords

These evaluate to a numeric property of the hand being tested.

| Keyword | Value |
|---------|-------|
| `Points` or `tpts` | Total high-card points (A=4, K=3, Q=2, J=1) |
| `Spades` or `spades` | Number of spades held |
| `Hearts` or `hearts` | Number of hearts held |
| `Diamonds` or `diamonds` | Number of diamonds held |
| `Clubs` or `clubs` | Number of clubs held |
| `Controls` or `controls` | Control count (Ace=2, King=1) |

### Suit-Prefixed Keywords

These test a specific suit.  The prefix selects the suit:
`S`/`s` = Spades, `H`/`h` = Hearts, `D`/`d` = Diamonds, `C`/`c` = Clubs.

**Suit length and points:**

| Keyword | Value |
|---------|-------|
| `Sl` or `Slen` | Length of the spade suit (also `Hl`, `Dl`, `Cl`) |
| `Spts` | High-card points in spades (also `Hpts`, `Dpts`, `Cpts`) |
| `Skcs` or `Skeycards` | Key cards in spades: Ace of any suit + King of spades (also `Hkcs`, etc.) |

**Specific-card tests** (return 1 if the card is held, 0 otherwise):

| Keyword | Card |
|---------|------|
| `Sa` | Ace of spades |
| `Sk` | King of spades |
| `Sq` | Queen of spades |
| `Sj` | Jack of spades |
| `St` | Ten of spades |
| `S9` … `S2` | Nine through Two of spades |

The same suffixes apply with `H`, `D`, `C` prefixes (`Ha`, `Hk`, `Dq`, `Ct`, …).

### Comparison Operators

All comparisons return 1 (true) or 0 (false).

| Operator | Meaning |
|----------|---------|
| `<` | less than |
| `>` | greater than |
| `<=` | less than or equal |
| `>=` | greater than or equal |
| `?=` | equal |
| `!=` | not equal |

Example: `Points >= 15`, `Spades ?= 5`, `Hpts != 0`

### Logical Operators

| Operator | Aliases | Meaning |
|----------|---------|---------|
| `AND` | `and`, `&&` | Both sides must be true |
| `OR` | `or`, `\|\|` | At least one side must be true |
| `NOT` | `not`, `!` | Logical negation (unary prefix) |
| `^` | — | Exclusive OR |

`AND` binds more tightly than `OR`, so `A AND B OR C` = `(A AND B) OR C`.

### Range Test

```
lo TO hi expression
```

True when `lo <= expression <= hi`.  This is the most common idiom:

```
15 TO 17 Points          ## Points >= 15 AND Points <= 17
5 TO 9 Spades
```

The `TO` keyword is non-associative; it cannot be chained.

### Shape Literals

A shape literal tests the length distribution of the four suits.  Two forms exist,
distinguished by the delimiter:

**Unordered shape** — comma-separated, suits in any order:

```
[4,3,3,3]      ## any 4-3-3-3 distribution
[5,4,2,2]      ## any 5-4-2-2 distribution
```

The four numbers are the sorted suit lengths; which suit holds which length is
not constrained.

**Exact distribution** — dash-separated, order is Spades-Hearts-Diamonds-Clubs:

```
[5-4-2-2]      ## exactly S=5, H=4, D=2, C=2
[4-4-3-2]      ## exactly S=4, H=4, D=3, C=2
```

The prefix keywords `shape` / `Shape` and `pattern` / `Pattern` may optionally
precede a shape literal for readability; they do not change the semantics.
The bracket delimiter (`[n,n,n,n]` vs `[n-n-n-n]`) alone determines whether
the match is unordered or exact:

```
shape [4,3,3,3]        ## same as [4,3,3,3]
pattern [4-4-3-2]      ## same as [4-4-3-2]
```

### Rule References

`$Name` within an expression evaluates the named rule against the same hand and
returns its boolean result:

```
$Balanced := [4,3,3,3] OR [4,4,3,2] OR [5,3,3,2] ;
$1N       := (15 TO 17 Points) AND $Balanced
END
```

### Grouping

Parentheses override operator precedence in the usual way.

---

## Operator Precedence

From highest to lowest:

| Precedence | Operators |
|-----------|-----------|
| Highest | `shape`, `pattern` prefix keywords |
| | `TO` range (non-associative) |
| | `NOT`, `!` (unary) |
| | `AND`, `and`, `&&` |
| | `OR`, `or`, `\|\|` |
| | `;` (definition separator) |
| | `^` (XOR) |
| | `<`, `>`, `?=`, `!=`, `>=`, `<=` |
| | `*`, `/`, `%` *(reserved, not yet active)* |
| | `+`, `-` *(reserved, not yet active)* |
| Lowest | `:=` (definition assignment) |

---

## Card Encoding

Cards are integers 0–51.  Suit order (low to high): Spades=0, Hearts=1,
Diamonds=2, Clubs=3.  Within each suit, Ace=0, King=1, Queen=2, Jack=3,
Ten=4, Nine=5, …, Two=12.  Thus card code = suit × 13 + rank.

---

## Partnership Rules

A partnership rule is evaluated against the combined North+South hand.  All
keywords return the sum of the two hands' values (`Points` = total NS HCP,
`Spades` = combined spade length, etc.).  Card-presence queries (`Sa`, `Hk`,
etc.) always return 0 for a partnership hand.

In the Dealer, a partnership rule is applied with the `-P ruleName` flag after
all four individual hand rules have been satisfied.

---

## Full Example

```
## input.txt — 1NT opener with a fit
$balanced := [4,3,3,3] OR [4,4,3,2] OR [5,3,3,2] ;
$1N       := (15 TO 17 Points) AND $balanced ;
$resp5S   := (Spades >= 5) AND (6 TO 11 Points) ;
$fit8     := Spades >= 8
END
```

Command (Dealer):

```
dealer -P fit8 100 1N Any resp5S Any
```

Generates 100 deals where North holds a 1NT hand, South holds a 5+ spade
response hand, and the combined NS spade holding is 8+.

---

## Bidding Sequence Rules (Bidder only)

In the Bidder program, rule names that begin with `$.` are treated as
**bidding sequences** rather than hand property tests.  They define the
opening bids and continuations that make up a bidding system, building an
internal convention tree used during auction simulation.

### Sequence Name Format

```
$.<bid>.<bid>. ... .<bidN>.
```

Each `<bid>` is separated — and the whole name terminated — by a dot.  A
valid bid is one of:

| Form | Meaning |
|------|---------|
| `P` | Pass (uppercase only) |
| `1C` … `7N` | Level digit (1–7) followed by a suit letter (`C`, `D`, `H`, `S`, or `N`; case-insensitive) |

The leading `$.` marks the start of the auction from the perspective of the
seat being modelled (typically North/South).  Each subsequent dot-separated
token extends the sequence by one call.

### Examples

```
## Opening bids
$.1N.          ## 1NT opener
$.P.           ## Pass as opener

## Responses after 1NT
$.1N.2C.       ## Stayman
$.1N.2D.       ## Jacoby transfer to hearts
$.1N.2H.       ## Jacoby transfer to spades

## Continuations
$.1N.2C.2H.           ## Stayman → 2♥ response
$.1N.2S.2N.3C.P.      ## transfer sequence ending with pass
```

### Rule Body

A bidding sequence rule has the same expression syntax as any other rule.
Its body describes which hands the Bidder should assign that bid:

```
$.1N. := (15 TO 17 Points) AND $balanced ;
$.1N.2C. := Spades >= 4 OR Hearts >= 4 ;   ## Stayman asks for a major
```

### Hand Property Helpers

Rule names that do **not** begin with `$.` (e.g., `$ntop`, `$balNoMajor`,
`$Any`) are ordinary hand property rules — they test hand features and return
a boolean.  They can be referenced from within sequence rule bodies or from
other property rules via `$Name`.

```
$balanced    := [4,3,3,3] OR [4,4,3,2] OR [5,3,3,2] ;
$ntop        := (15 TO 17 Points) AND $balanced ;
$.1N.        := $ntop
END
```

The special name `$ANY` is a built-in wildcard that always evaluates to true;
it is used in Dealer command lines and Bidder configuration to mean "no
constraint on this seat".  Because the language is case-insensitive, `$any`,
`$Any`, and `$ANY` in an input file all refer to the same rule.
