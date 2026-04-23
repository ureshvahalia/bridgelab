#include <stdio.h>
#include "oneDeal.hpp"
#include "translations.h"

char*
oneDeal::makePBNrec (char* where)
{
    return writePbnHand (where, hands[0].getHand(), hands[1].getHand(),
                         hands[2].getHand(), hands[3].getHand());
}

char*
oneDeal::makeLINrec (char* where, int boardNum)
{
    return writeLINboard (where, hands[0].getHand(), hands[1].getHand(),
                          hands[2].getHand(), hands[3].getHand(), boardNum);
}

bool
oneDeal::checkPartnerRule ()
{
    if (!partnerRule)
        return true;
    partnerHand ph (hands[0], hands[2]);
    return ph.checkHand (partnerRule);
}
