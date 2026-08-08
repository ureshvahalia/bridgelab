/* Generic binary tree operations */
/* Each leaf has a user defined type and value field, */
/* and pointers to left and right subtrees. */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <unordered_map>
#include "tnode.h"
#include "log.h"
#include "simplify.hpp"

TPTR defroot;	/* root of definitions tree */

/* One name->node map per definition tree, keyed by that tree's own root
 * pointer (the value defroot holds once its first definition is created).
 * read_rules() resets defroot to NULL and parses a fresh, independent tree
 * on every call — e.g. once per bidding-system file loaded by bidlab — and
 * each of those trees stays valid and separately queryable via its own root
 * for the life of the process (nothing here is ever freed), so lookups must
 * stay scoped to the tree they were asked about rather than always
 * reflecting whichever tree happened to be parsed most recently. */
static std::unordered_map<TPTR, std::unordered_map<std::string, TPTR>> defIndex;

// ── Transient-node arena ────────────────────────────────────────────────
//
// combineRule()/negateRule()/simplifyRule() never mutate an existing node
// (see simplify.hpp) -- correctness requires that, since the rule they're
// combining is a pointer into the single, process-lifetime rule-definition
// tree (findRule()'s result, reused by every hand that ever reaches that
// bid, for the life of the process), spliced in by reference. But that
// means every node THEY construct is transient: it belongs only to the
// current hand's bidding accumulator, is rebuilt from scratch by the next
// combine, and is never reachable once that hand's auction is discarded.
// Nothing ever frees it on its own -- this arena does, in bulk, without
// needing to know which nodes in a given tree are "ours" (transient) vs.
// spliced in from the permanent tree: tnodeArenaBegin()/tnodeArenaEnd()
// bracket one hand's worth of
// accumulator-building (see bidlab.cpp's main()), and make_leaf() routes
// through the arena instead of malloc() only while one is active, so
// nothing built at rule-load time (or by the self-test harness) is ever at
// risk of being swept up in a reset -- those calls happen with no arena
// active and keep using plain malloc(), same as before this existed.
//
// Not thread-safe: the only callers of make_leaf() during bidding
// (combineRule()/negateRule(), via findMatchingChild()) run on bidlab's
// single-threaded auction loop -- -fopenmp in the build is only for
// linking against the DDS library, never used in this codebase's own code.
static const size_t ARENA_BLOCK_NODES = 65536;   // ~68MB/block -- comfortably covers one hand's node churn across all 3 systems (observed ~40-50K nodes/rep) before a second block is needed

struct tnodeArenaBlock {
    tnodeArenaBlock* next;
    size_t           used;       // nodes handed out from this block so far
    size_t           capacity;
    struct tnode*    nodes;      // malloc'd once, reused block-to-block across hands
};

static tnodeArenaBlock* g_arenaBlocks  = NULL;   // every block ever allocated -- kept (not freed) for reuse by later hands
static tnodeArenaBlock* g_arenaCurrent = NULL;   // block currently being filled; NULL means the arena is inactive (make_leaf() falls back to malloc())

static tnodeArenaBlock*
tnodeArenaNewBlock (size_t capacity)
{
    tnodeArenaBlock* b = (tnodeArenaBlock*)malloc (sizeof (tnodeArenaBlock));
    b->next     = NULL;
    b->used     = 0;
    b->capacity = capacity;
    b->nodes    = (struct tnode*)malloc (capacity * sizeof (struct tnode));
    return b;
}

// Activates the arena: make_leaf() allocates from it until tnodeArenaEnd().
// Safe to call repeatedly (e.g. once per hand) -- reuses the same blocks
// every time rather than growing without bound, by resetting `used` back
// to 0 on each one instead of freeing/reallocating.
void
tnodeArenaBegin (void)
{
    if (g_arenaBlocks == NULL)
        g_arenaBlocks = tnodeArenaNewBlock (ARENA_BLOCK_NODES);
    for (tnodeArenaBlock* b = g_arenaBlocks; b != NULL; b = b->next)
        b->used = 0;
    g_arenaCurrent = g_arenaBlocks;
}

// Deactivates the arena -- make_leaf() falls back to malloc() again. Does
// NOT free the blocks (kept around for the next tnodeArenaBegin() to
// reuse); the nodes handed out during this generation stay valid data
// until that next call resets `used` and starts overwriting them, which is
// safe because nothing outlives the hand that built it (see above).
void
tnodeArenaEnd (void)
{
    g_arenaCurrent = NULL;
}

static inline TPTR
tnodeArenaAlloc (void)
{
    if (g_arenaCurrent->used == g_arenaCurrent->capacity)   {
        if (g_arenaCurrent->next == NULL)
            g_arenaCurrent->next = tnodeArenaNewBlock (ARENA_BLOCK_NODES);
        g_arenaCurrent = g_arenaCurrent->next;
    }
    return &g_arenaCurrent->nodes[g_arenaCurrent->used++];
}

static const char* typeNames[] = {
    "TINT",
    "TASSIGN",
    "TKWORD",
    "TSUITFUNC",
    "TSPOT",
    "TSHAPE",
    "TPATTERN",
    "TNEG",
    "TPLUS",
    "TMINUS",
    "TMULT",
    "TDIV",
    "TMOD",
    "TBITAND",
    "TBITOR",
    "TEQU",
    "TNEQ",
    "TGT",
    "TGEQ",
    "TLT",
    "TLEQ",
    "TNOT",
    "TAND",
    "TOR",
    "TXOR",
    "TDEFINE",
    "TDEFNAME"
};

/* Make a new leaf containing the given information */
TPTR
make_leaf (enum nodeType type, long long val)
{
	TPTR leaf;

	logDebug ("make_leaf: type %d, val %llx\n", type, val);
	leaf = (g_arenaCurrent != NULL) ? tnodeArenaAlloc () : (TPTR)malloc (sizeof (struct tnode));
	leaf->t_type = type;
	leaf->t_val  = val;
	leaf->t_left = leaf->t_right = (TPTR)0;	/* No children yet */
	leaf->t_result = 0;
	write_leaf (leaf);
	logDebug ("leaf at %p: type %s, val 0x%llX\n", leaf, typeNames[leaf->t_type], leaf->t_val);
	return leaf;
}


/* Add left and right subtrees to parent */
TPTR
add_leaves (TPTR to, TPTR l, TPTR r)
{
	logDebug ("add_leaves: to %p, l %p, r %p\n", to, l, r);
	if (l != NULL)
	    to->t_left = l;
	if (r != NULL)
	    to->t_right = r;
    write_node (to);
	logDebug ("Adding to leaf %p (type %s, val 0x%llX): left %p (type %s, val 0x%llX), right %p (type %s, val 0x%llX)\n",
		to, typeNames[to->t_type], to->t_val, l, l ? typeNames[l->t_type] : 0, l ? l->t_val : 0,
		r, r ? typeNames[r->t_type] : 0, r ? r->t_val : 0);
	return to;
}

/* Traverse the tree in lrt order, calling the function action */
/* on each node.  The right subtree is evaluated only if the */
/* test function is NULL or returns success. */
/* Note: lrt = left subtree, then right subtree, then top (this) node */
int
traverse_lrt (TPTR rootp, void (*action)(TPTR, int, int, void*), int level,
              int which, int (*test)(TPTR, int), void* argp)
{
	int result = 0;
	if (rootp->t_left)	/* traverse left subtree */
		result = traverse_lrt (rootp->t_left, action, level + 1, LEFTNODE, test, argp);
	if (!test || (*test) (rootp, result))	{
		/* evaluate right hand side also */
		if (rootp->t_right)		/* traverse right subtree */
			traverse_lrt (rootp->t_right, action, level + 1, RIGHTNODE, test, argp);
		/* now evaluate this node */
		action (rootp, level, which, argp);
	} else		/* test failed, use result of left side only */
		rootp->t_result = result;
	return (rootp->t_result);
}

/* Names are not required to be unique in a definition tree (a name may be
 * redefined with ":="): index_def below always overwrites any previous
 * entry for the same name in the same tree, so find_def_node/find_rule
 * always return the most recently defined match — a later ":=" naturally
 * supersedes an earlier one for anyone who looks the name up after that
 * point. Nodes already spliced into an earlier reference's tree (via a
 * prior find_rule call) hold a copy of the old t_right pointer and are
 * unaffected by a later redefinition. */
void*
find_def_node (void* defp, const char* name)
{
    if (defp == NULL || name == NULL)
        return NULL;
    auto treeIt = defIndex.find ((TPTR)defp);
    if (treeIt == defIndex.end())
        return NULL;
    const std::unordered_map<std::string, TPTR>& names = treeIt->second;
    auto nameIt = names.find (name);
    return (nameIt != names.end()) ? (void*)nameIt->second : NULL;
}

// $ANY is a language-level built-in meaning "always true" (see
// hand-spec.md), resolved here -- rather than via the normal per-file
// defIndex lookup below -- so it works everywhere a name can be
// referenced, not just after a file is fully loaded: a rule body written
// as "$X := $Any AND (Spades >= 4);" resolves this during parsing, via
// this same function (see bridge.y's DEFNAME-as-expression production),
// before defroot may even be set yet (e.g. if $Any is referenced by the
// very first definition in the file). Works regardless of whether the
// file defines $ANY/$Any/$any itself -- any such definition is simply
// never consulted, here or anywhere else, since every caller reaches
// this name through find_rule(), never find_def_node() directly.
//
// One process-wide node, never mutated in place: find_def_node() (used by
// ":&"/":|" to modify a node's t_right) is deliberately left untouched by
// this special case, so "$ANY :& expr" still requires (and modifies) a
// definition the file wrote itself -- it can never reach and corrupt the
// shared node returned here.
static TPTR
builtinAnyNode ()
{
    static TPTR node = make_leaf (TINT, 1);
    return node;
}

void*
find_rule (void* defp, const char* name)
{
    if ((name != NULL) && (strcmp (name, "$ANY") == 0))
        return builtinAnyNode ();
    TPTR nodep = (TPTR)find_def_node (defp, name);
    return nodep ? nodep->t_right : NULL;
}

/* Registers node under name in the tree defroot currently identifies — call
 * once per ":=", right after defroot is established (see bridge.y). Never
 * called for ":&"/":|", which modify an existing node's t_right in place
 * rather than creating a new one, so the name already maps to it. */
void
index_def (const char* name, void* node)
{
    defIndex[defroot][name] = (TPTR)node;
}

// NULL is the codebase-wide convention for "no rule" / "no constraint" /
// true (see checkHand(NULL)) -- but only where something explicitly checks
// for it. add_leaves() does not: a NULL child left embedded in a TAND/TOR
// node is not "true" to the generic tree walker, it's just an absent
// child, and traverse_lrt()/eval_node() do not agree on what that means
// (a NULL t_left silently reads as false under TAND, but crashes under
// TOR -- see write_node(), whose TAND/TOR cases dereference both sides'
// t_desc unconditionally). So NULL is resolved to its true meaning HERE,
// at the one place rules actually get combined, before it can ever become
// a node's child -- not by trying to make traverse_lrt()/eval_node()
// tolerate a NULL child in general, which would mean deciding a sensible
// default for every other operator (TPLUS, TGEQ, ...) too, for cases that
// structurally can't occur if this function is the sole entry point.
void*
combineRule (void* l, void* r)
{
    if (l == NULL) return r;
    if (r == NULL) return l;
    TPTR parent = make_leaf (TAND, TAND);
    TPTR combined = add_leaves (parent, (TPTR)l, (TPTR)r);
    return simplifyRule (combined);   // see simplify.hpp
}

// Same construction the parser uses for "NOT expr"/"!expr" (see bridge.y) --
// negateRule just builds that node programmatically instead of parsing it.
// NOT(NULL) = NOT(true) = always false, but NULL itself can't represent
// "false" anywhere (it already means "true" -- see combineRule() above),
// so this returns a real leaf rather than propagating NULL through.
void*
negateRule (void* r)
{
    if (r == NULL)
        return make_leaf (TINT, 0);
    TPTR parent = make_leaf (TNOT, TNOT);
    return add_leaves (parent, (TPTR)0, (TPTR)r);
}

void*
next_rule (void* t)
{
    return ((TPTR)t)->t_left;
}

char*
rule_name (void* t)
{
    return (char*)(((TPTR)t)->t_val);
}

void*
rule_def (void* t)
{
    return ((TPTR)t)->t_right;
}
