/* Bidder-specific hand summary functions */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pack.hpp"
#include "handInfo.hpp"

bool
aHand::copyHand (aHand* from)
{
    // Explicit field copy — memcpy on a polymorphic type would overwrite the vtable.
    points   = from->points;
    controls = from->controls;
    memcpy (pat,     from->pat,     sizeof pat);
    memcpy (suitPts, from->suitPts, sizeof suitPts);
    memcpy (shape,   from->shape,   sizeof shape);
    memcpy (h, from->h, sizeof h);
    cards = from->cards;
    int* hEnd = h + NCARDS_IN_HAND;
    for (int* fp = h; fp < hEnd; fp++)
        if (thePack.rem_card (*fp) != *fp)
            return false;   // Card not in pack, must be dealt already
    return true;
}

void
aHand::writeSummaryHeader (FILE* fp, char direction)
{
    fprintf (fp, "%c pts,%c S,%c H,%c D,%c C,%c Pattern,%c Shape,",
             direction, direction, direction, direction, direction, direction, direction);
}

char*
aHand::writeSummary (char* cp)
{
    sprintf (cp, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d-%d-%d-%d,%d.%d.%d.%d,",
             points, controls,
             getKeyCards(SPADES), getKeyCards(HEARTS), getKeyCards(DIAMONDS), getKeyCards(CLUBS),
             pat[0], pat[1], pat[2], pat[3],
             pat[0], pat[1], pat[2], pat[3],
             shape[3], shape[2], shape[1], shape[0]);
    while (*cp)
        cp++;
    return cp;
}
