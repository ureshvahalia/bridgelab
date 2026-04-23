#ifndef _PACK_HPP_
#define _PACK_HPP_

#include <time.h>
#include "consts.h"

extern int randCalls;
extern void shell (int*, int);
extern void setRngSeed (unsigned long seed);

class pack  {
  public:
    void  reshuffle ();
    void  deal_hand (oneHand);
    int   rem_card (int);
    int   save_pack (int*);
    void  restore_pack (int, int*);
    int   cardsLeft()   { return undealt; }
  private:
    int   deal_card ();
    int   cards[PACK_SIZE];
    int   undealt;
};
extern pack thePack;

#endif // _PACK_HPP_
