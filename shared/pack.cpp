#include <stdlib.h>
#include <stdio.h>
#include "pack.hpp"
#include <sys/time.h>
#include <gsl/gsl_rng.h>
#include "log.h"

#define MAX_RAND_CALLS  20000
int randCalls = 0;

static bool          rng_seed_set = false;
static unsigned long rng_seed_val = 0;

void
setRngSeed (unsigned long seed)
{
    rng_seed_set = true;
    rng_seed_val = seed;
}

static unsigned long
getRand ()
{
	randCalls++;
    static gsl_rng* rng = NULL;
    if (rng == NULL)    {
        rng = gsl_rng_alloc (gsl_rng_mt19937);
        if (rng_seed_set)   {
            gsl_rng_set (rng, rng_seed_val);
        } else {
            struct timeval tv;
            if (gettimeofday (&tv, NULL) != 0)
                logError ("gettimeofday failed\n");
            gsl_rng_set (rng, tv.tv_usec);
        }
    }
	return gsl_rng_get (rng);
}

void
shell (int* arr, int n)
// routine to sort an array in increasing order
{
	int gap, i, j, temp;

	for (gap = n/2; gap > 0; gap /= 2)
		for (i = gap; i < n; i++)
			for (j = i - gap; j >= 0 && arr[j] > arr[j + gap]; j -= gap)	{
				temp = arr [j];
				arr [j] = arr [j + gap];
				arr [j + gap] = temp;
			}
}

/* Pack Management:  The master card pack is represented by a 52 integer */
/* array called pack, and a global variable undealt, which tracks the */
/* number of cards left in the pack.  Upon reshuffle, the cards are kept */
/* in sorted order in the pack, and undealt is set to 52.  Each card is */
/* dealt by picking a random number (32 bit integer), dividing it by */
/* undealt, and using the remainder as an index into the pack.  The card */
/* so obtained is removed from the pack by putting the card in the  */
/* undealt'th slot into the one just vacated, and decrementing undealt. */
/* At any time, the slots beyond the undealt'th slot contain garbage. */

pack thePack;

/* put the cards back in the pack */
void
pack::reshuffle ()
{
	int j;
	undealt = PACK_SIZE;
	for (j = 0; j < PACK_SIZE; j++)
		cards[j] = j;
}

/* select a random card from those not dealt out yet */
int
pack::deal_card ()
{
	int ndx = getRand () % undealt;
	int card = cards[ndx];
	cards[ndx] = cards[--undealt];
	return (card);
}


/* remove a specific card from the pack */
int
pack::rem_card (int card)
{
	int i;

	/* first look in the most likely slot */
	if ((card < undealt) && (cards[card] == card))	{		/* found there */
		cards[card] = cards[--undealt];	/* remove it */
		return (card);
	}

	/* Not in default slot.  Look through all undealt cards */
	for (i = 0; i < undealt; i++)
		if (cards[i] == card)	{		/* found it */
			cards[i] = cards[--undealt];	/* remove it */
			return (card);
		}

	/* Not found.  Must have been dealt already */
	return (-1);
}


/* save current state of the pack in array p*/
int
pack::save_pack (int* p)
{
	int i;
	for (i = 0; i < PACK_SIZE; i++)
		p[i] = cards[i];
	return (undealt);
}


/* restore pack from copy saved in array p */
void
pack::restore_pack (int u, int* p)
{
	int i;
	for (i = 0; i < PACK_SIZE; i++)
		cards[i] = *p++;
	undealt = u;
}

/* deal a random hand */
void
pack::deal_hand (oneHand hand)
{
	int i;
	for (i = 0; i < NCARDS_IN_HAND; i++)
		hand[i] = deal_card ();
	shell (hand, NCARDS_IN_HAND);
}

int
lowpip (int suit)
/* deals the lowest remaining card of a suit — not yet implemented */
{
    (void)suit;
    logError ("lowpip: not yet implemented\n");
    return (-1);
}
