#include <assert.h>
#include "bidderDeal.hpp"

bidderDeal::bidderDeal (void* rn, void* re, void* rs, void* rw, void* pr)
{
    rules[0] = rn;
    rules[1] = re;
    rules[2] = rs;
    rules[3] = rw;
    partnerRule = pr;
}

static inline int
deal_and_check (aHand* h, void* rule)
{
    assert (rule != NULL);
    h->deal ();
    return h->checkHand (rule);
}

bool
bidderDeal::dealAndCheck (bool n, bool e, bool s, bool w)
{
    if (n && (!deal_and_check (&hands[0], rules[0])))
            return false;
    if (e && (!deal_and_check (&hands[1], rules[1])))
            return false;
    if (s && (!deal_and_check (&hands[2], rules[2])))
            return false;
    if (w && (!deal_and_check (&hands[3], rules[3])))
            return false;
    return true;
}

bool
bidderDeal::enterPbn (char* str)
{
    // Assumes str points into a sequence of space-separated PBN hand strings
    int i;
    for (i = 0; i < NHANDS - 1; i++)    {
        str = hands[i].dealFromPBN (str);
        if ((str == NULL) || (*str++ != ' '))
            return false;
    }
    // Last hand needs special casing: not followed by a space
    str = hands[i].dealFromPBN (str);
    return (str != NULL);
}
