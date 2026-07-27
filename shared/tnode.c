/* Generic binary tree operations */
/* Each leaf has a user defined type and value field, */
/* and pointers to left and right subtrees. */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "tnode.h"

TPTR defroot;	/* root of definitions tree */

#ifdef DEBUG
static char* typeNames[] = {
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
#endif

/* Make a new leaf containing the given information */
TPTR
make_leaf (enum nodeType type, long long val)
{
	TPTR leaf;

#ifdef DEBUG
	printf ("make_leaf: type %d, val %llx\n", type, val);
#endif
	leaf = (TPTR)malloc (sizeof (struct tnode));
	leaf->t_type = type;
	leaf->t_val  = val;
	leaf->t_left = leaf->t_right = (TPTR)0;	/* No children yet */
	leaf->t_result = 0;
	write_leaf (leaf);
#ifdef DEBUG
	printf ("leaf at %p: type %s, val 0x%llX\n", leaf, typeNames[leaf->t_type], leaf->t_val);
#endif
	return leaf;
}


/* Add left and right subtrees to parent */
TPTR
add_leaves (TPTR to, TPTR l, TPTR r)
{
#ifdef DEBUG
	printf ("add_leaves: to %p, l %p, r %p\n", to, l, r);
#endif
	if (l != NULL)
	    to->t_left = l;
	if (r != NULL)
	    to->t_right = r;
    write_node (to);
#ifdef DEBUG
	printf ("Adding to leaf %p (type %s, val 0x%llX): left %p (type %s, val 0x%llX), right %p (type %s, val 0x%llX)\n",
		to, typeNames[to->t_type], to->t_val, l, l ? typeNames[l->t_type] : 0, l ? l->t_val : 0,
		r, r ? typeNames[r->t_type] : 0, r ? r->t_val : 0);
#endif
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

/* Names are not required to be unique in the definition chain (a name may
 * be redefined with ":="). find_def_node/find_rule always return the most
 * recently defined match — the chain is walked in full, oldest first via
 * t_left, remembering the last hit rather than returning on the first,
 * so a later ":=" naturally supersedes an earlier one for anyone who looks
 * the name up after that point. Nodes already spliced into an earlier
 * reference's tree (via a prior find_rule call) hold a copy of the old
 * t_right pointer and are unaffected by a later redefinition. */
void*
find_def_node (void* defp, const char* name)
{
    if (name == NULL)
        return NULL;
    TPTR nodep = (TPTR)defp;
    TPTR found = NULL;
    while (nodep != NULL)	{
        if (strcmp ((char*)(nodep->t_val), name) == 0)
            found = nodep;
        nodep = nodep->t_left;
    }
    return found;
}

void*
find_rule (void* defp, const char* name)
{
    TPTR nodep = (TPTR)find_def_node (defp, name);
    return nodep ? nodep->t_right : NULL;
}

void*
combineRule (void* l, void* r)
{
    TPTR parent = make_leaf (TAND, TAND);
    return add_leaves (parent, (TPTR)l, (TPTR)r);
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
