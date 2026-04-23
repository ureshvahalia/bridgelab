#ifndef _DDSINFO_HPP_
#define _DDSINFO_HPP_

#include <stddef.h>

#define DDS_STRAINS 5
#define numLevels 7
#define PBN_STRLEN 80
#define TOT_BIDS (DDS_STRAINS * numLevels)

typedef char pbnString[PBN_STRLEN];
typedef int strainArray[DDS_STRAINS];
const char suitAbbrv[DDS_STRAINS + 1] = "SHDCN";

enum vulnerabilityCodes {
    vulNone = 1,
    vulNS = 2,
    vulEW = 3,
    vulBoth = 4
};

const vulnerabilityCodes dealVuls[] = {
  vulEW,  // Deal 0 is effectively Deal 16
  vulNone, vulNS, vulEW, vulBoth,
  vulNS, vulEW, vulBoth, vulNone,
  vulEW, vulBoth, vulNone, vulNS,
  vulBoth, vulNone, vulNS, vulEW
};

#define VUL_CYCLE 16
#define VUL(dealNo) dealVuls[dealNo%VUL_CYCLE]

class contract  {
    int bidLevel;
    int strain;
    int premium;
    int declarer;
    char bid[8];
  public:
    void initialize (char* s);
    void initialize2 (int l, int st, int d = 0, int p = 1);
    char* getBidp ()    { return bid; }
    int getBidLevel ()  { return bidLevel; }
    int getStrain ()    { return strain; }
    int getDeclarer ()  { return declarer; }
    int getPremium ()   { return premium; }
    bool isValid ()     { return (bidLevel >= 0);   }
};

/*
 * dealScore is used to accumulate the total raw scores across a set of generated deals for each level and strain
 */
class dealScore {
    int totScore[DDS_STRAINS][numLevels];
  public:
    dealScore ();
    void addRaw (int strain, int level, int val)    { totScore[strain][level] += val;   }
    bool compareScores (int i, int j);
    int setPar (contract* parp, bool vul, int nhands, int nBestToPrint);
    // Returns best value of totScore, and sets parp to highest optimum contract
    int getScore (int strain, int level)            { return totScore[strain][level];   }
};

class ddsInfo   {
    pbnString* pbnHands;
    strainArray* maxTricksList;
    // int vulScore[DDS_STRAINS][numLevels];
    // int nvScore[DDS_STRAINS][numLevels];
    int nhands;
    int curPbn;
    int curTrickList;
    contract par;
    float parAveScore;
    vulnerabilityCodes curVulCode;
    contract* getParPtr ()  {   return &par; }
    // contract vulPar;
    // contract nvPar;
    // int vulBest;
    // int nvBest;
    // void addVul (int strain, int level, int val)     { vulScore[strain][level] += val;   }
    // void addNV (int strain, int level, int val)      { nvScore[strain][level] += val;    }
    // void addRaw (int vulIndex, int handno);             // Add raw scores for hand i
  public:
    ddsInfo (int);
    ~ddsInfo ();
    void restartPbn()   { curPbn = 0;    }
    char* nextPbn() { return pbnHands[curPbn++];    }
    // Compute and save the par contract and score. Print the n best if asked
    void setPars (vulnerabilityCodes vulCode, int nBestToPrint = 0);
    void setMaxTricks (int hno, int strain, int val)    {   maxTricksList[hno][strain] = val;   }
    void summarize ();
    float impsVsPar (int bidLvl, int st);   // Average IMP score for lvl,st vs baseline
    float aveImps (contract* c1, contract* baseline);
    float aveTricks (int st);
    char* parBid ()    {   return par.getBidp (); }
    float parAve ()    {   return parAveScore; }
    vulnerabilityCodes vulCode()  { return curVulCode; }
    // char* vulParBid()   {   return vulPar.getBidp ();   }
    // float vulParAve()   {   return (float(vulBest))/nhands; }
    // char* nvParBid()   {   return nvPar.getBidp ();   }
    // float nvParAve()   {   return (float(nvBest))/nhands; }
};

// Call the double dummy analyzer.
// The routine can be called multiple times, but its output file is set only during the first invocation.
// Subsequent calls ignore the csvFile argument and write to the file specified in the first call.
void ddsMain (int handCount, ddsInfo* ddip, FILE* outFp, const char* filter, int declarer);

// Determines the best par contract(s) for a deal, given the maximum tricks makable in each strain.
// Assumes contract is not doubled or redoubled. Returns the par score and writes the par contracts at where
int NSparDD (int* maxTricks, char* where, bool v, bool imp);

#endif // _DDSINFO_HPP_