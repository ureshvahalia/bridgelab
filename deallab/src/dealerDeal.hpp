#ifndef _DEALERDEAL_HPP_
#define _DEALERDEAL_HPP_

#include <stdio.h>
#include "oneDeal.hpp"

class dealerDeal : public oneDeal {
    static int  handsDealt;
    int         nToDeal[NHANDS];
    pack        myPack;
  public:
    dealerDeal (const char* ruleNames[], void* pr = nullptr);
    bool dealAndCheck ();
    bool checkHand (int which, void* checkRule) { return hands[which].checkHand (checkRule); }
    void saveNS ();
    void writeSummaries (FILE* fp);
    static void printReport ();
};

#endif  // _DEALERDEAL_HPP_
