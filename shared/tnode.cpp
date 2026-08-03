/* Generic binary tree operations */
/* Each leaf has a user defined type and value field, */
/* and pointers to left and right subtrees. */

#include <stdlib.h>
#include <stdio.h>
#include <string>
#include <unordered_map>
#include "tnode.h"
#include "log.h"

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
	leaf = (TPTR)malloc (sizeof (struct tnode));
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

void*
find_rule (void* defp, const char* name)
{
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

void*
combineRule (void* l, void* r)
{
    TPTR parent = make_leaf (TAND, TAND);
    return add_leaves (parent, (TPTR)l, (TPTR)r);
}

// Same construction the parser uses for "NOT expr"/"!expr" (see bridge.y) --
// negateRule just builds that node programmatically instead of parsing it.
void*
negateRule (void* r)
{
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
