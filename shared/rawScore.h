#ifndef _RAWSCORE_H_
#define _RAWSCORE_H_

#include "consts.h"     // NSTRAINS

// Trick values and NT base score, indexed by DDS strain ordering: S=0, H=1, D=2, C=3, NT=4
extern const int trickVal[NSTRAINS];
extern const int baseScore[NSTRAINS];

static const int baseTricks = 6;   // tricks needed to make a 1-level contract

// Contract premium multipliers
enum { NORMAL = 1, DBL = 2, RDBL = 4 };

// Duplicate bridge score for the declarer side.
//   bid        = level (1-7)
//   denom      = strain in DDS ordering (S=0, H=1, D=2, C=3, NT=4)
//   premium    = NORMAL, DBL, or RDBL
//   tricksMade = total tricks made by declarer (0-13)
//   amVul      = true if declarer is vulnerable
// Returns a positive score for a made contract, negative for going down.
int rawScore (int bid, int denom, int premium, int tricksMade, bool amVul);

// Converts a duplicate score difference to IMPs, per the standard IMP table.
// Sign of the result matches the sign of scoreDiff.
int imps (int scoreDiff);

#endif // _RAWSCORE_H_
