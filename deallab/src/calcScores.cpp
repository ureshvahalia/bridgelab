#include <stdio.h>
#include <string.h>
#include <algorithm>
#include "consts.h"
#include "ddsinfo.hpp"
#include "rawScore.h"

class impTranslator {
    enum {
        IMP_RANGES = 24,
        MAX_IMP_SCORE = 4000,
        MAX_IMPS = 24
    };
    static int impTable[MAX_IMP_SCORE];
    static int impScale[IMP_RANGES];
    int junk;
  public:
    impTranslator ();
    static int rawToImp (int raw);
};

int impTranslator::impTable[MAX_IMP_SCORE];

impTranslator::impTranslator ()
{
    int* scalePtr = impScale;
    for (int i = 0; i < MAX_IMP_SCORE / 10; i++)    {
        if (*scalePtr <= (i * 10))
            scalePtr++;
        impTable[i] = scalePtr - impScale;
    }
}

int
impTranslator::rawToImp (int raw)
{
    if (raw >= 0)
        return ((raw > MAX_IMP_SCORE) ? MAX_IMPS : impTable[raw / 10]);
    else
        return (((-raw) > MAX_IMP_SCORE) ? -(MAX_IMPS) : -(impTable[(-raw) / 10]));
}

int impTranslator::impScale[] = { 20, 50, 90, 130, 170, 220, 270, 320, 370, 430, 500, 600, 750, 900, 1100, 1300, 1500, 1750, 2000, 2250, 2500, 3000, 3500, MAX_IMP_SCORE    };
static impTranslator dummy;    // So that the static impTable gets set up


void
contract::initialize (char* s)
{
    if (s)  {
        strcpy (bid, s);
        declarer = 0;
        premium = 1;
        if (*s == '(')  {   // Opponent's contract
            declarer = 1;   // Default to East
            s++;            // Skip the '('
        }
        if ((*s == 'P') || (*s == 'p'))  {
            bidLevel = 0;
            strain = -1;
        } else if ((*s < '1') || (*s > '7')) {
            bidLevel = strain = -1;
        } else  {
            bidLevel = *s++ - '0';
            switch (*s++)   {
                case 'S':
                case 's':
                    strain = 0;
                    break;
                case 'H':
                case 'h':
                    strain = 1;
                    break;
                case 'D':
                case 'd':
                    strain = 2;
                    break;
                case 'C':
                case 'c':
                    strain = 3;
                    break;
                case 'N':
                case 'n':
                    strain = 4;
                    break;
                default:
                    bidLevel = strain = -1;
                    break;
            }
        }
        if ((*s == 'X') || (*s == 'x'))  {
            s++;
            premium = 2;
            if ((*s == 'X') || (*s == 'x')) {
                premium = 4;
            }
        }
        if (*s == '-')  {       // Process Declarer
            if (declarer == 0)  {
                switch (*++s)  {
                case 'N':
                    declarer = 0;
                    break;
                case 'E':
                    declarer = 1;
                    break;
                case 'S':
                    declarer = 2;
                    break;
                case 'W':
                    declarer = 3;
                    break;
                default:
                    bidLevel = strain = -1; // Illegal format
                    break;
                }
            } else
                bidLevel = strain = -1;     // Illegal format
        }
    } else  {   // Empty string
        bidLevel = strain = -1;
        bid[0] = 0;
    }
}

void
contract::initialize2 (int lvl, int st, int decl, int prem)
{
    bidLevel = lvl;
    strain = st;
    premium = prem;
    declarer = decl;
    if (lvl == 0)  {
        strcpy (bid, "P");
        strain = -1;
    } else if ((lvl < 0) || (lvl > 7) || (st < 0) || (st >= DDS_STRAINS) ||
               ((prem != 1) && (prem != 2) && (prem != 4)) || (decl < 0) || (decl >= NHANDS))   {
        // Illegal bid
        bidLevel = -1;
        strain = -1;
        strcpy (bid, "NA");
    } else
        sprintf (bid, "%1d%c%s", lvl, suitAbbrv[st], (prem == 4) ? "XX" : ((prem == 2) ? "X" : ""));
}

// Computes the raw score for a contract given a contract pointer and result

inline int
rawScore (contract* cp, int tricksMade, int vulCode)
{
    // tricksMade is how many tricks our side made
    if ((cp->getDeclarer() % 2) == 0)    // Our contract
        return rawScore (cp->getBidLevel(), cp->getStrain(), cp->getPremium(), tricksMade, ((vulCode % 2) == 0));
    else    // Opponents' contract
        return (-1) * rawScore (cp->getBidLevel(), cp->getStrain(), cp->getPremium(), MAX_TRICKS - tricksMade, (vulCode > 2));
}

dealScore::dealScore ()
{
    memset (totScore, 0, sizeof (totScore));
}

static dealScore* curDS;

inline bool
dealScore::compareScores (int i, int j)
{
    return (totScore[i%DDS_STRAINS][i/DDS_STRAINS] > totScore[j%DDS_STRAINS][j/DDS_STRAINS]);
}

static bool
cmpScores (int i, int j)
{
    return curDS->compareScores (i, j);
}

int
dealScore::setPar (contract* parp, bool /*vul*/, int nhands, int nBestToPrint)
{
    int bestTotScore = 0;
    int strain, lvl;
    int sBest = -1;
    int lBest = -1;
    for (strain = 0; strain < DDS_STRAINS; strain++)
        for (lvl = 0; lvl < numLevels; lvl++)
            if (totScore[strain][lvl] >= bestTotScore) {
                bestTotScore = totScore[strain][lvl];
                sBest = strain;
                lBest = lvl;
            }
    parp->initialize2 (lBest + 1, sBest);
    curDS = this;
    int ranks[TOT_BIDS];
    // printf ("\n%s Par for NS: %s (ave. %7.2f)\n", vulStr(vul), parp->getBidp(), float (bestTotScore)/nhands);
    for (int* rp = ranks, j = 0; rp < ranks + TOT_BIDS; rp++, j++)
        *rp = j;
    std::sort (ranks, ranks + TOT_BIDS, cmpScores);
    for (int i = 0; i < nBestToPrint; i++)  {
        strain = ranks[i] % DDS_STRAINS;
        lvl = ranks[i] / DDS_STRAINS;
        printf ("%d%c: %7.2f%s", lvl + 1, suitAbbrv[strain], ((float)(totScore[strain][lvl]))/nhands, (i == (nBestToPrint - 1)) ?  "\n" : ",\t");
    }
    // printf ("\n");
    return bestTotScore;
}

ddsInfo::ddsInfo (int n)
    :   nhands (n), curPbn (0), curTrickList (0), curVulCode(vulNone)
{
    pbnHands = new pbnString[n];
    maxTricksList = new strainArray[n];
}

ddsInfo::~ddsInfo ()
{
    delete [] pbnHands;
    delete [] maxTricksList;
}

float
ddsInfo::aveTricks (int st)
{
    int totTricks = 0;
    for (int hno = 0; hno < nhands; hno++)
        totTricks += maxTricksList[hno][st];
    return (float(totTricks)/nhands);
}

void
ddsInfo::setPars (vulnerabilityCodes vulCode, int nBestToPrint)
{
    dealScore ds;
    bool vul = ((vulCode == vulBoth) || (vulCode == vulNS));
    for (int i = 0; i < nhands; i++)    {
        int* tricklist = maxTricksList[i];
        for (int strain = 0; strain < DDS_STRAINS; strain++)
            for (int lvl = 0; lvl < numLevels; lvl++)
                ds.addRaw (strain, lvl, rawScore (lvl + 1, strain, NORMAL, tricklist[strain], vul));
    }
    parAveScore = float(ds.setPar (&par, vul, nhands, nBestToPrint))/nhands;
    float bestImps = impsVsPar (0, 0); // IMPs scored by Pass
    int bestStrain = -1;
    int bestLevel = 0;
    for (int s = 0; s < DDS_STRAINS; s++)
        for (int bidLvl = 1; bidLvl <= numLevels; bidLvl++) {
            float a = impsVsPar (bidLvl, s);
            if (a >= bestImps)   {
                bestStrain = s;
                bestLevel = bidLvl;
                bestImps = a;
                if (a > 0)
                    printf ("IMP score for %d%c better than raw par %s\n", bidLvl, suitAbbrv[s], par.getBidp());
            }
        }
    par.initialize2 (bestLevel, bestStrain);
    parAveScore = float (bestLevel > 0 ? ds.getScore (bestStrain, bestLevel - 1) : 0) / nhands;
}

float
ddsInfo::impsVsPar (int bidLvl, int st)
{
    int totImps = 0;
    int parStrain = par.getStrain();
    for (int i = 0; i < nhands; i++)    {
        int baseRaw = rawScore (&par, maxTricksList[i][parStrain], curVulCode);
        int myRaw = (bidLvl == 0) ? 0 : rawScore (bidLvl, st, NORMAL, maxTricksList[i][st], ((curVulCode % 2) == 0));
        totImps += impTranslator::rawToImp (myRaw - baseRaw);
    }
    return float(totImps)/nhands;

}

float
ddsInfo::aveImps (contract* cp, contract* baseline)
{
    int totImps = 0;
    if (baseline == NULL)
        baseline = &par;
    int baseStrain = baseline->getStrain();
    for (int i = 0; i < nhands; i++)    {
        int baseRaw = rawScore (baseline, maxTricksList[i][baseStrain], curVulCode);
        int myRaw = rawScore (cp, maxTricksList[i][cp->getStrain()], curVulCode);
        totImps += impTranslator::rawToImp (myRaw - baseRaw);
    }
    return float(totImps)/nhands;
}

void
ddsInfo::summarize ()
{
    setPars (vulNone, 5);

    printf ("\n%s Average IMPs:\n\t", "NV");
    printf ("Pass: %7.2f\n\t", impsVsPar (0, -1));

    for (int bidLvl = 1; bidLvl <= numLevels; bidLvl++)   {
        for (int strain = 0; strain < DDS_STRAINS; strain++)
            printf ("%d%c: %7.2f,\t", bidLvl, suitAbbrv[strain], impsVsPar (bidLvl, strain));
        printf ("\n\t");
    }
    printf ("\n");

    setPars (vulBoth, 5);

    printf ("\n%s Average IMPs:\n\t", "Vul");
    printf ("Pass: %7.2f\n\t", impsVsPar (0, -1));

    for (int bidLvl = 1; bidLvl <= numLevels; bidLvl++)   {
        for (int strain = 0; strain < DDS_STRAINS; strain++)
            printf ("%d%c: %7.2f,\t", bidLvl, suitAbbrv[strain], impsVsPar (bidLvl, strain));
        printf ("\n\t");
    }
    printf ("\n");
}

int
NSparDD (int* maxTricks, char* where, bool v, bool imp)
{
    // in:  maxTricks is an array of DDS_STRAINS integers, each between 0 and 13
    // in:  where is a location where the par contract string will be written.
    //      It should have space for at least 15 chars
    // in:  v is a bool set to true if vulnerable
    // in:  imp is a bool set to true for imp scoring, false for matchpoints
    //      for imp scoring, a contract within 10 points of par is also par
    // out: returns the optimal score
    // out: the set of optimal contracts is written at where,
    //      separated by semi-colon and null-terminated
    int scores[DDS_STRAINS];
    int i;

    int bestScore = 0;
    for (i = 0; i < DDS_STRAINS; i++)   {
        int s = scores[i] = rawScore (maxTricks[i] - baseTricks, i, NORMAL, maxTricks[i], v);
        if (s > bestScore)
            bestScore = s;
    }
    if (bestScore == 0) {
        strcpy (where, "Pass");
        return bestScore;
    }
    if (imp)
        bestScore -= 10;    // Equivalent for imps
    for (i = 0; i < DDS_STRAINS; i++)   {
        if (scores[i] >= bestScore) {   // one of the par contracts
            sprintf (where, "%d%c;", maxTricks[i] - baseTricks, suitAbbrv[i]);
            where += 3;
        }
    }
    --where;
    *where = 0;
    return (bestScore + (imp ? 10 : 0));
}
