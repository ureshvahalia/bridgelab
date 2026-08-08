#ifndef _SIMPLIFY_HPP_
#define _SIMPLIFY_HPP_

// Simplifies a rule tree (a void* tnode, same convention as combineRule()/
// checkHand()): folds repeated comparisons on the same atomic quantity
// within an AND-chain into a single tightened interval ("(10 TO 18 Points)
// AND (Points > 15)" -> "(16 TO 18 Points)"), propagates facts from
// enclosing ANDs down into OR branches to drop branch-local redundant
// clauses or prune always-false branches, and pushes NOT down into
// comparisons so a negated comparison (e.g. from negateRule()) folds too.
// See hand-spec.md's "Rule Simplification" section for the full writeup,
// scope, and worked examples.
//
// Returns NULL if the whole tree collapses to "always true" (matching
// checkHand(NULL)'s existing convention) -- never any other time; every
// other intermediate "true"/"false" is represented as a real leaf
// internally, since NULL only has that meaning as this function's own
// top-level return value or as an operand to combineRule()/checkHand(),
// not as a node embedded inside a tree (see the design discussion in
// hand-spec.md's "NULL vs the rest of the tree" for why).
//
// Never mutates an existing node -- only ever constructs new ones (or
// reuses an existing atomic leaf by reference, unmodified). This matters
// because $Name references splice in shared pointers (see bridge.y), so
// two different rules can share actual tree nodes; mutating one reachable
// through one path would corrupt every other reference to it. Safe on
// anything it doesn't recognize (SHAPE/PATTERN, "!=", arithmetic, an
// unresolved cross-keyword relationship, ...): unrecognized constructs are
// always passed through completely unchanged, never guessed at, so the
// worst case is "no simplification happened here", never a wrong one.
//
// Pure tree->tree; no dependency on Bidder-specific types, so usable from
// Dealer too, even though the only current caller (combineRule()) is
// Bidder-only.
void* simplifyRule (void* root);

#endif
