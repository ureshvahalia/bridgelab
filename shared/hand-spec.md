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

A missing `;` between two definitions is a load-time syntax error (exit 1),
not a silent truncation — earlier versions of the parser could, for a
missing `;`, silently stop reading the file at that point with no error at
all, discarding every definition after it.

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

### Redefinition

`$Name` may be defined more than once with `:=`.  A later definition does
not retroactively change anything that already referenced the name — a
`$Name` reference always resolves to whichever definition of `$Name` was
most recent *at that point in the file* (the same sequential, no-forward-
reference rule that governs everything else), and is unaffected by any
redefinition that happens later:

```
$Weak := (0 TO 7 Points);
$X     := $Weak;              ## $X captures the first $Weak, permanently
$Weak := (0 TO 10 Points);    ## warns: redefining $Weak
$Y     := $Weak;              ## $Y captures the second $Weak
```

Redefining a name is usually a copy-paste accident rather than a deliberate
choice, so it is not silent: it prints a `warning: redefining $Name` to
stderr but still loads the file (exit 0).

### Modifying a definition: `:&` and `:|`

```
$Name :& expression   ## AND expression onto $Name's current definition
$Name :| expression   ## OR expression onto $Name's current definition
```

Unlike `:=`, both operators require `$Name` to already be defined — modifying
a name that doesn't exist yet is a load-time error, the same as referencing
an undefined `$Name`. Where `:=` replaces a definition outright, `:&`/`:|`
extend the *current* one in place: `$Name :& expr` is equivalent to
redefining `$Name` as `($Name's current body) AND (expr)` (and similarly
`OR` for `:|`), without having to spell the current body out again. This is
most useful for refining one case out of several that already share most of
their logic — see the Maj/Min example below.

Like a plain `:=` redefinition, `:&`/`:|` only affect `$Name` references made
*after* that point in the file; anything that already referenced `$Name`
keeps whatever `$Name` meant at the time. Unlike `:=`, `:&`/`:|` don't warn —
requiring the name to already exist makes accidental collisions much less
likely, and modifying an existing definition is the whole point of the
operators.

`:&` and `:|` can be applied to the same name repeatedly, and each wraps the
*entire current definition* at the moment it runs, so — unlike a plain chain
of `AND`/`OR` inside one expression — the order of the statements matters
whenever `:&` and `:|` are mixed on the same name:

```
$Z := A;
$Z :& B;   ## $Z is now (A AND B)
$Z :| C;   ## $Z is now ((A AND B) OR C)
```

reads differently from the same three clauses applied in the other order,
which would give `((A OR C) AND B)` instead. A run of the *same* operator is
order-independent (`AND`/`OR` are each associative on their own), only
mixing the two is order-sensitive.

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
| `NLTC` | New Losing Trick Count (Koelman 2003 / Klinger), **doubled** to keep the language's integer arithmetic: missing Ace = 3, missing King = 2, missing Queen = 1 (the conventional value is 1.5/1.0/0.5 losers each — divide by 2 to get it), capped per suit at 3.0 real losers (6 doubled) same as classic LTC. On a partnership rule (see below), silently evaluates to the worst case for the combined suit lengths, since it's built from the same `haveCard`-based mechanism as the `Sa`/`Hk`/etc. spot-card keywords, which are `false` for every card on a combined hand. |

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
| Lowest | `:=`, `:&`, `:|` (definition assignment / modification) |

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
etc.) always return 0 for a partnership hand.  `NLTC` is built on the same
card-presence check, so it silently evaluates to the worst-case (maximum
losers) value for the combined suit lengths rather than a meaningful
partnership losing-trick count — no error is raised.

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

### Bid-Sequence Legality

Every `$.`-prefixed name is checked when the rules file is loaded: each real
call (anything other than `P`) must rank strictly higher than every earlier
real call in that same sequence — bids must ascend, exactly as in a real
auction (`C < D < H < S < NT` within a level, then level increases). A
hand-typed name that violates this, such as `$.1S.1H.` (hearts below spades
at the same level), is rejected at load time with an error identifying the
rule. This check applies in both Bidder and Dealer, and also governs which
generated variants survive Maj/Min macro expansion — see below.

---

## Suit-Pair Macros: Maj / Min / OMaj / OMin / BMaj / BMin

These six keywords (plus their full spellings `Major`, `Minor`, `OMajor`,
`OMinor`, `BothMajors`, `BothMinors` — all case-insensitive, exact aliases)
let a single rule stand in for a pattern that's the same shape in either
major or either minor suit, instead of writing it out twice by hand. They
are resolved by a preprocessing pass (`shared/majMinExpand.cpp`) that runs
**before** the rules file is parsed: by the time the parser sees the file,
every macro has already been expanded into ordinary rules using only real
suit names. This preprocessing is shared by both Dealer and Bidder — the
`$.`-prefixed bid-token forking described below works identically in both,
even though only Bidder does anything with an auction/convention tree
afterward. Dealer just ends up with more ordinary named rules to reference
from the command line.

| Keyword | Full spelling | Meaning |
|---------|---------------|---------|
| `Maj`   | `Major`   | One major suit — which one is resolved per usage (see below) |
| `OMaj`  | `OMajor`  | The *other* major — whichever suit `Maj` did **not** resolve to |
| `Min`   | `Minor`   | One minor suit, same idea as `Maj` |
| `OMin`  | `OMinor`  | The other minor |
| `BMaj`  | `BothMajors` | Both major suits at once (body-only — see below) |
| `BMin`  | `BothMinors` | Both minor suits at once (body-only — see below) |

### `OMaj`/`OMin` require an established `Maj`/`Min`

`OMaj` only means something relative to an already-chosen `Maj` — "the other
one" needs a "the one" to be the other of. So within a single rule's own
text (its name, then its body, read in that order), the **first** occurrence
of the pair must be `Maj`/`Major`; any `OMaj`/`OMajor` appearing before that
is rejected at load time. The same rule applies independently to `Min`/`Minor`
vs. `OMin`/`OMinor`. `BMaj`/`BMin` are unrelated to this — they don't
establish or consume a `Maj`/`Min` anchor at all.

```
$Y := (Omaj > 3);
## rejected: OMAJ used before a preceding MAJ/MAJOR — nothing establishes
## which suit is "the major" here
```

### Two different expansions, depending on where the macro appears

**In a bid-token position** (Bidder-style `$.` sequence names), `Maj`/`Min`
must resolve to one concrete, callable suit, so using one there **forks the
rule into two concrete rules**, substituting consistently through that
rule's whole name and body:

```
$.1N.2C.2Maj. := (Maj > 3) AND (OMaj < 4);
```
expands to:
```
$.1N.2C.2H. := (Hearts > 3) AND (Spades < 4);
$.1N.2C.2S. := (Spades > 3) AND (Hearts < 4);
```

`:&` and `:|` fork the same way `:=` does — the expanded name is what
matters, not which of the three operators produced it. This makes it
possible to give one forked variant a follow-up refinement without touching
the other: show a 4-card major over Stayman, but with both majors held,
always rebid 2H rather than 2S:

```
$.1N.2C.2Maj. := (Maj > 3);
$.1N.2C.2S.   :& (Hearts < 4);
```
expands to:
```
$.1N.2C.2H. := (Hearts > 3);
$.1N.2C.2S. := (Spades > 3);
$.1N.2C.2S. :& (Hearts < 4);
```
which the parser then resolves to `$.1N.2C.2S. := (Spades > 3) AND (Hearts <
4)`, leaving `$.1N.2C.2H.` untouched — without having to duplicate `Hearts <
4` by hand into a `$.1N.2C.2S. := (Hearts < 4) AND (Spades > 3);` definition.
`$.1N.2C.2Maj. :& expr` (modifying, rather than defining, an already-forked
name) works the same way, applying to both forks independently — a `:&`/`:|`
statement's own name can use `Maj`/`Min`/`OMaj`/`OMin` exactly like a `:=`
statement's can.

If a rule's name doesn't use the macro but an **earlier** bid in that same
name did, later body-only references still resolve against that established
choice rather than forking again — the name is always spelled out in full,
so the anchor is always visible in the rule's own text:

```
$.1Maj.     := (11 TO 21 Points) AND (Maj >= 5);
$.1Maj.1N.  := (6 TO 9 Points) AND (Omaj < 3);
```
expands to:
```
$.1H.       := (11 TO 21 Points) AND (Hearts >= 5);
$.1S.       := (11 TO 21 Points) AND (Spades >= 5);
$.1H.1N.    := (6 TO 9 Points) AND (Spades < 3);
$.1S.1N.    := (6 TO 9 Points) AND (Hearts < 3);
```

`BMaj`/`BMin` cannot appear in a bid-token position at all ("both majors"
isn't a callable bid) — `$.1BMaj.` is rejected at load time.

**In a rule body with no name anchor at all** (an ordinary hand-property
rule, or a `$.` rule whose own name doesn't use the pair), there's no single
suit to resolve to, so the *entire* body is duplicated once per suit and the
copies are OR'd together — the whole enclosing rule body, never a smaller
sub-expression, which is what correctly keeps a `Maj` and its matching
`OMaj` elsewhere in the same body pointing at complementary suits instead of
being resolved independently:

```
$X := (Maj ?= 5) AND (Omaj ?= 4);
```
expands to:
```
$X := ((Spades ?= 5) AND (Hearts ?= 4)) OR ((Hearts ?= 5) AND (Spades ?= 4));
```

If both a Major-pair and a Minor-pair usage are unanchored in the same body,
all four combinations are generated and OR'd together.

### Illegal generated sequences are pruned, not fatal

When a name fork produces a `$.`-sequence that fails the ascending-rank
legality check (e.g. `$.1Maj.1Omaj.` — one of its two substitutions is
`$.1S.1H.`, illegal), that one variant is dropped with a warning and the
other, legal variant is kept; the file still loads. This is different from
a *hand-typed* illegal sequence with no macro involved, which is a hard,
fatal error — see "Bid-Sequence Legality" above.

### `BMaj`/`BMin`: both suits, body-only, always as their own parenthesized comparison

`BMaj`/`BMin` mean "both suits of the pair, ANDed" — deterministic, no suit
choice to anchor, so no ordering rule applies to them. But because the
substitution has to know exactly which comparison it's replacing, every
`BMaj`/`BMin` usage **must** be written as its own complete, parenthesized
comparison — `(BMaj > 3)`, never `BMaj > 3` bare or folded into a larger
parenthesized group. This is enforced at load time:

```
$W := BMaj > 3;
## rejected: BMAJ must be written as its own parenthesized comparison,
## e.g. "(BMAJ > 3)"

$Z := (BMaj > 3);
## expands to: $Z := ((Spades > 3) AND (Hearts > 3));
```

Avoid wrapping a `Maj`/`OMaj`/`BMaj`/`BMin` comparison directly in `NOT` if
you want the "obvious" negated meaning. These macros expand mechanically
(substitute, then combine with OR or AND as described above); the mechanical
result of negating that expansion is not the same as negating the *original*
existential ("some suit") or universal ("both suits") claim the macro
represents. Concretely, `NOT (BMaj > 3)` expands to `(NOT(Spades>3)) AND
(NOT(Hearts>3))` — "neither suit exceeds 3" — not the "at least one suit
doesn't exceed 3" you'd get by properly negating "both exceed 3". If you
need the negated reading, write it out with real suit names instead of
negating the macro directly.

### Referencing a Maj/Min-forked bid-sequence name from another rule's body

A `$.`-name that uses `Maj`/`Min`/`OMaj`/`OMin` in its own name forks into
several concrete rules (see above) — after expansion, the macro-form name
itself (e.g. `$.2Major.`) is never defined, only the concrete forks it
produced (`$.2H.`, `$.2S.`). If another rule's *body* needs to refer to "the
2-major-preempt rule, whichever suit it turned out to be," writing the
macro-form name there is handled automatically: a body-side `$`-reference
that is itself bid-sequence-shaped and contains a Maj/Min-family token is
replaced with a parenthesized OR of every concrete name it forks into,
before that rule's own body is otherwise processed.

```
$.2Major. := (5 TO 10 Points) AND (Major ?= 6) AND (OMajor < 4) ;
$.3Minor. := (5 TO 10 Points) AND (Minor ?= 7) AND (OMinor < 4) ;
$.3Major. := (5 TO 10 Points) AND (Major >= 7) AND (OMajor < 4) ;
$.P.      := (Points < 11) AND NOT ($.2Major. OR $.3Minor. OR $.3Major.) ;
```

`$.P.`'s body expands the three references before anything else runs on it,
giving (schematically) `NOT (($.2H. OR $.2S.) OR ($.3D. OR $.3C.) OR ($.3H.
OR $.3S.))` — "pass unless this hand would have opened one of the
major/minor preempts defined above."

This only applies to a reference that is both bid-sequence-shaped (matches
the `$.<bid>.<bid>....` name grammar) and uses a Maj/Min-family token — an
ordinary property reference (`$balanced`) or an already-concrete
bid-sequence reference (`$.1N.2C.2H.`) is left exactly as written, same as
today. The anchor rule (first occurrence of a pair must be `Maj`/`Min`, not
`OMaj`/`OMin`) applies to the reference's *own* tokens only — a reference
can't borrow an anchor established by the enclosing rule's name or an
unrelated earlier body occurrence, so a reference's meaning is always
readable from the reference itself rather than depending on where it
happens to appear.

### Debugging expansions

Every expansion Bidder or Dealer performs is written back out, fully
resolved, to `<rules-file>.expanded.txt` next to the original file — a
normal, re-parseable rules file with every macro already gone. In Debug
builds (`-DDEBUG2`), each expansion is also traced to stderr as it happens,
e.g. `[majMinExpand] $.1N.2C.2Maj. -> $.1N.2C.2S., $.1N.2C.2H.,`.
