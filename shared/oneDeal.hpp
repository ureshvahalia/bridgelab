#ifndef _ONEDEAL_HPP_
#define _ONEDEAL_HPP_

#include <stdio.h>      // FILE* (required by handInfo.hpp on both sides)
#include "pack.hpp"     // pack, thePack, randCalls; also pulls in consts.h and time.h
#include "handInfo.hpp" // aHand, partnerHand

class oneDeal {
  protected:
    aHand       hands[NHANDS];
    void*       rules[NHANDS];
    void*       partnerRule;
  public:
    char* makePBNrec (char* where);
    char* makeLINrec (char* where, int boardNum);
    bool  checkPartnerRule ();
};

#endif  // _ONEDEAL_HPP_
