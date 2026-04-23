#include "consts.h"
#include <stdio.h>
#include <string.h>

// Base class shared by aHand and partnerHand.
// aHand::deal() and dealFromPBN() must NOT memset the whole object,
// to preserve the vtable pointer set by the constructor.
class handBase {
  protected:
    int points;
    int controls;
    int pat[NSUITS];
    int suitPts[NSUITS];
    int shape[NSUITS];
  public:
    int  getPoints ()               { return points; }
    int  get_controls ()            { return controls; }
    int  suitLen (int suit)         { return pat[suit]; }
    int  suitPoints (int suit)      { return suitPts[suit]; }
    virtual bool haveCard (int)     { return false; }
    virtual int  getKeyCards (int)  { return 0; }
    virtual ~handBase () {}
    bool checkShape (int sh);
    bool checkPattern (int ptn);
    bool checkHand (void*);
};

class aHand : public handBase {
    oneHand h;
    int   cards;
    void  process ();
  public:
    aHand () : cards (0) { points = 0; controls = 0; memset (h, 0, sizeof h); memset (pat, 0, sizeof pat); memset (suitPts, 0, sizeof suitPts); memset (shape, 0, sizeof shape); }
    int cardsDealt ()   { return cards; }
    int* getHand ()     { return (cards ? h : NULL); }
    void deal ();
    char* dealFromPBN (const char* pbnStr);
    bool copyHand (aHand* from);    // Bidder only — remove cards from pack
    bool saveHand ();               // Dealer only — validate cards still available
    bool haveCard (int card) override;
    int  getKeyCards (int suit) override;
    static void writeSummaryHeader (FILE* fp, char direction);
    char* writeSummary (char* where);
    // Write point and suit len info in csv form
    // Returns pointer to location after last char written
};

// Represents the combined N+S holding for partnership rule evaluation.
// Keywords like Points, Spades etc. return the sum of both hands.
// haveCard always returns false (inherited from handBase).
struct partnerHand : public handBase {
    partnerHand (aHand& n, aHand& s);
};
