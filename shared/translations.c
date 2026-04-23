#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "consts.h"

static char SPOTS[] = "AKQJT98765432akqjt98765432,-Xx";

char*
PBN2oneHand (const char* str, oneHand hand)
{
    if (*str == '-')        // Hand not specified
        return (char*)(str + 1);    // Skip the '-'
    int suitBase = 0;
    int* hp = hand;
    int i;
    if (*str == '$')
        str++;                      // Skip leading '$'
    for (i = 0; i < 16; i++)    {   // Valid hand described by 16-char str
        if (hp - hand >= NCARDS_IN_HAND)
            return NULL;    // Too many cards
        char c = *str++;
        switch (c)  {
        case 'A':
        case 'a':
            *hp++ = suitBase;
            break;
        case 'K':
        case 'k':
            *hp++ = suitBase + 1;
            break;
        case 'Q':
        case 'q':
            *hp++ = suitBase + 2;
            break;
        case 'J':
        case 'j':
            *hp++ = suitBase + 3;
            break;
        case 'T':
        case 't':
            *hp++ = suitBase + 4;
            break;
        case '9':
        case '8':
        case '7':
        case '6':
        case '5':
        case '4':
        case '3':
        case '2':
            *hp++ = suitBase + 14 - (c - '0');
            break;
            break;
        case '.':
        case ',':
            suitBase += NCARDS_IN_SUIT;
            if (suitBase >= PACK_SIZE)
                return NULL;    // Too many '.'s
            break;
        default:
            return NULL;
            break;
        }
    }
    return (char*)(((hp - hand) == NCARDS_IN_HAND) ? str : NULL);
}

static char*
oneHand2PBN (char* where, oneHand hand)
{
	int* hEnd = hand + NCARDS_IN_HAND;
	int base = 0;
	char* cp = where;
	int* h;
	if (hand == NULL)
        *where++ = '-';
    else    {
        for (h = hand; h < hEnd; h++)	{
            int j = *h - base;
            while (j >= NCARDS_IN_SUIT)	{	// new suit
                base += NCARDS_IN_SUIT;
                *where++ = '.';             // suit change marker
                j -= NCARDS_IN_SUIT;
            }
            *where++ = SPOTS[j];
        }

        // Finished all cards, now check for voids in lowest suit(s)
        while (base < (3 * NCARDS_IN_SUIT))	{
            *where++ = '.';                 // suit change marker
            base += NCARDS_IN_SUIT;			// next suit
        }
        assert (where == (cp + NCARDS_IN_HAND + NSUITS - 1));
    }
    *where++ = ' ';
	*where = 0;
    return where;
}

char*
writePbnHand (char* where, int* nhand, int* ehand, int* shand, int* whand)
{
    where = oneHand2PBN (where, nhand);
    where = oneHand2PBN (where, ehand);
    where = oneHand2PBN (where, shand);
    where = oneHand2PBN (where, whand);
    *(--where) = 0;
    return where;
}

/* Write one hand in BBO LIN format: S<spades>H<hearts>D<diamonds>C<clubs>
   Void suits are omitted entirely. */
static char*
oneHand2LIN (char* where, int* hand)
{
    static const char suitLetters[] = "SHDC";
    int* hEnd = hand + NCARDS_IN_HAND;
    int base = 0;
    int suit = 0;
    int suitStarted = 0;
    int* h;
    for (h = hand; h < hEnd; h++) {
        int j = *h - base;
        while (j >= NCARDS_IN_SUIT) {
            base += NCARDS_IN_SUIT;
            suit++;
            j -= NCARDS_IN_SUIT;
            suitStarted = 0;
        }
        if (!suitStarted) {
            *where++ = suitLetters[suit];
            suitStarted = 1;
        }
        *where++ = SPOTS[j];
    }
    return where;
}

/* Write a complete BBO LIN board record.
   board_num is 1-based; dealer and vulnerability follow standard bridge rotation.
   Writes: pn|,,,|st||md|{dealer}{S},{W},{N},|sv|{vul}|rh||ah|Board N|pg||
   Returns pointer past the terminating newline. */
char*
writeLINboard (char* where, int* nhand, int* ehand, int* shand, int* whand, int board_num)
{
    /* Standard vulnerability cycle for boards 1-16 */
    static const char* const vulCodes[16] = {
        "o", "n", "e", "b",
        "n", "e", "b", "o",
        "e", "b", "o", "n",
        "b", "o", "n", "e"
    };
    /* Dealer codes: (board-1)%4 → 0=South(1), 1=West(2), 2=North(3), 3=East(4) */
    static const int dealerCodes[4] = { 1, 2, 3, 4 };
    int dealer = dealerCodes[(board_num - 1) % 4];
    const char* vul = vulCodes[(board_num - 1) % 16];

    where += sprintf (where, "qx|o%d|pn|,,,|st||md|%d", board_num, dealer);
    where = oneHand2LIN (where, shand);
    *where++ = ',';
    where = oneHand2LIN (where, whand);
    *where++ = ',';
    where = oneHand2LIN (where, nhand);
    *where++ = ',';
    where += sprintf (where, "|sv|%s|rh||ah|Board %d|pg||\n", vul, board_num);
    (void) ehand;   /* East is derived by BBO from the other three hands */
    return where;
}
