# `$ANY` in-body-reference regression fixture

`input.txt` references `$Any` from within another rule's own body
(`$X := $Any AND (Spades >= 4);`) and never defines `$Any` itself anywhere
in the file.

This specifically exercises a gap the first version of the `$ANY` built-in
had: that version injected `$ANY`'s definition into the file's definition
table *after* `yyparse()` completed (`read_rules()`,
`shared/parse_rules.cpp`). That's fine for lookups that happen after a file
is fully loaded (Bidder's `bidHand()` accumulator reset, the CLI's implicit
seat defaulting) but not for a reference to `$ANY` from *within* the same
file, since name references resolve during parsing -- before the
post-parse injection ever ran. Referencing `$Any` without defining it
locally used to fail with "undefined rule reference $ANY".

Fixed properly: `find_rule()` itself (`shared/tnode.cpp`) now special-cases
the name `"$ANY"` and returns a single, process-wide "always true" node
directly, without ever consulting the per-file definition table at all.
Every name reference -- during parsing (via `bridge.y`'s `DEFNAME`
production) or after (Bidder/Dealer's own lookups) -- goes through this
same function, so this works everywhere uniformly, regardless of parse
order, and regardless of whether the file defines `$ANY` itself (any such
definition is simply never consulted, by this function or anything else --
see `any_redefine/README.md`).

Expected: load and run succeed (exit 0), producing real results.
