/* Hand evaluation functions shared by Bidder and Dealer */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "tnode.h"
#include "translations.h"
#include "pack.hpp"
#include "handInfo.hpp"
#include "log.h"

void
aHand::deal ()
{
    // Do NOT memset the whole object — that would corrupt the vtable pointer.
    cards = 0; points = 0; controls = 0;
    memset (pat, 0, sizeof pat);
    memset (suitPts, 0, sizeof suitPts);
    memset (shape, 0, sizeof shape);
    thePack.deal_hand (h);
    process ();
}

char*
aHand::dealFromPBN (const char* pbnStr)
{
    // Do NOT memset the whole object — that would corrupt the vtable pointer.
    cards = 0; points = 0; controls = 0;
    memset (pat, 0, sizeof pat);
    memset (suitPts, 0, sizeof suitPts);
    memset (shape, 0, sizeof shape);
    char* s = PBN2oneHand (pbnStr, h);
    if (s != NULL)  {
        const int* const hEnd = h + NCARDS_IN_HAND;
        for (int* hp = h; hp < hEnd; hp++)
            if (thePack.rem_card (*hp) != *hp)
                return NULL;   // Card not in pack, must be dealt already
        process ();
    }
    return s;
}

void
aHand::process ()
{
    assert (points == 0);
    cards = NCARDS_IN_HAND;
	int* cardEnd = h + NCARDS_IN_HAND;
	int* card;
	int suit = 0;
	int nextSuitStart = NCARDS_IN_SUIT;

	for (card = h; card < cardEnd; card++)  {
		while (*card >= nextSuitStart) {
            nextSuitStart += NCARDS_IN_SUIT;
            suit++;
            assert (suit < NSUITS);
		}
		pat[suit]++;
		int cardVal = *card % NCARDS_IN_SUIT;
		int ptval = 4 - min (4, cardVal);
		if (ptval > 0)  {
            points += ptval;
            suitPts[suit] += ptval;
		}
		if (cardVal < 2)
            controls += (2 - cardVal);   // Ace=2, King=1
	}
	memcpy (shape, pat, sizeof (pat));
	shell (shape, NSUITS);
}

// See if the given card is present in this hand
bool
aHand::haveCard (int card)
{
	int* hEnd = h + NCARDS_IN_HAND;
	int* cardp;
	for (cardp = h; cardp < hEnd; cardp++)
		if (*cardp >= card)
			return (*cardp == card);
	return false;
}

int
aHand::getKeyCards (int suit)
{
    int* hEnd = h + NCARDS_IN_HAND;
    int* cardp;
    int kc = 0;
    int trumpKing = KING + suit * NCARDS_IN_SUIT;
    for (cardp = h; cardp < hEnd; cardp++)
        if (((*cardp % NCARDS_IN_SUIT) == ACE) || (*cardp == trumpKing))
            kc++;
    return kc;
}

/* Check if this hand has a particular shape */
bool
handBase::checkShape (int sh)
{
	// Extract shape from coded input.  The shape info is packed
	// into a 32 bit  word, each byte storing one suit length
	int wantl[NSUITS];
	wantl[0] = (sh & 0xff000000) >> 24;
	wantl[1] = (sh & 0x00ff0000) >> 16;
	wantl[2] = (sh & 0x0000ff00) >> 8;
	wantl[3] = sh & 0x000000ff;

	/* Sort the two shapes, since we do not need exact distribution */
	shell (wantl, NSUITS);
	for (int i = 0; i < NSUITS; i++)
		if (wantl[i] != shape[i])		/* Wrong shape */
			return false;
	return true;
}

// Check if this hand has a particular exact distribution
bool
handBase::checkPattern (int sh)
{
	// Extract shape from coded input.  The shape info is packed
	// into a 32 bit  word, each byte storing one suit length
	int wantl[NSUITS];
	wantl[0] = (sh & 0xff000000) >> 24;
	wantl[1] = (sh & 0x00ff0000) >> 16;
	wantl[2] = (sh & 0x0000ff00) >> 8;
	wantl[3] = sh & 0x000000ff;
	for (int i = 0; i < NSUITS; i++)
		if (wantl[i] != suitLen (i))		/* Wrong distribution */
			return false;
	return true;
}

/* Count total points in this hand */
static int
get_tpts (handBase* ah, int )
{
	return ah->getPoints();
}

static int
have_spot (handBase* ah, int card)
{
    return ah->haveCard (card);    // virtual: dispatches to aHand or returns false for partnerHand
}

static int
suit_len (handBase* ah, int suit)
{
    return ah->suitLen (suit);
}

#define NSPOTS      13
#define CURSUIT     4

static int spade_len (handBase* ah, int )     { return suit_len (ah, SPADES); }
static int heart_len (handBase* ah, int )     { return suit_len (ah, HEARTS); }
static int diamond_len (handBase* ah, int )   { return suit_len (ah, DIAMONDS); }
static int club_len (handBase* ah, int )      { return suit_len (ah, CLUBS); }
static int get_controls (handBase* ah, int )  { return ah->get_controls(); }
static int key_cards (handBase* ah, int suit) { return ah->getKeyCards (suit); }

static int
suit_pts (handBase* ah, int suit)
{
    return ah->suitPoints (suit);
}

static int
check_shape (handBase* ah, int sh)
{
    return ah->checkShape (sh);
}

static int
check_dist (handBase* ah, int sh)
{
    return ah->checkPattern (sh);
}

typedef int (*checker_fn)(handBase*, int);
struct funcDesc {
    const char* name;
    checker_fn f;
};

/* Non suit specific keywords */
static const funcDesc kword_fn_list[] = {
	{"TPTS",      get_tpts},
	{"POINTS",    get_tpts},
	{"SPADES",    spade_len},
	{"HEARTS",    heart_len},
	{"DIAMONDS",  diamond_len},
	{"CLUBS",     club_len},
	{"CONTROLS",  get_controls},
	{0, 0}
};

// suit specific keywords - prefixed by s,h,d,c, or x (for cursuit)
static const funcDesc suffix_fn_list[] = {
	{"A",        have_spot},
	{"K",        have_spot},
	{"Q",        have_spot},
	{"J",        have_spot},
	{"T",        have_spot},
	{"9",        have_spot},
	{"8",        have_spot},
	{"7",        have_spot},
	{"6",        have_spot},
	{"5",        have_spot},
	{"4",        have_spot},
	{"3",        have_spot},
	{"2",        have_spot},
	{"PTS",      suit_pts},
	{"L",        suit_len},
	{"LEN",      suit_len},
	{"KCS",      key_cards},
	{"KEYCARDS", key_cards},
	{0, 0}
};

/* Check if key matches a keyword */
/* return pointer to a key containing */
/* information about the keyword */
TPTR
match_string (char* s)
{
	const struct funcDesc* fdp;
	TPTR node;

	logDebug ("Calling match_string (%s)\n", s);
    for (fdp = kword_fn_list; fdp->name != 0; fdp++)
        if (strcmp (s, fdp->name) == 0) {   /* found match */
            node = make_leaf (TKWORD, fdp - kword_fn_list);
            strcpy (node->t_desc, s);
            return node;
        }
    /* No direct match, check if suit function */
	int j;
	switch (*s)	{
	case 's':
	case 'S':
		j = SPADES;
		break;
	case 'h':
	case 'H':
		j = HEARTS;
		break;
	case 'd':
	case 'D':
		j = DIAMONDS;
		break;
	case 'c':
	case 'C':
		j = CLUBS;
		break;
	case 'x':			/* refers to cursuit */
	case 'X':
		j = CURSUIT;
		break;
	default:
		return NULL;
		break;
	};
	// Possibly suit related.  Now match suffix
	for (fdp = suffix_fn_list; fdp->name != 0; fdp++)
		if (strcmp (s+1, fdp->name) == 0)	{		/* found match of suffix */
			int i = fdp - suffix_fn_list;			/* index in suffix array */
			if (i < NSPOTS) // specific card
                node = make_leaf (TSPOT, i + j * NCARDS_IN_SUIT);
			else            // suit function
                node = make_leaf (TSUITFUNC, i * SUIT_SELECTOR + j);
			strcpy (node->t_desc, s);
            return node;
		}
	return NULL;
}

#define LRES	t_left->t_result
#define RRES	t_right->t_result

static void
eval_node (TPTR node, int , int , void* hand)
{
	switch (node->t_type)	{
		case TINT:			/* integer - just use its value */
			node->t_result = node->t_val;
			break;
		case TASSIGN:		/* assign value to variable */
			/* store value from right leaf into address in left leaf */
			*(long long *)(node->LRES) = node->RRES;
			node->t_result = 1;		/* Return success */
			break;
		case TKWORD:		/* keyword - call appropriate function */
			node->t_result = (kword_fn_list[node->t_val].f)((handBase*)hand, 0);
			break;
		case TSUITFUNC:		/* keyword tied to specific suit */
			node->t_result = (suffix_fn_list[node->t_val / SUIT_SELECTOR].f)((handBase*)hand, node->t_val % SUIT_SELECTOR);
			break;
		case TSPOT:		/* specific card of specific suit */
			node->t_result = have_spot ((handBase*)hand, node->t_val);
			break;
		case TSHAPE:		/* match with hand shape */
			node->t_result = check_shape ((handBase*)hand, node->t_val);
			break;
		case TPATTERN:		/* match exact distribution */
			node->t_result = check_dist ((handBase*)hand, node->t_val);
			break;
		case TNEG:			/* arithmetic operation */
			node->t_result = -(node->RRES);
			break;
		case TPLUS:			/* arithmetic operation */
			node->t_result = ((node->LRES) + (node->RRES));
			break;
		case TMINUS:			/* arithmetic operation */
			node->t_result = ((node->LRES) - (node->RRES));
			break;
		case TMULT:			/* arithmetic operation */
			node->t_result = ((node->LRES) * (node->RRES));
			break;
		case TDIV:			/* arithmetic operation */
			node->t_result = ((node->LRES) / (node->RRES));
			break;
		case TMOD:			/* arithmetic operation */
			node->t_result = ((node->LRES) % (node->RRES));
			break;
		case TBITAND:		/* bitwise logical operation */
			node->t_result = ((node->LRES) & (node->RRES));
			break;
		case TBITOR:		/* bitwise logical operation */
			node->t_result = ((node->LRES) | (node->RRES));
			break;
		case TEQU:			/* comparison operation */
			node->t_result = ((node->LRES) == (node->RRES));
			break;
		case TNEQ:			/* comparison operation */
			node->t_result = ((node->LRES) != (node->RRES));
			break;
		case TGT:			/* comparison operation */
			node->t_result = ((node->LRES) > (node->RRES));
			break;
		case TGEQ:			/* comparison operation */
			node->t_result = ((node->LRES) >= (node->RRES));
			break;
		case TLT:			/* comparison operation */
			node->t_result = ((node->LRES) < (node->RRES));
			break;
		case TLEQ:			/* comparison operation */
			node->t_result = ((node->LRES) <= (node->RRES));
			break;
		case TNOT:			/* comparison operation */
			node->t_result = !(node->RRES);
			break;
		case TAND:			/* boolean operation */
			node->t_result = ((node->LRES) && (node->RRES));
			break;
		case TOR:			/* boolean operation */
			node->t_result = ((node->LRES) || (node->RRES));
			break;
		case TXOR:			/* boolean operation */
			node->t_result = ((node->LRES) ^ (node->RRES));
			break;
        case TDEFINE:
        case TDEFNAME:
            logError ("Unexpected value of TDEFINE or TDEFNAME\n");
            break;
	}
/* 	printf ("got result %d\n", node->t_result); */
}

void
write_leaf (TPTR node)
{
	const char suitNames[] = "SHDC";
	switch (node->t_type)	{
		case TINT:			/* integer - just use its value */
			sprintf (node->t_desc, "%lld", node->t_val);
			break;
		case TSHAPE:		/* match with hand shape */
			sprintf (node->t_desc, "Shape %llx", node->t_val);
			break;
		case TPATTERN:		/* match exact distribution */
			sprintf (node->t_desc, "Pattern %llx", node->t_val);
			break;
		case TSPOT:		/* specific card of specific suit */
		    sprintf (node->t_desc, "%c%s", suitNames[node->t_val / NCARDS_IN_SUIT],
                     suffix_fn_list[node->t_val % NCARDS_IN_SUIT].name);
		    break;
		case TASSIGN:		/* assign value to variable */
		case TKWORD:		/* keyword - call appropriate function */
		case TSUITFUNC:		/* keyword tied to specific suit */
		case TNEG:			/* arithmetic operation */
		case TPLUS:			/* arithmetic operation */
		case TMINUS:		/* arithmetic operation */
		case TMULT:			/* arithmetic operation */
		case TDIV:			/* arithmetic operation */
		case TMOD:			/* arithmetic operation */
		case TBITAND:		/* bitwise logical operation */
		case TBITOR:		/* bitwise logical operation */
		case TEQU:			/* comparison operation */
		case TNEQ:			/* comparison operation */
		case TGT:			/* comparison operation */
		case TGEQ:			/* comparison operation */
		case TLT:			/* comparison operation */
		case TLEQ:			/* comparison operation */
		case TNOT:			/* comparison operation */
		case TAND:			/* boolean operation */
		case TOR:			/* boolean operation */
		case TXOR:			/* boolean operation */
        case TDEFINE:
        case TDEFNAME:
        default:
            node->t_desc[0] = 0;
            break;
	}
/* 	printf ("got result %d\n", node->t_result); */
}

void
write_node (TPTR node)
{
	switch (node->t_type)	{
		case TASSIGN:		/* assign value to variable */
		case TSUITFUNC:		/* keyword tied to specific suit */
			break;
		case TNEG:			/* arithmetic operation */
			sprintf (node->t_desc, "(-%.500s)", node->t_right->t_desc);
			break;
		case TPLUS:			/* arithmetic operation */
			sprintf (node->t_desc, "(%.500s + %.500s)", node->t_left->t_desc, node->t_right->t_desc);
			break;
		case TMINUS:		/* arithmetic operation */
			sprintf (node->t_desc, "(%.500s - %.500s)", node->t_left->t_desc, node->t_right->t_desc);
			break;
		case TMULT:			/* arithmetic operation */
			sprintf (node->t_desc, "(%.500s * %.500s)", node->t_left->t_desc, node->t_right->t_desc);
			break;
		case TDIV:			/* arithmetic operation */
			sprintf (node->t_desc, "(%.500s / %.500s)", node->t_left->t_desc, node->t_right->t_desc);
			break;
		case TMOD:			/* arithmetic operation */
			sprintf (node->t_desc, "(%.500s %% %.500s)", node->t_left->t_desc, node->t_right->t_desc);
			break;
		case TBITAND:		/* bitwise logical operation */
			sprintf (node->t_desc, "(%.500s & %.500s)", node->t_left->t_desc, node->t_right->t_desc);
			break;
		case TBITOR:		/* bitwise logical operation */
			sprintf (node->t_desc, "(%.500s | %.500s)", node->t_left->t_desc, node->t_right->t_desc);
			break;
		case TEQU:			/* comparison operation */
			sprintf (node->t_desc, "(%.500s ?= %.500s)", node->t_left->t_desc, node->t_right->t_desc);
			break;
		case TNEQ:			/* comparison operation */
			sprintf (node->t_desc, "(%.500s != %.500s)", node->t_left->t_desc, node->t_right->t_desc);
			break;
		case TGT:			/* comparison operation */
			sprintf (node->t_desc, "(%.500s > %.500s)", node->t_left->t_desc, node->t_right->t_desc);
			break;
		case TGEQ:			/* comparison operation */
			sprintf (node->t_desc, "(%.500s >= %.500s)", node->t_left->t_desc, node->t_right->t_desc);
			break;
		case TLT:			/* comparison operation */
			sprintf (node->t_desc, "(%.500s < %.500s)", node->t_left->t_desc, node->t_right->t_desc);
			break;
		case TLEQ:			/* comparison operation */
			sprintf (node->t_desc, "(%.500s <= %.500s)", node->t_left->t_desc, node->t_right->t_desc);
			break;
		case TNOT:			/* comparison operation */
			sprintf (node->t_desc, "(!%.500s)", node->t_right->t_desc);
			break;
		case TAND:			/* boolean operation */
			sprintf (node->t_desc, "(%.500s && %.500s)", node->t_left->t_desc, node->t_right->t_desc);
			break;
		case TOR:			/* boolean operation */
			sprintf (node->t_desc, "(%.500s || %.500s)", node->t_left->t_desc, node->t_right->t_desc);
			break;
		case TXOR:			/* boolean operation */
			sprintf (node->t_desc, "(%.500s ^ %.500s)", node->t_left->t_desc, node->t_right->t_desc);
			break;
        case TDEFINE:
        case TDEFNAME:
            break;
		case TINT:			/* integer - just use its value */
		case TSHAPE:		/* match with hand shape */
		case TPATTERN:		/* match exact distribution */
		case TKWORD:		/* keyword - call appropriate function */
		case TSPOT:		/* specific card of specific suit */
        default:
            logError ("Unexpected type %d in write_node\n", node->t_type);
	}
/* 	printf ("got result %d\n", node->t_result); */
}

static int
test_and_or (TPTR rootp, int result)
{
	if (rootp->t_type == TAND)	/* do rhs only if lhs is true */
		return result;
	if (rootp->t_type == TOR)	/* do rhs only if lhs is false */
		return (result == 0);
	return true;
}

bool
handBase::checkHand (void* cur_root)
{
	if (cur_root == NULL) return true;   // NULL rule means no constraint (matches any hand)
	return traverse_lrt ((TPTR)cur_root, eval_node, 0, TOPNODE, test_and_or, this);
}

partnerHand::partnerHand (aHand& n, aHand& s)
{
    points   = n.getPoints ()    + s.getPoints ();
    controls = n.get_controls () + s.get_controls ();
    for (int i = 0; i < NSUITS; i++)    {
        pat[i]     = n.suitLen (i)    + s.suitLen (i);
        suitPts[i] = n.suitPoints (i) + s.suitPoints (i);
    }
    memcpy (shape, pat, sizeof pat);
    shell (shape, NSUITS);
}
