# Illegal-bid regression fixture

`input.txt` defines `$.1S.1H.` — a continuation where South "responds" 1H
over North's 1S, which is illegal (hearts ranks below spades at the same
level). North is constrained to always qualify for `$.1S.` and South to
always qualify for `$.1S.1H.`, so the tree match is deterministic.

Repro command:

    bidlab -i input.txt -o out.csv -nchecks 8 5 OpenS Resp4H

`before_fix.csv` is the captured output before the bid-legality check was
added: every row shows `Bidding = 1S-1H` (contradicting the ascending-rank
rule of bridge) alongside `Contract = 1S - N`, as if South had passed.

Fixed: `biddingSystem::processRule` (bidlab.cpp) now checks that every real
call in a `$.`-sequence ranks strictly higher than the previous real call in
that same sequence (`Pass` is exempt). Loading this file now fails fast:

    illegal bid sequence in rule $.1S.1H.: a call must rank higher than every earlier call in the same auction

exit code 1, no output written. See `run_tests.sh` for the automated check
(asserts non-zero exit and absence of `1S-1H` in any output produced).
