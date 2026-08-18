#ifndef _MAJMINEXPAND_INTERNAL_HPP_
#define _MAJMINEXPAND_INTERNAL_HPP_

#include <string>

// Narrow, purpose-built exposure of majMinExpand.cpp's bid-token name
// analysis for shared/trumpAskExpand.cpp's ask-template grafting step.
//
// Grafting synthesizes new "$."-shaped rule names by concatenating an
// already-legal attachment path with a template's relative bid tokens (see
// shared/hand-spec.md's "Ask templates"). A synthesized name should be held
// to exactly the same bid-ranking-legality standard expandMajMinMacros()
// already enforces on every hand-typed name (shared/majMinExpand.cpp's
// isLegalBidSequence()) -- not a weaker one just because it was assembled
// by the preprocessor rather than typed by hand. Rather than expose (and
// so couple trumpAskExpand.cpp to) majMinExpand.cpp's internal
// BidNameToken/Suit/KeywordKind types, this is the one predicate grafting
// actually needs.
//
// Returns true if `name` is bid-sequence-shaped ("$." + dot-separated bid
// tokens) AND every real call in it ranks higher than every earlier one.
// Grafted names are always fully concrete by construction (ask-templates
// don't support Maj/Min placeholders in their own relative tokens -- see
// hand-spec.md), so this also returns false if `name` contains one; that
// should never happen for a name this function is actually called on, but
// guards against a latent bug producing a silently-wrong tree instead of a
// loud one.
bool isWellFormedConcreteBidSequenceName (const std::string& name);

#endif  // _MAJMINEXPAND_INTERNAL_HPP_
