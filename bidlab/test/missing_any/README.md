# Missing-`$ANY` regression fixture

`input.txt` defines a working convention tree (`$.1N.` / `$.P.`) but never
defines `$ANY`. This file's own content hasn't changed since it was first
added, but what it's *expected* to do has flipped.

**History**: originally, this was a crash repro. `bidHand()` unconditionally
resets `rules[bidder]` from `sp->findRule("$ANY")` at the start of every
auction, and that value later reached `combineRule()`, which assumed a
non-`NULL` operand (`write_node()`'s `TAND` case dereferenced both sides'
`t_desc` unconditionally, unlike `checkHand(NULL)`, which explicitly treats
`NULL` as "no constraint"). A missing `$ANY` crashed two different ways
depending on the command line -- an assert in `bidderDeal::dealAndCheck`,
or an outright segfault inside `combineRule()`, intermittently, only once a
dealt hand matched the very first sibling rule in the tree.

That was first fixed by rejecting a missing `$ANY` at load time with a
clear error. But `$ANY` is now a true language-level built-in instead:
`read_rules()` (`shared/parse_rules.cpp`) injects a definition for it into
every loaded file's definition table right after parsing, unconditionally
(see `any_redefine/README.md` for what happens if a file tries to define
its own). A rules file no longer needs to define `$ANY` at all -- so this
fixture now exercises exactly that: loading and running a file that omits
it should just work, using the built-in "always true" meaning, the same
way it would if the file *had* defined `$Any := (Points >= 0);` itself.

Separately, `combineRule()`/`negateRule()` were also made tolerant of a
`NULL` operand regardless (treating it the same way `checkHand(NULL)`
already did) -- defense in depth for any future code path that might
combine a possibly-absent rule, not specific to `$ANY`. See
`bidlab --self-test` and its entry in `run_tests.sh`.
