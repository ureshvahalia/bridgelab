#include "rawScore.h"

// Indexed by DDS strain ordering: S=0, H=1, D=2, C=3, NT=4
const int trickVal[NSTRAINS]  = { 30, 30, 20, 20, 30 };
const int baseScore[NSTRAINS] = {  0,  0,  0,  0, 10 };

int
rawScore (int bid, int denom, int premium, int tricksMade, bool amVul)
{
    if ((bid < 1) || (bid > 7))
        return 0;
    if ((denom < 0) || (denom >= NSTRAINS))
        return 0;
    if ((premium != NORMAL) && (premium != DBL) && (premium != RDBL))
        return 0;
    if ((tricksMade < 0) || (tricksMade > 13))
        return 0;

    enum {
        vulDown           = 100,
        nvDown            = 50,
        partScoreBonus    = 50,
        vulGameBonus      = 500,
        nvGameBonus       = 300,
        smallSlamLevel    = 6,
        grandSlamLevel    = 7,
        smallSlamVulBonus = 750,
        smallSlamNVBonus  = 500,
        grandSlamVulBonus = 1500,
        grandSlamNVBonus  = 1000,
        doubleBonus       = 50,
        redoubleBonus     = 100
    };

    int tricksContracted = bid + baseTricks;
    int ot = tricksMade - tricksContracted;
    if (ot < 0) {   // down
        if (premium == NORMAL)
            return ot * (amVul ? vulDown : nvDown);
        else {
            int dbldown = ot * 300 + 100;
            if (!amVul) {
                if (ot == -1)
                    dbldown = -100;
                else if (ot == -2)
                    dbldown = -300;
                else
                    dbldown += 300;
            }
            return (premium == DBL) ? dbldown : 2 * dbldown;
        }
    }
    // Contract made, perhaps with overtricks
    int trickScore = (bid * trickVal[denom] + baseScore[denom]) * premium;
    int bonus = (trickScore < 100) ? partScoreBonus : (amVul ? vulGameBonus : nvGameBonus);
    if (bid == smallSlamLevel)
        bonus += (amVul ? smallSlamVulBonus : smallSlamNVBonus);
    else if (bid == grandSlamLevel)
        bonus += (amVul ? grandSlamVulBonus : grandSlamNVBonus);
    if (premium == DBL)
        bonus += doubleBonus;
    else if (premium == RDBL)
        bonus += redoubleBonus;
    int otBonus = (premium == NORMAL) ? trickVal[denom] : (amVul ? 200 : 100);
    if (premium == RDBL)
        otBonus *= 2;
    return trickScore + bonus + ot * otBonus;
}
