/* Dealer-specific hand summary functions */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pack.hpp"
#include "handInfo.hpp"

bool
aHand::saveHand ()
{
    int* hEnd = h + NCARDS_IN_HAND;
    for (int* fp = h; fp < hEnd; fp++)
        if (thePack.rem_card (*fp) != *fp)
            return false;   // Card not in pack, must be dealt already
    return true;
}

char*
aHand::writeSummary (char* cp)
{
    sprintf (cp, ",%d,%d,%d,%d,%d,%d-%d-%d-%d", points,
             pat[0], pat[1], pat[2], pat[3], shape[3], shape[2], shape[1], shape[0]);
    while (*cp)
        cp++;
    return cp;
}
