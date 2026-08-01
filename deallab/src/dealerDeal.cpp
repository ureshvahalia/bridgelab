#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "dealerDeal.hpp"
#include "tnode.h"
#include "log.h"

int dealerDeal::handsDealt;

static inline int
deal_and_check (aHand* h, void* rule)
{
    assert (rule != NULL);
    h->deal ();
    return h->checkHand (rule);
}

dealerDeal::dealerDeal (const char* rulenames[], void* pr)
{
    memset (rules, 0, sizeof rules);
    // Do NOT memset hands[] — that would wipe the vtable pointers set by aHand constructors.
    // aHand's own deal()/dealFromPBN() reset their fields before use.
    partnerRule = pr;
    thePack.reshuffle ();
    for (int i = 0; i < NHANDS; i++)    {
        if (rulenames[i])  {
            if ((rules[i] = find_rule (defroot, rulenames[i])) == NULL) {
                if (hands[i].dealFromPBN (rulenames[i]) == NULL)    {
                    logError ("Could not process rule %s\n", rulenames[i]);
                    exit (1);
                }
                nToDeal[i] = 0;
            } else
                nToDeal[i] = 13;
        } else  {
            if ((rules[i] = find_rule (defroot, "$ANY")) == NULL)   {
                logError ("Could not process rule $ANY\n");
                exit (1);
            }
            nToDeal[i] = 13;
        }
    }
    if ((nToDeal[0] + nToDeal[1] + nToDeal[2] + nToDeal[3]) != thePack.cardsLeft())   {
        logError ("Logic error in dealerDeal constructor: cardsLeft %d\n", thePack.cardsLeft());
        exit (12);
    }
    myPack = thePack;
    // Post: For each hand, either there is a valid rule or the hand is dealt
}

void
dealerDeal::writeSummaries (FILE* fp)
{
    char line[80];
    char* cp = line;
    cp = hands[0].writeSummary (cp);        // N hand summary
    (void) hands[2].writeSummary (cp);      // S hand summary
    fprintf (fp, "%s", line);
}

void
dealerDeal::printReport ()
{
    logInfo ("Total attempts = %d, randCalls = %d\n", handsDealt, randCalls);
}

void
dealerDeal::saveNS ()
{
    thePack.reshuffle ();
    hands[0].saveHand ();
    hands[2].saveHand ();
    nToDeal[0] = nToDeal[2] = 0;
    nToDeal[1] = nToDeal[3] = 13;
    myPack = thePack;
}

bool
dealerDeal::dealAndCheck ()
{
    int hno;
    while (handsDealt < MAXTRIES)    {
        thePack = myPack;   // Restore pack to saved state
        for (hno = 0; hno < NHANDS; hno++)  {
            if (rules[hno] && (nToDeal[hno] > 0))    {          // Hand to be dealt
                if (!deal_and_check (&hands[hno], rules[hno]))  // Retry
                    break;                                        // Go to outer while loop
            }
        }
        handsDealt++;
        if (hno == NHANDS)  {   // All individual rules passed
            if (!checkPartnerRule ())
                continue;   // Partnership rule failed, retry
            return true;
        }
    }
    logWarning ("Exceeded MAXTRIES\n");
    return false;
}
