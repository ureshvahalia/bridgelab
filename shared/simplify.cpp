// See simplify.hpp for the full description, scope, and design rationale.
#include <climits>
#include <vector>
#include <unordered_map>
#include "tnode.h"
#include "consts.h"
#include "simplify.hpp"

// ── Building blocks (no dependency on combineRule()/negateRule() --
// those are Bidder-only; this file must stay usable by Dealer too) ────────

static TPTR
buildAnd (TPTR l, TPTR r)
{
    TPTR parent = make_leaf (TAND, TAND);
    return add_leaves (parent, l, r);
}

static TPTR
buildOr (TPTR l, TPTR r)
{
    TPTR parent = make_leaf (TOR, TOR);
    return add_leaves (parent, l, r);
}

static TPTR
buildNot (TPTR r)
{
    TPTR parent = make_leaf (TNOT, TNOT);
    return add_leaves (parent, NULL, r);
}

static TPTR
buildCmp (nodeType t, TPTR atomLeaf, long long value)
{
    TPTR parent = make_leaf (t, t);
    return add_leaves (parent, atomLeaf, make_leaf (TINT, value));
}

// Internal "true"/"false" leaves -- real nodes, not NULL (see simplify.hpp).
static TPTR trueLeaf ()  { return make_leaf (TINT, 1); }
static TPTR falseLeaf () { return make_leaf (TINT, 0); }
static bool isTrueLeaf  (TPTR n) { return (n->t_type == TINT) && (n->t_val != 0); }
static bool isFalseLeaf (TPTR n) { return (n->t_type == TINT) && (n->t_val == 0); }

static TPTR
andOfAll (const std::vector<TPTR>& items)
{
    if (items.empty ())
        return trueLeaf ();
    TPTR result = items[0];
    for (size_t i = 1; i < items.size (); i++)
        result = buildAnd (result, items[i]);
    return result;
}

static TPTR
orOfAll (const std::vector<TPTR>& items)
{
    if (items.empty ())
        return falseLeaf ();
    TPTR result = items[0];
    for (size_t i = 1; i < items.size (); i++)
        result = buildOr (result, items[i]);
    return result;
}

// ── Atom identity ───────────────────────────────────────────────────────
//
// What "quantity" a comparison's non-literal operand refers to, keyed on
// the underlying registered function pointer (kwordFnAt()/suffixFnAt())
// rather than the raw t_val index, so aliases that resolve to the same
// function (Points/tpts; Sl/Slen; Skcs/Skeycards) are recognized as the
// same atom automatically, without hand-maintaining an alias table here.
// Known gap: TKWORD "Spades" (backed by spade_len) and TSUITFUNC "Sl"
// (backed by suit_len, called with suit=SPADES) compute the same number
// via two *different* registered functions, so they are deliberately not
// unified -- documented in hand-spec.md, not silently pretended away.

enum AtomKind { ATOM_FUNC, ATOM_SPOT };

struct AtomId {
    AtomKind kind;
    void*    fn;    // ATOM_FUNC only
    int      suit;  // ATOM_FUNC only: suit index for TSUITFUNC, -1 for TKWORD
    long long spot;  // ATOM_SPOT only: card code (0..51)

    bool operator== (const AtomId& o) const
    {
        if (kind != o.kind)
            return false;
        return (kind == ATOM_SPOT) ? (spot == o.spot) : ((fn == o.fn) && (suit == o.suit));
    }
};

struct AtomIdHash {
    size_t operator() (const AtomId& a) const
    {
        if (a.kind == ATOM_SPOT)
            return std::hash<long long> () (a.spot) ^ 0x9E3779B9u;
        return std::hash<void*> () (a.fn) ^ (std::hash<int> () (a.suit) << 1);
    }
};

// Fills *id and returns true if node is a leaf Tier 1/2 knows how to fold
// comparisons on (TKWORD/TSUITFUNC/TSPOT); false for anything else
// (arithmetic, $Name references to non-trivial subtrees, ...).
static bool
atomIdOf (TPTR node, AtomId* id)
{
    switch (node->t_type)   {
    case TKWORD:
        id->kind = ATOM_FUNC;
        id->fn   = kwordFnAt ((int)node->t_val);
        id->suit = -1;
        return true;
    case TSUITFUNC:
        id->kind = ATOM_FUNC;
        id->fn   = suffixFnAt ((int)(node->t_val / SUIT_SELECTOR));
        id->suit = (int)(node->t_val % SUIT_SELECTOR);
        return true;
    case TSPOT:
        id->kind = ATOM_SPOT;
        id->spot = node->t_val;
        return true;
    default:
        return false;
    }
}

// ── Interval arithmetic ─────────────────────────────────────────────────

struct Interval {
    long long lo = LLONG_MIN;
    long long hi = LLONG_MAX;
    bool empty () const { return lo > hi; }
};

typedef std::unordered_map<AtomId, Interval, AtomIdHash> IntervalMap;

enum CmpOp { CMP_GEQ, CMP_LEQ, CMP_EQ };

struct NormCmp {
    AtomId    atom;
    CmpOp     op;
    long long value;
};

static bool
isComparisonType (nodeType t)
{
    return (t == TGT) || (t == TGEQ) || (t == TLT) || (t == TLEQ) || (t == TEQU);
}

// Recognizes a plain comparison between a foldable atom and an integer
// literal, in EITHER operand order (the grammar preserves source order
// onto t_left/t_right with no canonicalization -- "Points >= 15" and
// "15 <= Points" build differently-shaped trees). Normalizes to "atom OP
// value" form, canonicalizing TGT/TLT to TGEQ/TLEQ with an adjusted
// boundary (Points > 15 -> Points >= 16) so callers only ever need to
// handle three cases instead of five. *atomLeaf receives the actual atom
// node, for reuse when rebuilding output.
static bool
normalizeComparison (TPTR node, NormCmp* out, TPTR* atomLeaf)
{
    if (!isComparisonType (node->t_type))
        return false;
    TPTR left = node->t_left, right = node->t_right;
    if ((left == NULL) || (right == NULL))
        return false;

    AtomId atomId;
    bool atomOnLeft;
    long long litVal;
    if ((right->t_type == TINT) && atomIdOf (left, &atomId))   {
        atomOnLeft = true;
        litVal = right->t_val;
        *atomLeaf = left;
    } else if ((left->t_type == TINT) && atomIdOf (right, &atomId))   {
        atomOnLeft = false;
        litVal = left->t_val;
        *atomLeaf = right;
    } else
        return false;   // neither side is (foldable atom, literal) -- e.g. two atoms, or "!="

    out->atom = atomId;
    switch (node->t_type)   {
    case TGEQ: out->op = atomOnLeft ? CMP_GEQ : CMP_LEQ; out->value = litVal; break;
    case TLEQ: out->op = atomOnLeft ? CMP_LEQ : CMP_GEQ; out->value = litVal; break;
    case TGT:  if (atomOnLeft) { out->op = CMP_GEQ; out->value = litVal + 1; }
               else            { out->op = CMP_LEQ; out->value = litVal - 1; }
               break;
    case TLT:  if (atomOnLeft) { out->op = CMP_LEQ; out->value = litVal - 1; }
               else            { out->op = CMP_GEQ; out->value = litVal + 1; }
               break;
    case TEQU: out->op = CMP_EQ; out->value = litVal; break;
    default:   return false;
    }
    return true;
}

static void
applyCmpToInterval (Interval& iv, const NormCmp& cmp)
{
    switch (cmp.op)   {
    case CMP_GEQ: if (cmp.value > iv.lo) iv.lo = cmp.value; break;
    case CMP_LEQ: if (cmp.value < iv.hi) iv.hi = cmp.value; break;
    case CMP_EQ:  if (cmp.value > iv.lo) iv.lo = cmp.value;
                  if (cmp.value < iv.hi) iv.hi = cmp.value;
                  break;
    }
}

static Interval
intersect (const Interval& a, const Interval& b)
{
    Interval r;
    r.lo = (a.lo > b.lo) ? a.lo : b.lo;
    r.hi = (a.hi < b.hi) ? a.hi : b.hi;
    return r;
}

// The sub-interval of `local` NOT already implied by `ctx` -- i.e. what's
// actually worth emitting. Bounds fully covered by ctx collapse to
// +-infinity so buildIntervalNode() below omits them.
static Interval
nonRedundantPart (const Interval& local, const Interval& ctx)
{
    Interval out = local;
    if (out.lo <= ctx.lo) out.lo = LLONG_MIN;
    if (out.hi >= ctx.hi) out.hi = LLONG_MAX;
    return out;
}

static TPTR
buildIntervalNode (TPTR atomLeaf, const Interval& iv)
{
    if (iv.empty ())
        return falseLeaf ();
    if ((iv.lo == LLONG_MIN) && (iv.hi == LLONG_MAX))
        return trueLeaf ();
    if (iv.lo == iv.hi)
        return buildCmp (TEQU, atomLeaf, iv.lo);
    if (iv.hi == LLONG_MAX)
        return buildCmp (TGEQ, atomLeaf, iv.lo);
    if (iv.lo == LLONG_MIN)
        return buildCmp (TLEQ, atomLeaf, iv.hi);
    return buildAnd (buildCmp (TGEQ, atomLeaf, iv.lo), buildCmp (TLEQ, atomLeaf, iv.hi));
}

static Interval
lookup (const IntervalMap& ctx, const AtomId& atom)
{
    auto it = ctx.find (atom);
    return (it != ctx.end ()) ? it->second : Interval ();
}

// ── The recursive simplifier ────────────────────────────────────────────

static TPTR simplifyNode (TPTR node, const IntervalMap& context);

static void
flattenAnd (TPTR node, std::vector<TPTR>& out)
{
    if (node->t_type == TAND)   {
        if (node->t_left)  flattenAnd (node->t_left, out);
        if (node->t_right) flattenAnd (node->t_right, out);
    } else
        out.push_back (node);
}

static void
flattenOr (TPTR node, std::vector<TPTR>& out)
{
    if (node->t_type == TOR)   {
        if (node->t_left)  flattenOr (node->t_left, out);
        if (node->t_right) flattenOr (node->t_right, out);
    } else
        out.push_back (node);
}

// Flattens `node`'s AND-chain, folds every comparison-on-a-foldable-atom
// conjunct into one tightened interval per atom (Tier 1), and recurses
// into everything else (Tier 2) with context extended by what this AND
// itself has established -- so an OR sitting inside this same AND sees
// both the caller's context AND this AND's own comparisons.
static TPTR
simplifyAnd (TPTR node, const IntervalMap& context)
{
    std::vector<TPTR> conjuncts;
    flattenAnd (node, conjuncts);

    IntervalMap local;
    std::unordered_map<AtomId, TPTR, AtomIdHash> repLeaf;
    std::vector<TPTR> opaque;

    for (TPTR c : conjuncts)   {
        NormCmp cmp;
        TPTR atomLeaf = NULL;
        if (normalizeComparison (c, &cmp, &atomLeaf))   {
            applyCmpToInterval (local[cmp.atom], cmp);
            if (repLeaf.find (cmp.atom) == repLeaf.end ())
                repLeaf[cmp.atom] = atomLeaf;
        } else
            opaque.push_back (c);
    }

    // context, tightened by everything this AND's own comparisons add --
    // the view opaque conjuncts (and the contradiction check below) see.
    IntervalMap merged = context;
    for (auto& kv : local)   {
        Interval& m = merged[kv.first];
        Interval combined = intersect (m, kv.second);
        m = combined;
    }
    for (auto& kv : local)
        if (merged[kv.first].empty ())
            return falseLeaf ();   // this AND is a contradiction, with or without context's help

    std::vector<TPTR> rebuilt;
    for (auto& kv : local)   {
        Interval toEmit = nonRedundantPart (kv.second, lookup (context, kv.first));
        TPTR built = buildIntervalNode (repLeaf[kv.first], toEmit);
        if (!isTrueLeaf (built))
            rebuilt.push_back (built);
    }
    for (TPTR c : opaque)   {
        TPTR simplified = simplifyNode (c, merged);
        if (isFalseLeaf (simplified))
            return falseLeaf ();
        if (!isTrueLeaf (simplified))
            rebuilt.push_back (simplified);
    }

    return andOfAll (rebuilt);
}

// Each branch gets the SAME inherited context (an OR's branches are each
// implicitly ANDed with everything above the OR, so this is sound -- see
// hand-spec.md). A branch that folds to true collapses the whole OR to
// true; a branch that folds to false (given context) is dropped.
static TPTR
simplifyOr (TPTR node, const IntervalMap& context)
{
    std::vector<TPTR> branches;
    flattenOr (node, branches);

    std::vector<TPTR> survivors;
    for (TPTR b : branches)   {
        TPTR simplified = simplifyNode (b, context);
        if (isTrueLeaf (simplified))
            return trueLeaf ();
        if (!isFalseLeaf (simplified))
            survivors.push_back (simplified);
    }
    return orOfAll (survivors);
}

static TPTR
negateComparison (TPTR cmp)
{
    nodeType negType;
    switch (cmp->t_type)   {
    case TGEQ: negType = TLT;  break;
    case TLEQ: negType = TGT;  break;
    case TGT:  negType = TLEQ; break;
    case TLT:  negType = TGEQ; break;
    case TEQU: negType = TNEQ; break;
    case TNEQ: negType = TEQU; break;
    default:   return cmp;   // not reached: caller only calls this for the above types
    }
    TPTR parent = make_leaf (negType, negType);
    return add_leaves (parent, cmp->t_left, cmp->t_right);
}

// Pushes NOT down one level (De Morgan) or onto a comparison (turning it
// into the complementary comparison, e.g. NOT(Hearts>=4) -> Hearts<4, so
// it becomes directly foldable instead of staying opaque) and re-enters
// simplifyNode() on the result. Linear, not full distribution of AND over
// OR -- each push touches each node once, no risk of the exponential
// blowup that converting to disjunctive normal form would risk.
static TPTR
simplifyNot (TPTR node, const IntervalMap& context)
{
    TPTR inner = node->t_right;
    if (inner == NULL)
        return node;

    switch (inner->t_type)   {
    case TGT: case TGEQ: case TLT: case TLEQ: case TEQU: case TNEQ:
        return simplifyNode (negateComparison (inner), context);
    case TAND:
        return simplifyNode (buildOr (buildNot (inner->t_left), buildNot (inner->t_right)), context);
    case TOR:
        return simplifyNode (buildAnd (buildNot (inner->t_left), buildNot (inner->t_right)), context);
    case TNOT:
        return simplifyNode (inner->t_right, context);
    default:
        return node;   // NOT(something opaque) -- passthrough, unchanged
    }
}

static TPTR
simplifyNode (TPTR node, const IntervalMap& context)
{
    if (node == NULL)
        return NULL;
    switch (node->t_type)   {
    case TAND: return simplifyAnd (node, context);
    case TOR:  return simplifyOr (node, context);
    case TNOT: return simplifyNot (node, context);
    default:   {
        NormCmp cmp;
        TPTR atomLeaf = NULL;
        if (!normalizeComparison (node, &cmp, &atomLeaf))
            return node;   // TSHAPE/TPATTERN/TNEQ/arithmetic/two-atom comparisons/... -- opaque, passthrough
        Interval single;
        applyCmpToInterval (single, cmp);
        Interval ctxIv = lookup (context, cmp.atom);
        if (intersect (single, ctxIv).empty ())
            return falseLeaf ();
        return buildIntervalNode (atomLeaf, nonRedundantPart (single, ctxIv));
    }
    }
}

void*
simplifyRule (void* root)
{
    if (root == NULL)
        return NULL;
    IntervalMap emptyContext;
    TPTR result = simplifyNode ((TPTR)root, emptyContext);
    return isTrueLeaf (result) ? NULL : (void*)result;
}
