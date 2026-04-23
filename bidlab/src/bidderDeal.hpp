#ifndef _BIDDERDEAL_HPP_
#define _BIDDERDEAL_HPP_

#include "oneDeal.hpp"

class bidderDeal : public oneDeal {
  public:
    bidderDeal (void* rn, void* re, void* rs, void* rw, void* pr = nullptr);
    bool dealAndCheck (bool n, bool e, bool s, bool w);
    bool enterPbn (char* pbnStr);
    bool enterNorth (aHand* from)  { return hands[0].copyHand (from); }
    bool enterSouth (aHand* from)  { return hands[2].copyHand (from); }
    bool saveNorth  (aHand* to)    { return to->copyHand (&hands[0]); }
    bool saveEast   (aHand* to)    { return to->copyHand (&hands[1]); }
    bool saveSouth  (aHand* to)    { return to->copyHand (&hands[2]); }
    bool saveWest   (aHand* to)    { return to->copyHand (&hands[3]); }
};

#endif  // _BIDDERDEAL_HPP_
