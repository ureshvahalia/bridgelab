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
#include "log.h"
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

static bool rulesOnlyMode = false;   // --rules-only: stop at the first unmatched rule, no simulation/guessing
static bool validateMode  = false;   // --validate: check a system's rule tree for overlaps/gaps/dead rules, don't deal

class funcStats {
    char    name[MAXNAMELEN];
    int     ncalls;
    LONGLONG   ticks;
    funcStats*      next;
    void print ()   { logDebug ("%s: %d calls, %lld usec/call\n", name, ncalls, ncalls ? (ticks * MICROSECONDS_PER_SECOND / ncalls)/freq : -1); }
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

// A match, plus `acc` (the caller's starting accumulator) folded together
// with the negation of every rejected sibling scanned on the way to it --
// see findMatchingChild() below.
struct matchResult {
    convention* match;
    void*       acc;
};

// Finds the first child of `node` whose rule matches `hand`, falling back to
// rootSysp's children if nothing at `node` matches and every real call so
// far in this auction was Pass. Every "$."-rule's path is built from the
// tree root (see biddingSystem::processRule), so a rule written as a bare
// opening (e.g. "$.1N.") only lives at the root and is never a child of
// "$.P." unless the file also spells out "$.P.1N.". When nothing but passes
// precede this point, retrying against the root's children is what makes
// third/fourth-hand openers fall back to the same requirements as a
// first-hand opener unless the rules file explicitly overrides them with
// its own "$.P...." rule (which, being found above, always takes priority
// over this fallback). Returns a NULL match if nothing matches even after
// that.
//
// Negative inference: first-match-wins means every sibling scanned before
// the match is known to have failed for this hand -- not merely "never
// checked" -- so its negation is folded into the returned `acc` alongside
// whatever `acc` already held. Without this, a hand accumulated from "S
// bid 2S over 2H" only ever gains "Spades >= 4" (2S's own rule) and never
// "NOT (Hearts >= 4)" (2H's rule, rejected first) -- so callers that later
// use `acc` to *simulate* this hand (suggestContract()'s partner-hand SDA;
// --validate's reachability sampling -- see walkValidate()) could still
// generate/count hands with both 4+ hearts and 4+ spades, which the actual
// auction already ruled out by choosing 2S over 2H. Only siblings actually
// scanned-and-rejected qualify: siblings *after* the match are never
// evaluated at all, so nothing can be soundly inferred about them -- if the
// file has an undetected OVERLAP (see --validate), one of those unscanned
// siblings could in principle also have matched, which is exactly why this
// is only as complete as "no OVERLAP at this decision point". The matched
// rule's own contribution is deliberately *not* folded into the returned
// `acc` -- callers combine that separately, same as before this function
// returned an accumulator at all.
//
// Shared by the live auction (nextBid()) and --validate's offline tree
// walk, so the two can never disagree about what "reachable"/"matches" mean.
static matchResult
findMatchingChild (convention* node, convention* rootSysp, bool allPassSoFar, handBase& hand, void* acc)
{
    for (convention* s = node->firstChild (); s != NULL; s = s->nextSibling ()) {
        void* rule = s->getRule ();
        if (rule == NULL)
            continue;
        if (hand.checkHand (rule))
            return { s, acc };
        acc = (acc == NULL) ? negateRule (rule) : combineRule (acc, negateRule (rule));
    }
    if (allPassSoFar && (node != rootSysp))    {
        for (convention* s = rootSysp->firstChild (); s != NULL; s = s->nextSibling ()) {
            void* rule = s->getRule ();
            if (rule == NULL)
                continue;
            if (hand.checkHand (rule))
                return { s, acc };
            acc = (acc == NULL) ? negateRule (rule) : combineRule (acc, negateRule (rule));
        }
    }
    return { NULL, acc };
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
    logInfo ("Building system %s\n", name);
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
    // Bid-sequence legality (ascending rank) is enforced upstream, in the
    // shared Maj/Min macro-expansion preprocessor (shared/majMinExpand.cpp),
    // for every "$."-shaped rule name — auto-generated or hand-typed alike —
    // before this tree is ever built.
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

static vulnerability defaultVul   = NoneVul;   // set from -V; None unless specified
static bool          vulFromBoard = false;     // -V Bno: derive vulnerability from board number instead

// Standard duplicate board-vulnerability cycle, board 1 = None, 2 = NS, 3 = EW, 4 = Both, ...
static const vulnerability boardVulCycle[16] = {
    NoneVul, NSVul,   EWVul,   BothVul,
    NSVul,   EWVul,   BothVul, NoneVul,
    EWVul,   BothVul, NoneVul, NSVul,
    BothVul, NoneVul, NSVul,   EWVul
};

static vulnerability
vulForBoard (int bno)   // bno is 1-based
{
    return boardVulCycle[(bno - 1) % 16];
}

static constexpr bidName bidNames[] = { "ER", "ER", "ER", "ER", "P",
                                    "1C", "1D", "1H", "1S", "1N",
                                    "2C", "2D", "2H", "2S", "2N",
                                    "3C", "3D", "3H", "3S", "3N",
                                    "4C", "4D", "4H", "4S", "4N",
                                    "5C", "5D", "5H", "5S", "5N",
                                    "6C", "6D", "6H", "6S", "6N",
                                    "7C", "7D", "7H", "7S", "7N" };

class auction  {
    aHand       hands[NHANDS];
    void*       rules[NHANDS];
    int         bidder;
    bidList     bidding;
    char        bidStr[MAXNAMELEN];
    char*       nextBidp;
    convention* sysp;   // System from this point in auction
    convention* rootSysp;   // Root of the tree for this bidHand() call, for the pass-retry below
    bool        allPassSoFar;  // True until the first non-Pass call of this auction
    char        guessPointBidStr[MAXNAMELEN];  // auction so far, at the point suggestContract() would be called
    bool        guessWasCalled;    // true once we've reached the "no matching rule" branch
    int         whoseTurn;
    handScores  totScores;
    bid         maxBid;
    bid         bestContract;
    int         bestScore;
    int         bestBy;
    int         parScore;  // = bestScore at the time setSDAPar() ran; survives later initializeBidding() resets
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
    void    bidHand (systemp sp, int sysIndex);
    void    outputResults (int sysIndex);
    static void writeHeaders (systemNamep* systemNames);
    static void writeSummary (systemNamep* systemNames);
};

auction::auction (void* rulep[4])
    :   v (defaultVul)
{
    initializeBidding ();
    parScore = 0;
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
    allPassSoFar = true;
    guessWasCalled = false;
    *guessPointBidStr = 0;
}

void
auction::bidHand (systemp sp, int sysIndex)
{
    initializeBidding ();
    sysp = sp;
    rootSysp = sp;
    rules[bidder] = rules[nextBidder()] = sp->findRule ("$ANY");   // reset to no info

    while (nextBid ())
        ;

    outputResults (sysIndex);
}

bool
auction::nextBid ()
{
    bid bidVal;
    if (sysp == NULL)
        bidVal = considerOverride ();
    else    {
        matchResult mr = findMatchingChild (sysp, rootSysp, allPassSoFar, hands[bidder], rules[bidder]);
        convention* s = mr.match;
        if (s == NULL)  {   // No matching rule found. Take a guess at best contract
            guessWasCalled = true;
            snprintf (guessPointBidStr, sizeof (guessPointBidStr), "%s", bidStr);
            {
                size_t len = strlen (guessPointBidStr);
                if ((len > 0) && (guessPointBidStr[len - 1] == '-'))
                    guessPointBidStr[len - 1] = 0;
            }
            if (rulesOnlyMode)  {   // Rules-only: stop here, no simulation, no final contract
                bidder = -1;
                return false;
            }
            bidVal = suggestContract ();
            if (bidVal > maxBid)    {   // New best bid
                maxBid = bidVal;
                setDeclarer (maxBid);
            }
            bidder = -1;
            bidding.append (bidVal);
            sprintf (nextBidp, "%s-AP", bidNames[bidVal]);
            nextBidp += ((bidVal == bidPass) ? 5 : 6);
            return false;
        } else    {   // Found matching rule
            bidVal = s->thisBid ();
            rules[bidder] = combineRule (mr.acc, s->getRule());
        }
        sysp = s;
    }
    if (bidVal != bidPass)
        allPassSoFar = false;
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
static int   boardNum        = 0;   // 1-based; counts successfully-created deals
static void* partnerRule     = NULL;
static const char* partnerRuleName = NULL;
static int*  impSum          = NULL;   // running IMPs-vs-par total, per system
static int*  parMatches      = NULL;   // count of boards where we bid the par contract, per system
static int   boardsScored    = 0;

void
auction::writeHandInfo ()
{
    char s[LINE_LENGTH];

    char* cp = writePbnHand (s, hands[0].getHand(), NULL, hands[2].getHand(), NULL);
    *cp++ = ',';
    *cp++ = (((v == NoneVul) || (v == EWVul)) ? 'N' : 'V');
    *cp++ = ',';
    cp = hands[0].writeSummary (cp);
    cp = hands[2].writeSummary (cp);
    if (rulesOnlyMode)
        *cp = 0;   // no par computed; nothing to append
    else
        sprintf (cp, "%s - %c,%d,", bidNames[bestContract],
                 (bestBy == 0) ? 'N' : 'S', bestScore);
    fprintf (oh, "%s", s);

    if (bboFp && *dealLIN)
        fputs (dealLIN, bboFp);
}

void
auction::outputResults (int sysIndex)
{
    if (nextBidp > bidStr)
        *(--nextBidp) = 0;  // Remove trailing '-'
    const char* auctionSoFar = guessWasCalled ? guessPointBidStr : bidStr;
    fprintf (oh, "%s,", auctionSoFar);
    if (rulesOnlyMode)
        return;   // no simulation was run; no contract/score to report
    int score = totScores.get (getDeclarer (maxBid), maxBid) / totHandsToCheck;
    if (score > 2000)
           logWarning ("Unusual score: %s,%s - %c,%d,", bidStr, bidNames[maxBid], (getDeclarer (maxBid) == 0) ? 'N' : 'S', score);
    int impDiff = imps (score - parScore);
    fprintf (oh, "%s,%s - %c,%d,%d,", bidStr, bidNames[maxBid], (getDeclarer (maxBid) == 0) ? 'N' : 'S', score, impDiff);
    impSum[sysIndex] += impDiff;
    if (score == parScore)
        parMatches[sysIndex]++;
}

void
auction::writeHeaders (systemNamep* systemNames)
{
    fprintf (oh, "Hand,Vul,"
                 "N Pts,N Ctls,N KC S,N KC H,N KC D,N KC C,N S,N H,N D,N C,N pattern,N shape,"
                 "S Pts,S Ctls,S KC S,S KC H,S KC D,S KC C,S S,S H,S D,S C,S pattern,S shape,");
    if (!rulesOnlyMode)
        fprintf (oh, "Par Bid,Par Score,");
    for (systemNamep* snpp = systemNames; snpp < systemNames + numSystems; snpp++)   {
        if (rulesOnlyMode)
            fprintf (oh, "Auction %s,", *snpp);
        else
            fprintf (oh, "Auction %s,Bidding %s,Contract %s,Score %s,IMPs vs Par %s,", *snpp, *snpp, *snpp, *snpp, *snpp);
    }
    fprintf (oh, "\n");
    if (detailh)    {
        fprintf (detailh, "Hand,Bidding,Bidder,");
        bid b;
        for (b = bidPass + 1; b < bidMaxBid; b++)
            fprintf (detailh, "%s N,%s S,", bidNames[b], bidNames[b]);
        fprintf (detailh, "%s N,%s S\n", bidNames[b], bidNames[b]);  // last one will be newline-terminated
    }
}

void
auction::writeSummary (systemNamep* systemNames)
{
    systemNamep* snpp = systemNames;
    for (int sysIndex = 0; sysIndex < numSystems; sysIndex++, snpp++)   {
        double avgImps = boardsScored ? ((double)impSum[sysIndex] / boardsScored) : 0.0;
        double parPct  = boardsScored ? (100.0 * parMatches[sysIndex] / boardsScored) : 0.0;
        logInfo ("System %s: average IMPs vs par = %.2f, bid par contract %d/%d (%.1f%%)\n",
                *snpp, avgImps, parMatches[sysIndex], boardsScored, parPct);
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
                {
                    funcTimer tmr (&ddsStats);
                    int res = CalcAllTablesPBN (&DDdealsPBN, 0, trumpFilter, &tableRes, &pres);
                    if (res != RETURN_NO_FAULT) {
                        enum { lineSize = 80 };
                        char line[lineSize];
                        ErrorMessage(res, line);
                        logError ("DDS error: %s\n", line);
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
                    logInfo ("Processed hand %d\n", handsChecked);
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
        logError ("Error entering known hand\n");
        return bidInvalid;
    }

    handScores expectedScores;
    runSimulation(&expectedScores, &deal, false, v);

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
    parScore = bestScore;   // preserved across the initializeBidding() reset at the top of each bidHand()
}

bool
auction::createDeal (char* pbnStr)
{
    bidderDeal deal (rules[0], rules[1], rules[2], rules[3], partnerRule);
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
    boardNum++;
    if (vulFromBoard)
        v = vulForBoard (boardNum);
    if (bboFp)
        deal.makeLINrec (dealLIN, boardNum);   // capture before reshuffle clears hands
    thePack.reshuffle();
    if (!deal.saveNorth (&hands[0]) || !deal.saveSouth (&hands[2])) {
        logError ("Error saving known hands\n");
        return false;
    }
    bidder = 0;
    if (!rulesOnlyMode)  {   // Par requires the full-hand SDA; skip it when only checking rule coverage
        runSimulation(&totScores, &deal, true, v);
        if (detailh)
            writeDetails (detailh, true, &totScores, "Par");
        setSDAPar ();
    }
    writeHandInfo ();
    return true;
}

bid
auction::considerOverride ()
{
    return bidPass;
}

// ── System validation (--validate mode) ────────────────────────────────────
//
// Walks a bidding system's convention tree offline (no live auction, no
// DDS) and flags likely rule-authoring mistakes at every decision point (a
// tree node with 2+ children):
//   OVERLAP     more than one sibling's rule can match the same hand --
//               findMatchingChild()'s first-match-wins silently picks one
//               and hides the ambiguity from the rules writer.
//   GAP         no sibling matches -- the live auction would silently fall
//               through to suggestContract()'s simulated guess.
//   UNREACHABLE nothing satisfies the path leading to this node at all --
//               likely dead code, usually because an ancestor rule already
//               rules it out.
//   DUPLICATE   two siblings have byte-identical rule text -- almost
//               always a copy-paste mistake, and free to catch (no
//               sampling needed).
//
// Reachability sampling reconstructs the exact precondition nextBid()
// builds live: rules[bidder] accumulates only that bidder's own rules
// across their own turns (see bidHand()/nextBid()), so this walk keeps two
// separate accumulators, one per seat, each combined in only on that
// seat's own tree depth (odd depths are North's own bids, even depths
// South's -- see initializeBidding(): bidder starts at posNorth and
// alternates via nextBidder()).
enum { VALIDATE_TARGET_SAMPLES = 3000, VALIDATE_MAXTRIES = 2000000 };

struct validateStats {
    int decisionPoints;
    int overlaps;
    int gaps;
    int unreachable;
    int duplicates;
};

static void
reportDuplicateSiblings (convention* node, const char* pathStr, validateStats* stats)
{
    for (convention* a = node->firstChild (); a != NULL; a = a->nextSibling ())    {
        void* ra = a->getRule ();
        if (ra == NULL)
            continue;
        for (convention* b = a->nextSibling (); b != NULL; b = b->nextSibling ())    {
            void* rb = b->getRule ();
            if ((rb != NULL) && (strcmp (((TPTR)ra)->t_desc, ((TPTR)rb)->t_desc) == 0))    {
                logWarning ("[validate] DUPLICATE at %s: %s and %s use identical rule text \"%s\"\n",
                            pathStr, bidNames[a->thisBid ()], bidNames[b->thisBid ()], ((TPTR)ra)->t_desc);
                stats->duplicates++;
            }
        }
    }
}

// Samples hands satisfying `precondition` (the reachability requirement for
// `node`'s own bidder to have gotten here) and, for each one, counts how
// many of `node`'s children match. Returns false if nothing satisfies
// `precondition` after VALIDATE_MAXTRIES tries -- callers should prune
// recursion below an unreachable node, since nothing under it is reachable
// either.
static bool
checkDecisionPoint (convention* node, convention* rootSysp, bool allPassSoFar,
                     void* precondition, const char* pathStr, validateStats* stats)
{
    stats->decisionPoints++;
    const char* label = *pathStr ? pathStr : "(opening)";
    reportDuplicateSiblings (node, label, stats);

    aHand hand;
    int reachable = 0, overlapHits = 0, gapHits = 0, tries;
    char overlapExample[LINE_LENGTH] = "";
    char overlapBids[MAXNAMELEN] = "";
    char gapExample[LINE_LENGTH] = "";

    for (tries = 0; tries < VALIDATE_MAXTRIES; tries++)    {
        thePack.reshuffle ();
        hand.deal ();
        if (!hand.checkHand (precondition))
            continue;
        reachable++;
        int matches = 0;
        for (convention* c = node->firstChild (); c != NULL; c = c->nextSibling ())
            if ((c->getRule () != NULL) && hand.checkHand (c->getRule ()))
                matches++;
        if (matches == 0)    {
            // Not a genuine gap if the third/fourth-hand-opener fallback
            // (see findMatchingChild()) would rescue this hand at runtime.
            // The accumulator findMatchingChild() would also build here is
            // irrelevant -- this call only asks whether a match exists.
            if (findMatchingChild (node, rootSysp, allPassSoFar, hand, NULL).match == NULL)    {
                gapHits++;
                if (!*gapExample)
                    writePbnHand (gapExample, hand.getHand (), NULL, NULL, NULL);
            }
        } else if (matches >= 2)    {
            overlapHits++;
            if (!*overlapExample)    {
                writePbnHand (overlapExample, hand.getHand (), NULL, NULL, NULL);
                // Re-scan (cheap: only happens once per decision point) to
                // name which siblings tied for this example hand.
                for (convention* c = node->firstChild (); c != NULL; c = c->nextSibling ())
                    if ((c->getRule () != NULL) && hand.checkHand (c->getRule ()))    {
                        size_t len = strlen (overlapBids);
                        snprintf (overlapBids + len, sizeof (overlapBids) - len,
                                  "%s%s", len ? ", " : "", bidNames[c->thisBid ()]);
                    }
            }
        }
        if (reachable >= VALIDATE_TARGET_SAMPLES)
            break;
    }

    if (reachable == 0)    {
        logWarning ("[validate] UNREACHABLE at %s: no hand satisfies the path here after %d tries\n",
                    label, tries);
        stats->unreachable++;
        return false;
    }
    if (overlapHits > 0)    {
        logWarning ("[validate] OVERLAP at %s: %d/%d sampled hands (%.1f%%) match more than one option (%s), e.g. %s\n",
                    label, overlapHits, reachable, 100.0 * overlapHits / reachable, overlapBids, overlapExample);
        stats->overlaps++;
    }
    if (gapHits > 0)    {
        logWarning ("[validate] GAP at %s: %d/%d sampled hands (%.1f%%) match no option, e.g. %s\n",
                    label, gapHits, reachable, 100.0 * gapHits / reachable, gapExample);
        stats->gaps++;
    }
    return true;
}

// combineRule() itself assumes both operands are non-NULL (write_node()'s
// TAND case dereferences both sides' t_desc unconditionally) -- safe in the
// live auction because rules[bidder] is seeded from $ANY, which every rules
// file is expected to define. --validate can't rely on that convention, so
// treat a NULL side as "no constraint" (matching checkHand(NULL)'s own
// semantics) instead of ever calling combineRule() with one.
static void*
combineRuleSafe (void* acc, void* rule)
{
    if (rule == NULL)
        return acc;
    if (acc == NULL)
        return rule;
    return combineRule (acc, rule);
}

static void
walkValidate (convention* node, convention* rootSysp, int depth,
              void* northAcc, void* southAcc, bool allPassSoFar,
              const char* pathStr, validateStats* stats)
{
    if (depth > 0)    {
        if (node->thisBid () != bidPass)
            allPassSoFar = false;
        // A node with no rule of its own is a pure path waypoint -- it
        // exists only because some deeper "$."-sequence uses it as a
        // prefix (see biddingSystem::processRule), never because it was
        // itself matched against a hand. Unlike the live auction (which
        // only ever combines in a *matched* child's rule -- see
        // findMatchingChild()), this walk visits every node structurally,
        // so combineRuleSafe() (rather than combineRule() directly) treats
        // a NULL rule here as "no additional constraint".
        if (depth % 2 == 1)   // odd depth: North's own bid/rule
            northAcc = combineRuleSafe (northAcc, node->getRule ());
        else                  // even depth: South's own bid/rule
            southAcc = combineRuleSafe (southAcc, node->getRule ());
    }
    if (node->firstChild () == NULL)
        return;   // leaf -- nothing to validate below it

    bool childIsNorth = (depth % 2 == 0);   // depth+1 parity
    void* childPrecondition = childIsNorth ? northAcc : southAcc;
    if (!checkDecisionPoint (node, rootSysp, allPassSoFar, childPrecondition, pathStr, stats))
        return;   // unreachable -- nothing below here is reachable either

    // Thread the same negative inference findMatchingChild() applies to the
    // live auction through this structural walk too: c's own subtree is
    // only reachable via a hand that also failed every sibling scanned
    // before c, so siblingAcc accumulates their negations as the loop
    // proceeds, folded only into whichever of northAcc/southAcc belongs to
    // c's own bidder -- the other seat's accumulator passes through
    // unchanged, same as the depth>0 combine above.
    void* siblingAcc = childPrecondition;
    for (convention* c = node->firstChild (); c != NULL; c = c->nextSibling ())    {
        char childPath[MAXNAMELEN];
        snprintf (childPath, sizeof (childPath), "%s%s%s", pathStr, *pathStr ? "-" : "", bidNames[c->thisBid ()]);
        void* childNorthAcc = childIsNorth ? siblingAcc : northAcc;
        void* childSouthAcc = childIsNorth ? southAcc : siblingAcc;
        walkValidate (c, rootSysp, depth + 1, childNorthAcc, childSouthAcc, allPassSoFar, childPath, stats);
        void* rule = c->getRule ();
        if (rule != NULL)
            siblingAcc = combineRuleSafe (siblingAcc, negateRule (rule));
    }
}

static void
validateSystem (biddingSystem* sys, const char* sysName)
{
    validateStats stats = { 0, 0, 0, 0, 0 };
    void* anyRule = sys->findRule ("$ANY");   // mirrors bidHand()'s initial rules[bidder] reset
    logInfo ("Validating system %s...\n", sysName);
    walkValidate (sys, sys, 0, anyRule, anyRule, true, "", &stats);
    logInfo ("System %s: %d decision point(s), %d overlap, %d gap, %d unreachable, %d duplicate-text\n",
             sysName, stats.decisionPoints, stats.overlaps, stats.gaps, stats.unreachable, stats.duplicates);
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

	// Syntax: dealer [-d directory] [-i infile[,infile]...] [-o outfile] [=v detailfile]
	//                [-V None|NS|EW|Both|Bno] [-s seed] [-L error|warning|info|debug]
	//                [reps [ruleN [ruleE [ruleS [ruleW]]]]]
    // Missing rules will be replaced by $ANY
    int i = 1;
    while ((i < argc) && (*argv[i] == '-')) {
        if (strcmp (argv[i], "--rules-only") == 0)    {
            rulesOnlyMode = true;
            i += 1;
            continue;
        }
        if (strcmp (argv[i], "--validate") == 0)    {
            validateMode = true;
            i += 1;
            continue;
        }
        if (argc <= (i+1))  // remaining options below all require a value
            break;
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
        if (strcmp (argv[i], "-L") == 0)    {
            if (!logSetLevelFromString (argv[i + 1]))
                logError ("Unrecognized -L level %s (expected error|warning|info|debug)\n", argv[i + 1]);
            i += 2;
            continue;
        }
        if (strcmp (argv[i], "-V") == 0)    {
            char vbuf[8];
            snprintf (vbuf, sizeof (vbuf), "%s", argv[i + 1]);
            upcase_str (vbuf);
            if (strcmp (vbuf, "NS") == 0)
                defaultVul = NSVul;
            else if (strcmp (vbuf, "EW") == 0)
                defaultVul = EWVul;
            else if (strcmp (vbuf, "BOTH") == 0)
                defaultVul = BothVul;
            else if (strcmp (vbuf, "BNO") == 0)
                vulFromBoard = true;
            else
                defaultVul = NoneVul;   // "None", or anything unrecognized
            i += 2;
            continue;
        }
    }
    if (chdir (path) != 0)  {
        logError ("failed to change directory to %s\n", path);
        perror ("chdir");
        exit (1);
    }
    if (!validateMode)  {
        oh = fopen (output, "w");
        if (oh == NULL)   {
            logError ("Failed to open output%s", output);
            perror ("fopen");
            exit (1);
        }
        bboFp = fopen (bboFile, "w");
        if (bboFp == NULL)   {
            logError ("Failed to open %s", bboFile);
            perror ("fopen");
            exit (1);
        }
    }
    if (*pbnFile)   {
        pbnh = fopen (pbnFile, "r");
        if (pbnh == NULL)   {
            logError ("Failed to open %s", pbnFile);
            perror ("fopen");
            exit (1);
        }
    }
    if (!validateMode && *detailFile)    {
        detailh = fopen (detailFile, "w");
        if (detailh == NULL)   {
            logError ("Failed to open %s", detailFile);
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
            logError ("failed to build system\n");
            exit (1);
        }
        s++;
        *snpp++ = inFile;
        inFile = fileEnd ? fileEnd + 1 : NULL;
    } while (fileEnd != NULL);
    assert (s == (systemsList + numSystems));

    if (validateMode)   {
        for (int vi = 0; vi < numSystems; vi++)
            validateSystem (systemsList[vi], systemNames[vi]);
        return 0;
    }

    impSum = new int[numSystems]();
    parMatches = new int[numSystems]();

    if (partnerRuleName) {
        char prBuf[MAXNAMELEN];
        snprintf (prBuf, sizeof (prBuf), "$%s", partnerRuleName);
        upcase_str (prBuf + 1);
        partnerRule = systemsList[0]->findRule (prBuf);
        if (partnerRule == NULL) {
            logError ("Could not find partner rule %s\n", partnerRuleName);
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

	logInfo ("Starting deal generation\n");
    auction::writeHeaders (systemNames);
    time_t t0 = time(0);
    int repsDone = 0;
    time_t t1;
    int snum;
    enum { reportInterval = 100 };   // print elapsed/remaining time every N boards
	if (pbnh == NULL)   {
        while (reps > 0)    {
            if (detailh) {
                fflush (detailh);
                fprintf (detailh, "Processing deal %d,,%d\n", repsDone, randCalls);
            }
            auction a (rulep);
            (void)a.createDeal (NULL);
            boardsScored++;
            for (snum = 0; snum < numSystems; snum++)
                a.bidHand (systemsList[snum], snum);
            t1 = time (0) - t0;
            --reps;
            ++repsDone;
            if ((repsDone % reportInterval) == 0)
                print_time_estimate (t1, t1 * reps / repsDone);
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
                boardsScored++;
                for (snum = 0; snum < numSystems; snum++)
                    a.bidHand (systemsList[snum], snum);
                t1 = time (0) - t0;
                ++repsDone;
                if ((repsDone % reportInterval) == 0)
                    print_time_estimate (t1, t1 / repsDone);
                fprintf (oh, "\n");
                fflush (oh);
            }
            if (++done == 10) {
                FORCE_DISK_WRITE(fileno(oh));
                done = 0;
            }
        }
	}
    logInfo ("bidHand took %lld seconds\n", (long long)(time(0) - t0));
    if (!rulesOnlyMode)
        auction::writeSummary (systemNames);
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
