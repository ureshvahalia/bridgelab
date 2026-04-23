#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <limits.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <assert.h>
#include "tnode.h"
#include "consts.h"
#include "pack.hpp"
#include "translations.h"
#include "parse_rules.h"
extern "C" void print_time_estimate (time_t, time_t);
#include "bidderDeal.hpp"
#include "bid.hpp"
#include "rawScore.h"
#include "dll.h"
#ifndef _WIN32
typedef long long LONGLONG;
#else   // _WIN32
#include <windows.h>
#endif  // _WIN32
#ifdef _WIN32
    #include <io.h>
    #define FORCE_DISK_WRITE(fd) _commit(fd)
#else
    #include <unistd.h>
    #define FORCE_DISK_WRITE(fd) fsync(fd)
#endif

static constexpr LONGLONG NANOSECONDS_PER_SECOND = 1000000000LL;
static constexpr LONGLONG MICROSECONDS_PER_SECOND = 1000000LL;

static void upcase_str (char* s) { for (; *s; s++) *s = toupper((unsigned char)*s); }

class funcStats {
    char    name[MAXNAMELEN];
    int     ncalls;
    LONGLONG   ticks;
    funcStats*      next;
    void print ()   { printf ("%s: %d calls, %lld usec/call\n", name, ncalls, ncalls ? (ticks * MICROSECONDS_PER_SECOND / ncalls)/freq : -1); }
  public:
    static LONGLONG freq;
    funcStats (const char* s);
    void addCall (LONGLONG t)  { ncalls++; ticks += t; }
    static void printAll ();
};

funcStats* funcStatList;
LONGLONG funcStats::freq = 0;

funcStats::funcStats (const char* s)
    : ncalls (0), ticks (0), next (funcStatList)
{
    if (freq == 0)
#ifndef _WIN32
    {
        // POSIX: get nanosecond resolution
        struct timespec ts;
        clock_getres(CLOCK_MONOTONIC, &ts);
        freq = NANOSECONDS_PER_SECOND / ts.tv_nsec;
    }
#else   // _WIN32
        QueryPerformanceFrequency ((LARGE_INTEGER*)(&freq));
#endif  // _WIN32
    strcpy (name, s);
    funcStatList = this;
}

void
funcStats::printAll()
{
    for (funcStats* fsp = funcStatList; fsp != NULL; fsp = fsp->next)
        fsp->print ();
}

class funcTimer {
    LONGLONG start_time;
    funcStats* statp;
  public:
    funcTimer (funcStats* p);
    ~funcTimer ();
};

funcTimer::funcTimer (funcStats* p)
    : statp (p)
{
#ifndef _WIN32
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    start_time = ts.tv_sec * NANOSECONDS_PER_SECOND + ts.tv_nsec;
#else   // _WIN32
    QueryPerformanceCounter ((LARGE_INTEGER*)(&start_time));
#endif  // _WIN32
}

funcTimer::~funcTimer ()
{
#ifndef _WIN32
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    LONGLONG end_time = ts.tv_sec * NANOSECONDS_PER_SECOND + ts.tv_nsec;
#else   // _WIN32
    LONGLONG end_time;
    QueryPerformanceCounter ((LARGE_INTEGER*)(&end_time));
#endif  // _WIN32
    statp->addCall (end_time - start_time);
}

class oneBid   {
    bid     b;
    oneBid* next;
  public:
    oneBid (bid inb)    :   b (inb), next(NULL)    {}
    oneBid* getNext()   { return next; }
    bid     getBid()    { return b; }
    void    append (oneBid* ob) { next = ob; }
};

class bidList   {
    oneBid* first;
    oneBid* last;
  public:
    bidList ()  : first(NULL), last(NULL)   {}
    ~bidList();
    oneBid* firstBid()  { return first; }
    void    append (bid bidVal);
};

bidList::~bidList()
{
    oneBid* current = first;
    while (current != NULL) {
        oneBid* next = current->getNext();
        delete current;
        current = next;
    }
}

void
bidList::append (bid bidVal)
{
    oneBid* newBid = new oneBid (bidVal);
    if (first == NULL)
        first = newBid;
    else
        last->append (newBid);
    last = newBid;
}

class convention    {
    bid         b;
    convention* child;
    convention* sibling;
  public:
    void*       rule;
    convention (convention* parent, bid inb, void* r);
    convention* firstChild()    { return child; }
    convention* nextSibling()   { return sibling; }
    bid         thisBid()       { return b; }
    void*       getRule()       { return rule; }
};

convention::convention (convention* parent, bid inb, void* r)
    : b(inb), child(NULL), sibling(NULL), rule(r)
{
    if (!parent)
        return;
    if (parent->firstChild() == NULL)
        parent->child = this;
    else    {
        convention* c = parent->firstChild();
        while (c->nextSibling() != NULL)
            c = c->nextSibling();
        c->sibling = this;
    }
}

const char*
parseBid (const char* cp, bid* bp)
{
    if (*cp == 'P') {
        *bp = bidPass;
        cp++;
        if (*cp == '.')
            return (cp + 1);
    } else if ((*cp >= '1') && (*cp <= '7'))    {
        *bp = (*cp - '0') * 5;
        cp++;
        switch (*cp) {
        case 'C':
        case 'c':
            break;
        case 'D':
        case 'd':
            *bp += 1;
            break;
        case 'H':
        case 'h':
            *bp += 2;
            break;
        case 'S':
        case 's':
            *bp += 3;
            break;
        case 'N':
        case 'n':
            *bp += 4;
            break;
        default:
            *bp = bidInvalid;
            return NULL;
            break;
        }
        cp++;
        if (*cp == '.')
            return (cp + 1);
    }
    // Failed to parse
    *bp = bidInvalid;
    return NULL;
}

class biddingSystem : public convention
{
    void* ruleList;
    char name[MAXNAMELEN];
  public:
    biddingSystem (char* fileName);
    bool isValid()                  { return (ruleList != NULL); }
    void* findRule (const char* ruleName) { return find_rule (ruleList, ruleName); }
    void processRule (void* rule);
};

biddingSystem::biddingSystem (char* fileName)
    : convention (NULL, bidInvalid, NULL)
{
    char* cp = strchr (fileName, '.');
    if (cp != NULL) {
        strncpy(name, fileName, cp - fileName);
        name[cp - fileName] = 0;
    } else
        strcpy (name, fileName);
    printf ("Building system %s\n", name);
    ruleList = read_rules (fileName);
    if (ruleList != NULL)   {
        for (void* rule = ruleList; rule !=NULL; rule = next_rule (rule))
            processRule (rule);
    }
}

void
biddingSystem::processRule (void* def)
{
    const char* cp = rule_name (def) + 1; // Skip the '$'
    if (*cp++ != '.')   // Skip the '.' after the '$'
        return;   // Not a convention
    convention* c1 = this;
    for (;;)    {
        bid b1;
        cp = parseBid (cp, &b1);
        if (cp == NULL) // Not a convention
            return;
        // assert ((b1 >= bidPass) && (b1 <= bidMaxBid));
        convention* c2;
        for (c2 = c1->firstChild(); c2 != NULL; c2 = c2->nextSibling())
            if (c2->thisBid() == b1)    // Found it
                break;
        c1 = (c2 ? c2 : new convention (c1, b1, NULL));
        if (*cp == 0)    // Done parsing
            break;
    }
    // Valid sequence of bids. Now c1 points to the last bid
    // assert (c1 != NULL);
    c1->rule = rule_def (def);
}

class handScores    {
    int score[4][bidMaxBid + 1];
    // int squareSum[4][bidMaxBid + 1];
  public:
    handScores();
    int  get (int who, bid b)           { return score[who][b]; }
    // int  getSquareSum (int who, bid b)  { return squareSum[who][b]; }
    void add (int who, bid b, int val)  { score[who][b] += val; /* squareSum[who][b] += (val * val); */ }
    void addResults (ddTableResults* rp, vulnerability v, char* cp);
};

handScores::handScores()
{
    memset (this, 0, sizeof (handScores));
}

static FILE* detailh;
static const int DDS_SUIT_XLATE[] = { 3, 2, 1, 0, 4 };
inline int ddsSuit (int strain)            { return DDS_SUIT_XLATE[strain]; }
enum { posNorth, posEast, posSouth, posWest };

void
handScores::addResults (ddTableResults* rp, vulnerability v, char* cp)
{
    if (detailh)
        fprintf (detailh, "-,%s,%d,", cp, randCalls);
    bool nsVul = (v == NSVul || v == BothVul);
    for (bid b = bidPass + 1; b <= bidMaxBid; b++)  {
        int s = ddsSuit (bidStrain(b));   // convert to DDS strain ordering
        int scoreNorth = rawScore(bidLevel(b), s, NORMAL, rp->resTable[s][posNorth], nsVul);
        int scoreSouth = rawScore(bidLevel(b), s, NORMAL, rp->resTable[s][posSouth], nsVul);
        add (posNorth, b, scoreNorth);
        add (posSouth, b, scoreSouth);
        if (detailh)
            fprintf (detailh, "%d,%d,", scoreNorth, scoreSouth);
    }
    if (detailh)
        fprintf (detailh, "\n");
}

typedef biddingSystem* systemp;
typedef char* systemNamep;

class auction  {
    static constexpr bidName bidNames[] = { "ER", "ER", "ER", "ER", "P",
                                        "1C", "1D", "1H", "1S", "1N",
                                        "2C", "2D", "2H", "2S", "2N",
                                        "3C", "3D", "3H", "3S", "3N",
                                        "4C", "4D", "4H", "4S", "4N",
                                        "5C", "5D", "5H", "5S", "5N",
                                        "6C", "6D", "6H", "6S", "6N",
                                        "7C", "7D", "7H", "7S", "7N" };

    aHand       hands[NHANDS];
    void*       rules[NHANDS];
    int         bidder;
    bidList     bidding;
    char        bidStr[MAXNAMELEN];
    char*       nextBidp;
    convention* sysp;   // System from this point in auction
    int         whoseTurn;
    handScores  totScores;
    bid         maxBid;
    bid         bestContract;
    int         bestScore;
    int         bestBy;
    int         declarer[DDS_STRAINS];
    vulnerability   v;
    char        dealLIN[256];
    bool    nextBid ();
    int     nextBidder ()   { return (bidder == posNorth ? posSouth : posNorth); }
    int     getDeclarer (bid b);
    void    setDeclarer (bid b);
    bid     considerOverride ();
    bid     suggestContract ();
    void    writeHandInfo ();
    void    initializeBidding ();
    void    setSDAPar ();
    void    writeDetails (FILE* dh, bool bothHands, handScores* hsp, const char* scenario);
  public:
    auction (void* rulep[4]);
    bool    createDeal (char* pbnStr);
    // Creates a deal from pbnStr. If pbnStr is NULL, creates random deal matching the rules
    // Saves North and South hands in hands array
    // Analyzes the deal to identify single dummy par results for each contract and saves in totScores
    void    bidHand (systemp sp);
    void    outputResults ();
    static void writeHeaders (systemNamep* systemNames);
};

auction::auction (void* rulep[4])
    :   v (NoneVul)
{
    initializeBidding ();
    for (int i = 0; i < 4; i++)
        rules[i] = rulep[i];
    *bidStr = 0;
    *dealLIN = 0;
    memset (declarer, -1, DDS_STRAINS * sizeof (int));   // North for all at the beginning
}

inline int
auction::getDeclarer (bid b)
{
    int x = declarer[bidStrain(b)];
    return ((x < 0) ? bidder : x);
}

inline void
auction::setDeclarer (bid b)
{
    int* xp = declarer + bidStrain(b);
    if (*xp < 0)
        *xp = bidder;
}

void
auction::initializeBidding ()
{
    bidder = 0;
    nextBidp = bidStr;
    whoseTurn = 0;
    maxBid = bidNotFound;
    bestContract = bidPass;
    bestScore = 0;
    bestBy = 0;
}

void
auction::bidHand (systemp sp)
{
    initializeBidding ();
    sysp = sp;
    rules[bidder] = rules[nextBidder()] = sp->findRule ("$ANY");   // reset to no info

    while (nextBid ())
        ;

    outputResults ();
}

bool
auction::nextBid ()
{
    bid bidVal;
    if (sysp == NULL)
        bidVal = considerOverride ();
    else    {
        convention* s;
        for (s = sysp->firstChild (); s != NULL; s = s->nextSibling ()) {
            void* rule = s->getRule();
            if ((rule != NULL) && hands[bidder].checkHand (rule))    // Found matching rule
                break;
        }
        if (s == NULL)  {   // No matching rule found. Take a guess at best contract
            bidVal = suggestContract ();
            bidder = -1;
            bidding.append (bidVal);
            if (bidVal > maxBid)    {   // New best bid
                maxBid = bidVal;
                setDeclarer (maxBid);
            }
            sprintf (nextBidp, "%s-AP", bidNames[bidVal]);
            nextBidp += ((bidVal == bidPass) ? 5 : 6);
            return false;
        } else    {   // Found matching rule
            bidVal = s->thisBid ();
            rules[bidder] = combineRule (rules[bidder], s->getRule());
        }
        sysp = s;
    }
    if (bidVal > maxBid)    {   // New best bid
        maxBid = bidVal;
        setDeclarer (maxBid);
        bidder = nextBidder ();
        bidding.append (bidVal);
        sprintf (nextBidp, "%s-", bidNames[bidVal]);
        nextBidp += ((bidVal == bidPass) ? 2 : 3);
        return true;
    } else  {   // Not a new best bid, so Pass and end auction
        bidder = -1;
        bidding.append (bidVal);
        sprintf (nextBidp, "%s-AP", bidNames[bidVal]);
        nextBidp += ((bidVal == bidPass) ? 2 : 3);
        return false;
    }
}

static int totHandsToCheck = 128;

void
auction::writeDetails (FILE* dh, bool bothHands, handScores* hsp, const char* scenario)
{
    char s[LINE_LENGTH];
    (void)writePbnHand (s, hands[bidder].getHand(), NULL, bothHands ? hands[nextBidder()].getHand() : NULL, NULL);
    fprintf (dh, "%s,%s,%c %s,", s, bidStr, (bidder == 0) ? 'N' : 'S', scenario);
    bid b;
    for (b = bidPass + 1; b < bidMaxBid; b++)
        fprintf (dh, "%d,%d,", hsp->get (bidder, b) / totHandsToCheck,
                 hsp->get (nextBidder(), b) / totHandsToCheck);
    // last one will be newline-terminated with no comma
    fprintf (dh, "%d,%d\n", hsp->get (bidder, b) / totHandsToCheck,
             hsp->get (nextBidder(), b) / totHandsToCheck);
}

static int numSystems;
static FILE* oh;
static FILE* bboFp           = NULL;
static int   bboBoardNum     = 0;
static void* partnerRule     = NULL;
static const char* partnerRuleName = NULL;

void
auction::writeHandInfo ()
{
    char s[LINE_LENGTH];
    static int j = 0;
    char* cp = writePbnHand (s, hands[0].getHand(), NULL, hands[2].getHand(), NULL);
    *cp++ = ',';
    *cp++ = (((v == NoneVul) || (v == EWVul)) ? 'N' : 'V');
    *cp++ = ',';
    cp = hands[0].writeSummary (cp);
    cp = hands[2].writeSummary (cp);
    sprintf (cp, "%s - %c,%d,", bidNames[bestContract],
             (bestBy == 0) ? 'N' : 'S', bestScore);
    printf ("\n%d %s", j++, s);
    fprintf (oh, "%s", s);

    if (bboFp && *dealLIN)
        fputs (dealLIN, bboFp);
}

void
auction::outputResults ()
{
    *(--nextBidp) = 0;  // Remove trailing '-'
    if ((totScores.get (getDeclarer (maxBid), maxBid) / totHandsToCheck) > 2000)
           printf("Unusual score: %s,%s - %c,%d,", bidStr, bidNames[maxBid], (getDeclarer (maxBid) == 0) ? 'N' : 'S',
                totScores.get (getDeclarer (maxBid), maxBid) / totHandsToCheck);
        fprintf (oh, "%s,%s - %c,%d,", bidStr, bidNames[maxBid], (getDeclarer (maxBid) == 0) ? 'N' : 'S',
             totScores.get (getDeclarer (maxBid), maxBid) / totHandsToCheck);
}

void
auction::writeHeaders (systemNamep* systemNames)
{
    fprintf (oh, "Hand,Vul,"
                 "N Pts,N Ctls,N KC S,N KC H,N KC D,N KC C,N S,N H,N D,N C,N pattern,N shape,"
                 "S Pts,S Ctls,S KC S,S KC H,S KC D,S KC C,S S,S H,S D,S C,S pattern,S shape,"
                 "Par Bid,Par Score,");
    for (systemNamep* snpp = systemNames; snpp < systemNames + numSystems; snpp++)
        fprintf (oh, "Bidding %s,Contract %s,Score %s,", *snpp, *snpp, *snpp);
    fprintf (oh, "\n");
    if (detailh)    {
        fprintf (detailh, "Hand,Bidding,Bidder,");
        bid b;
        for (b = bidPass + 1; b < bidMaxBid; b++)
            fprintf (detailh, "%s N,%s S,", bidNames[b], bidNames[b]);
        fprintf (detailh, "%s N,%s S\n", bidNames[b], bidNames[b]);  // last one will be newline-terminated
    }
}

static int trumpFilter[DDS_STRAINS] = {0, 0, 0, 0, 0}; // North by default
enum { maxHandsPerRound = 32 };

static
void
runSimulation (handScores* hsp, bidderDeal* dp, bool twoKnown, vulnerability v)
{
	// static funcTimer simTimer ("CalcAllTables");
	int spack[PACK_SIZE];
	int u = thePack.save_pack (spack);  // Take a snapshot of the pack
    assert (u == (PACK_SIZE - NCARDS_IN_HAND * (twoKnown ? 2 : 1)));
    int handsChecked = 0;
    int handsThisRound = 0;
    ddTableDealsPBN DDdealsPBN;
    ddTablesRes tableRes;
    memset (&tableRes, 0, sizeof (ddTablesRes));
    allParResults pres;
    static funcStats ddsStats ("dds");
    for (int i = 0; i < MAXTRIES; i++)  {	/* Generate other hands */
        // printf (".");
        if (dp->dealAndCheck (false, true, !twoKnown, true))   {
            // printf ("|");
            char* cp = DDdealsPBN.deals[handsThisRound].cards;
            *cp++ = 'N';
            *cp++ = ':';
            dp->makePBNrec (cp);
            handsThisRound++;
            handsChecked++;
            if ((handsThisRound == maxHandsPerRound) || (handsChecked == totHandsToCheck))   {
                // Analyze this subset
                DDdealsPBN.noOfTables = handsThisRound;
                printf (".");
                {
                    funcTimer tmr (&ddsStats);
                    int res = CalcAllTablesPBN (&DDdealsPBN, 0, trumpFilter, &tableRes, &pres);
                    if (res != RETURN_NO_FAULT) {
                        enum { lineSize = 80 };
                        char line[lineSize];
                        ErrorMessage(res, line);
                        printf("DDS error: %s\n", line);
                        exit (1);
                    }
                }
                // Process analysis results
                ddTableResults* rEnd = tableRes.results + handsThisRound;
                ddTableDealPBN* dealp = DDdealsPBN.deals;
                for (ddTableResults* resp = tableRes.results; resp < rEnd; resp++, dealp++)
                    hsp->addResults (resp, v, dealp->cards);
                if (handsChecked == totHandsToCheck)
                    break;
                enum { progressInterval = 1000 };
                if ((handsChecked % progressInterval) == 0)
                    printf ("Processed hand %d\n", handsChecked);
                handsThisRound = 0;
            }
        }
        thePack.restore_pack (u, spack);
    }
}

bid
auction::suggestContract ()
{
    // return bidMaxBid;
    if (detailh) {
        fprintf (detailh, "Suggest contract after %s\n", bidStr);
        fprintf (detailh, "Rule for partner's hand: %s\n", ((TPTR)(rules[(bidder + 2) % NHANDS]))->t_desc);
    }
    bidderDeal deal (rules[bidder], rules[(bidder + 1) % NHANDS],
                     rules[(bidder + 2) % NHANDS], rules[(bidder + 3) % NHANDS]);
    thePack.reshuffle ();
    if (!deal.enterNorth (&hands[bidder]))  {
        printf ("Error entering known hand\n");
        return bidInvalid;
    }

    handScores expectedScores;
    runSimulation(&expectedScores, &deal, false, v);
    printf (",");

    if (detailh && (maxBid > bidNotFound))
        writeDetails (detailh, false, &expectedScores, "Bid");

    // baseline is current bid, see if we can improve
    int bestExp = (maxBid > bidPass)
                  ? expectedScores.get (getDeclarer (maxBid), maxBid)
                  : 0;
    bid bestBid = bidPass;
    bid b1;
    for (b1 = maxBid + 1; b1 <= bidMaxBid; b1++)   {
        int y = expectedScores.get (getDeclarer (b1), b1);
        if (y > bestExp)   {
            bestBid = b1;
            bestExp = y;
        }
    }
    return bestBid;
}

void
auction::setSDAPar ()
{
    bestScore = 0;
    bestContract = bidPass;
    bestBy = 0;
    bid b1;
    int y;
    for (b1 = bidPass + 1; b1 <= bidMaxBid; b1++)   {
        if ((y = totScores.get(posNorth, b1)) >= bestScore)   {
            bestContract = b1;
            bestScore = y;
            bestBy = posNorth;
        }
        if ((y = totScores.get(posSouth, b1)) >= bestScore)   {
            bestContract = b1;
            bestScore = y;
            bestBy = posSouth;
        }
    }
    bestScore /= totHandsToCheck;
}

bool
auction::createDeal (char* pbnStr)
{
    bidderDeal deal (rules[0], rules[1], rules[2], rules[3], partnerRule);
    printf(":");
    if (pbnStr == NULL) {
        int i;
        for (i = 0; i < MAXTRIES; i++)  {
            thePack.reshuffle();
            if (deal.dealAndCheck (true, true, true, true)
                    && deal.checkPartnerRule ())
                break;
            // printf(",");
        }
        if (i == MAXTRIES)  {   // Can't create a deal that meets rules
            bidder = -1;
            return false;
        }
    } else  {
        thePack.reshuffle ();
        if (!deal.enterPbn (pbnStr))
            return false;
    }
    printf(".");
    if (bboFp)
        deal.makeLINrec (dealLIN, ++bboBoardNum);   // capture before reshuffle clears hands
    thePack.reshuffle();
    if (!deal.saveNorth (&hands[0]) || !deal.saveSouth (&hands[2])) {
        printf ("Error saving known hands\n");
        return false;
    }
    runSimulation(&totScores, &deal, true, v);
    printf (",");

    bidder = 0;
    if (detailh)
        writeDetails (detailh, true, &totScores, "Par");

    setSDAPar ();
    writeHandInfo ();
    return true;
}

bid
auction::considerOverride ()
{
    return bidPass;
}

static char inFileList[PATH_MAX];
static char path[PATH_MAX];
static char output[PATH_MAX];
static char pbnFile[PATH_MAX];
static char detailFile[PATH_MAX];

int
main (int argc, char** argv)
{
    SetMaxThreads(0);

    strcpy (path, ".");
    strcpy (inFileList, "input.txt");
    strcpy (output, "output.csv");
    char bboFile[PATH_MAX];
    strcpy (bboFile, "bbo.lin");
    FILE* pbnh = NULL;
    systemp* systemsList;
    systemNamep* systemNames;

	// Syntax: dealer [-d directory] [-i infile[,infile]...] [-o outfile] [=v detailfile] [reps [ruleN [ruleE [ruleS [ruleW]]]]]
    // Missing rules will be replaced by $ANY
    int i = 1;
    while ((argc > (i+1)) && (*argv[i] == '-')) {
        if (strcmp (argv[i], "-d") == 0)    {
            snprintf(path, PATH_MAX, "%s", argv[i+1]);
            i += 2;
            continue;
        }
        if (strcmp (argv[i], "-i") == 0)    {
            strcpy (inFileList, argv[i + 1]);
            i += 2;
            continue;
        }
        if (strcmp (argv[i], "-o") == 0)    {
            strcpy (output, argv[i + 1]);
            i += 2;
            continue;
        }
        if (strcmp (argv[i], "-p") == 0)    {
            strcpy (pbnFile, argv[i + 1]);
            i += 2;
            continue;
        }
        if (strcmp (argv[i], "-v") == 0)    {
            strcpy (detailFile, argv[i + 1]);
            i += 2;
            continue;
        }
        if (strcmp (argv[i], "-b") == 0)    {
            strcpy (bboFile, argv[i + 1]);
            i += 2;
            continue;
        }
        if (strcmp (argv[i], "-nchecks") == 0)    {
            totHandsToCheck = atoi (argv[i + 1]);
            i += 2;
            continue;
        }
        if (strcmp (argv[i], "-P") == 0)    {
            partnerRuleName = argv[i + 1];
            i += 2;
            continue;
        }
        if (strcmp (argv[i], "-s") == 0)    {
            setRngSeed ((unsigned long)atol (argv[i + 1]));
            i += 2;
            continue;
        }
    }
    if (chdir (path) != 0)  {
        printf ("failed to change directory to %s\n", path);
        perror ("chdir");
        exit (1);
    }
    oh = fopen (output, "w");
    if (oh == NULL)   {
        printf ("Failed to open output%s", output);
        perror ("fopen");
        exit (1);
    }
    bboFp = fopen (bboFile, "w");
    if (bboFp == NULL)   {
        printf ("Failed to open %s", bboFile);
        perror ("fopen");
        exit (1);
    }
    if (*pbnFile)   {
        pbnh = fopen (pbnFile, "r");
        if (pbnh == NULL)   {
            printf ("Failed to open %s", pbnFile);
            perror ("fopen");
            exit (1);
        }
    }
    if (*detailFile)    {
        detailh = fopen (detailFile, "w");
        if (detailh == NULL)   {
            printf ("Failed to open %s", detailFile);
            perror ("fopen");
            exit (1);
        }
    }

    // Process inFileList
    char* inFile = inFileList;
    numSystems = 1;
    while ((inFile = strchr (inFile, ',')) != NULL) {
        inFile++;
        numSystems++;
    }
    systemp* s = systemsList = new systemp[numSystems];
    systemNamep* snpp = systemNames = new systemNamep[numSystems];
    if ((systemsList == NULL) || (systemNames == NULL))
        exit (1);
    char* fileEnd;
    inFile = inFileList;
    do {
        if ((fileEnd = strchr (inFile, ',')) != NULL)
            *fileEnd = 0;
        *s = new biddingSystem (inFile);
        if (!(*s)->isValid())   {
            printf ("failed to build system\n");
            exit (1);
        }
        s++;
        *snpp++ = inFile;
        inFile = fileEnd ? fileEnd + 1 : NULL;
    } while (fileEnd != NULL);
    assert (s == (systemsList + numSystems));

    if (partnerRuleName) {
        char prBuf[MAXNAMELEN];
        snprintf (prBuf, sizeof (prBuf), "$%s", partnerRuleName);
        upcase_str (prBuf + 1);
        partnerRule = systemsList[0]->findRule (prBuf);
        if (partnerRule == NULL) {
            printf ("Could not find partner rule %s\n", partnerRuleName);
            exit (1);
        }
    }

    // Now parse the positional parameters
    int reps = (argc > i) ? atoi(argv[i++]) : 1;
    void* rulep[4];
    char prefixed[MAXNAMELEN];
    int remaining = argc - i;
    if (remaining <= 2) {
        snprintf(prefixed, sizeof(prefixed), "$%s", (argc > i) ? argv[i++] : "ANY");
        upcase_str (prefixed + 1);
        rulep[0] = systemsList[0]->findRule (prefixed);
        rulep[1] = systemsList[0]->findRule ("$ANY");
        snprintf(prefixed, sizeof(prefixed), "$%s", (argc > i) ? argv[i++] : "ANY");
        upcase_str (prefixed + 1);
        rulep[2] = systemsList[0]->findRule (prefixed);
        rulep[3] = systemsList[0]->findRule ("$ANY");
    } else {
        snprintf(prefixed, sizeof(prefixed), "$%s", argv[i++]);
        upcase_str (prefixed + 1);
        rulep[0] = systemsList[0]->findRule (prefixed);
        snprintf(prefixed, sizeof(prefixed), "$%s", argv[i++]);
        upcase_str (prefixed + 1);
        rulep[1] = systemsList[0]->findRule (prefixed);
        snprintf(prefixed, sizeof(prefixed), "$%s", argv[i++]);
        upcase_str (prefixed + 1);
        rulep[2] = systemsList[0]->findRule (prefixed);
        snprintf(prefixed, sizeof(prefixed), "$%s", (argc > i) ? argv[i++] : "ANY");
        upcase_str (prefixed + 1);
        rulep[3] = systemsList[0]->findRule (prefixed);
    }

	auction::writeHeaders (systemNames);
    time_t t0 = time(0);
    int repsDone = 0;
    time_t t1;
    int snum;
	if (pbnh == NULL)   {
        while (reps > 0)    {
            if (detailh) {
                fflush (detailh);
                fprintf (detailh, "Processing deal %d,,%d\n", repsDone, randCalls);
            }
            auction a (rulep);
            (void)a.createDeal (NULL);
            for (snum = 0; snum < numSystems; snum++)
                a.bidHand (systemsList[snum]);
            t1 = time (0) - t0;
            print_time_estimate (t1, t1 * (--reps) / (++repsDone));
            fprintf (oh, "\n");
            fflush (oh);
        }
	} else  {
        char pbnDeal[LINE_LENGTH];
        int done = 0;
        while ((fgets (pbnDeal, LINE_LENGTH, pbnh)) != NULL)    {
            if (detailh) {
                fflush (detailh);
                fprintf (detailh, "Processing deal %d,,%d\n", repsDone, randCalls);
            }
            auction a (rulep);
            if (a.createDeal (pbnDeal))   {
                for (snum = 0; snum < numSystems; snum++)
                    a.bidHand (systemsList[snum]);
                t1 = time (0) - t0;
                print_time_estimate (t1, t1 / (++repsDone));
                fprintf (oh, "\n");
                fflush (oh);
            }
            if (++done == 10) {
                FORCE_DISK_WRITE(fileno(oh));
                done = 0;
            }
        }
	}
    printf ("bidHand took %lld seconds\n", (long long)(time(0) - t0));
    funcStats::printAll ();
    fclose (oh);
    if (bboFp)
        fclose (bboFp);
    if (detailh)
        fclose (detailh);
    if (pbnh)
        fclose (pbnh);
    return 0;
}
