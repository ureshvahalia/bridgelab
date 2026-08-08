# `$ANY`-redefinition regression fixture

`$ANY` is a language-level built-in (see `missing_any/README.md`) --
`read_rules()` injects its "always true" definition into every loaded
file's definition table right after parsing, unconditionally overriding
anything the file itself tried to define for that name.

`input.txt` tries to redefine it as `(Points > 100)`, a condition no real
13-card hand can ever satisfy. This is deliberately chosen so the test can
verify the redefinition was actually *dropped*, not silently honored: if it
had taken effect, `dealAndCheck`'s rejection-sampling loop (used here with
no positional rule args, so N/S default to `$ANY`) could never find a
matching hand and would exhaust its retry budget instead of completing.
Completing quickly with real output is itself the proof the built-in
meaning won.

The drop is silent -- no warning -- deliberately: see the comment on
`injectBuiltinAny()` in `shared/parse_rules.cpp` for why (every rules file
predating the `$ANY` built-in, including this project's own other test
fixtures, defines `$Any` itself, since that used to be required; that's
harmless boilerplate now, not a mistake worth flagging).

Expected: load succeeds (exit 0), no warning or error output, and
dealing/bidding completes normally, producing real hands (including ones
with far fewer than 100 points) in `results.csv`.
