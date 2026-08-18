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
#include <vector>
#include <unordered_set>
#include <string>
#include "tnode.h"
#include "consts.h"
#include "pack.hpp"
#include "translations.h"
#include "parse_rules.h"
#include "trumpAskExpand.hpp"
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
static bool selfTestMode  = false;   // --self-test: check combineRule()/negateRule()'s own NULL handling, no rules file needed
static bool statsMode     = false;   // --stats: print runtime (dynamic) stats gathered during a normal run -- static/structural stats print unconditionally with --validate instead

// --nrules N[,N2,...]: cap how many rule-decisions (nextBid() calls that
// found/could-find a matching rule) fire before forcing a suggestContract()
// guess, so a run can compare bidding depth vs. guess quality on the same
// deals. -1 means "unlimited" (today's default behavior). Each value in the
// list produces its own set of output columns per system -- see main()'s
// "runs" (system x nrules-value pairs) below.
static std::vector<int> nrulesList;

static void
parseNrulesList (const char* arg)
{
    char buf[LINE_LENGTH];
    snprintf (buf, sizeof (buf), "%s", arg);
    char* tok = strtok (buf, ",");
    while (tok != NULL)    {
        char* end;
        long v = strtol (tok, &end, 10);
        if ((end == tok) || (*end != 0) || (v < 0))    {
            logError ("Invalid --nrules value '%s' (expected a non-negative integer)\n", tok);
            exit (1);
        }
        nrulesList.push_back ((int)v);
        tok = strtok (NULL, ",");
    }
}

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
    // Precomputed by precomputeSiblingNegations(), once, after the whole
    // tree is built -- see its own comment for why this can't be done
    // incrementally during tree construction. negatedEarlierSiblings is
    // this node's own contribution when findMatchingChild() selects it (AND
    // of NOT(rule) for every earlier sibling under the same parent, skipping
    // waypoints); negatedAllChildren is a parent's contribution when NONE of
    // its children match (AND of NOT(rule) for all of them). NULL until
    // precomputeSiblingNegations() runs, same as any other "no constraint".
    void*       negatedEarlierSiblings;
    void*       negatedAllChildren;
    convention (convention* parent, bid inb, void* r);
    convention* firstChild()    { return child; }
    convention* nextSibling()   { return sibling; }
    bid         thisBid()       { return b; }
    void*       getRule()       { return rule; }
};

convention::convention (convention* parent, bid inb, void* r)
    : b(inb), child(NULL), sibling(NULL), rule(r),
      negatedEarlierSiblings(NULL), negatedAllChildren(NULL)
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

// Precomputes negatedEarlierSiblings/negatedAllChildren (see convention's
// own comment) for every node in the tree rooted at `node`, once, so
// findMatchingChild() never has to rebuild the same negation from scratch
// on every deal that reaches the same decision point -- see the "Precompute
// negative-inference" work item for the full rationale and the measured
// cost this removes.
//
// Must run as ONE pass over the *fully-built* tree -- after every rule in
// the file has been processed, not incrementally as each node is inserted.
// biddingSystem::processRule() can create a node as a waypoint (rule==NULL,
// existing only as a path prefix for a deeper rule) while processing one
// rule, and a *later* rule in the same file can retroactively set that same
// node's `rule` via direct field assignment if its own bid sequence happens
// to terminate there (e.g. "$.1N.2C.2H." processed before "$.1N.2C.", both
// sharing the "1N.2C" node). Computing a sibling's negation at insertion
// time would capture that node's rule as it stood *then* -- NULL, skipped
// -- not its final value. Running this only after the whole file is parsed
// means every node's rule is already settled, so no such staleness is
// possible.
//
// Called once per system, from biddingSystem's constructor, before any
// deal is dealt and before the per-deal tnodeArenaBegin()/End() bracket
// exists yet -- so the make_leaf() calls inside combineRule()/negateRule()
// here fall back to plain malloc() (arena inactive), making every node this
// builds permanent/process-lifetime for free, exactly like the rest of the
// parsed rule tree. Deliberately eager rather than lazy/memoized-on-first-
// use for this reason: computing it lazily would happen from inside the
// per-deal loop, where the arena *is* active, and would need a new
// mechanism to force permanent allocation there instead of reusing this
// already-safe window.
static void
precomputeSiblingNegations (convention* node)
{
    void* acc = NULL;
    for (convention* c = node->firstChild (); c != NULL; c = c->nextSibling ())    {
        c->negatedEarlierSiblings = acc;
        if (c->getRule () != NULL)
            acc = combineRule (acc, negateRule (c->getRule ()));
        precomputeSiblingNegations (c);
    }
    node->negatedAllChildren = acc;
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
// The negation itself is not rebuilt here -- negatedEarlierSiblings/
// negatedAllChildren are precomputed once per tree by
// precomputeSiblingNegations() (see its own comment), since which siblings
// get negated for a given match is entirely a function of tree structure,
// never of the specific hand. This turns what used to be up to K
// combineRule()/negateRule() calls (K = position of the match among its
// siblings, redone from scratch on every deal that reaches this decision)
// into exactly one.
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
            return { s, combineRule (acc, s->negatedEarlierSiblings) };
    }
    acc = combineRule (acc, node->negatedAllChildren);
    if (allPassSoFar && (node != rootSysp))    {
        for (convention* s = rootSysp->firstChild (); s != NULL; s = s->nextSibling ()) {
            void* rule = s->getRule ();
            if (rule == NULL)
                continue;
            if (hand.checkHand (rule))
                return { s, combineRule (acc, s->negatedEarlierSiblings) };
        }
        acc = combineRule (acc, rootSysp->negatedAllChildren);
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
    // Snapshot of unusedAskTemplateNames() taken right after this system's
    // own read_rules() call -- graftAskTemplates() reports are keyed to
    // "the file most recently processed", so with multiple -i systems this
    // must be captured per-system at load time, not re-read later from
    // validateSystem() (by then every system has already loaded, and the
    // global would only reflect the last one).
    std::vector<std::string> unusedTemplates;
  public:
    biddingSystem (char* fileName);
    bool isValid()                  { return (ruleList != NULL); }
    void* findRule (const char* ruleName) { return find_rule (ruleList, ruleName); }
    void processRule (void* rule);
    int  countHandPropertyRules ();
    const std::vector<std::string>& getUnusedTemplates ()  { return unusedTemplates; }
};

// Hand-property ("$Name := ...", not "$.<bid>...") rules -- e.g. $balanced,
// $ntop -- aren't part of the convention tree at all (processRule() skips
// them), so they need their own pass over the flat definition list rather
// than falling out of walkValidate()'s tree walk. Deduplicated by name so a
// redefined rule (see hand-spec.md's "Redefinition") counts once, matching
// what's actually live, not how many "$Name := ..." occurrences are in the
// file.
int
biddingSystem::countHandPropertyRules ()
{
    std::unordered_set<std::string> names;
    for (void* rule = ruleList; rule != NULL; rule = next_rule (rule))    {
        char* nm = rule_name (rule);
        if ((nm[0] == '$') && (nm[1] == '.'))
            continue;   // bid-sequence rule -- counted by walkValidate() instead
        names.insert (nm);
    }
    return (int)names.size ();
}

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
    unusedTemplates = unusedAskTemplateNames ();
    if (ruleList != NULL)   {
        for (void* rule = ruleList; rule !=NULL; rule = next_rule (rule))
            processRule (rule);
        precomputeSiblingNegations (this);   // whole tree now built -- see its own comment for why not sooner
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

// Rule-coverage tracking for --stats: matchedByRound[sysIndex][round] / same
// shape for guessedByRound, round 1 = opening bid (same numbering as
// validateSystem()'s bidsByDepth). Gathered unconditionally -- the
// bookkeeping is a few integer increments per bid, negligible next to a
// DDS solve -- only the printing is gated on statsMode. Declared ahead of
// auction::nextBid() (which populates it) and printRuleCoverageTable()
// (which reports it, much further down).
static std::vector<std::vector<int>> matchedByRound;
static std::vector<std::vector<int>> guessedByRound;

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
    int         roundNum;          // for --stats: which bid nextBid() is deciding, 1 = opening bid
    int         curSysIndex;       // for --stats: which system this bidHand() call is for
    int         nrulesLimit;       // --nrules: force a guess once roundNum exceeds this (-1 = unlimited)
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
    void    bidHand (systemp sp, int sysIndex, int nrulesLimit);
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
    roundNum = 0;
}

void
auction::bidHand (systemp sp, int sysIndex, int nrulesLimitArg)
{
    initializeBidding ();
    sysp = sp;
    rootSysp = sp;
    curSysIndex = sysIndex;
    nrulesLimit = nrulesLimitArg;
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
        roundNum++;
        matchResult mr;
        convention* s;
        if ((nrulesLimit >= 0) && (roundNum > nrulesLimit))
            s = NULL;   // --nrules cutoff reached: force a guess even if a rule would have matched
        else    {
            mr = findMatchingChild (sysp, rootSysp, allPassSoFar, hands[bidder], rules[bidder]);
            s = mr.match;
        }
        // Rule-coverage bookkeeping for --stats (see printRuleCoverageTable()):
        // grow this system's per-round vector as needed, then tally which
        // branch below decided this bid -- rule match or suggestContract()
        // guess. Always gathered; only the report is gated on statsMode.
        std::vector<int>& matched = matchedByRound[curSysIndex];
        std::vector<int>& guessed = guessedByRound[curSysIndex];
        if ((size_t)roundNum >= matched.size ())   {
            matched.resize (roundNum + 1, 0);
            guessed.resize (roundNum + 1, 0);
        }
        if (s == NULL)  {   // No matching rule found. Take a guess at best contract
            guessed[roundNum]++;
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
            matched[roundNum]++;
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
static int numRuns;    // numSystems * nrulesList.size() -- one output column-group per (system, nrules value) pair
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
    for (systemNamep* snpp = systemNames; snpp < systemNames + numRuns; snpp++)   {
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
    for (int sysIndex = 0; sysIndex < numRuns; sysIndex++, snpp++)   {
        double avgImps = boardsScored ? ((double)impSum[sysIndex] / boardsScored) : 0.0;
        double parPct  = boardsScored ? (100.0 * parMatches[sysIndex] / boardsScored) : 0.0;
        logInfo ("System %s: average IMPs vs par = %.2f, bid par contract %d/%d (%.1f%%)\n",
                *snpp, avgImps, parMatches[sysIndex], boardsScored, parPct);
    }
}

// Round names for the first few bid-sequence depths (1 = opening bid).
// Shared between printStatsTable()'s static bidsByDepth report and
// printRuleCoverageTable()'s dynamic one below, so the two use identical
// round labels. Beyond NAMED_ROUNDS, printStatsTable() lumps everything
// into one "Round N+" bucket (rare enough not to name individually), but
// printRuleCoverageTable() prints each deeper round on its own row instead
// -- see the comment there for why.
static const char* const roundNames[] = {
    NULL,                              // depth 0: root, not a real round
    "Opening bids (North)",            // depth 1
    "Responses (South)",               // depth 2
    "Opener's rebid (North)",          // depth 3
    "Responder's rebid (South)",       // depth 4
    "Opener's 2nd rebid (North)",      // depth 5
    "Responder's 2nd rebid (South)",   // depth 6
};
enum { NAMED_ROUNDS = 6 };

struct TableRow {
    std::string label;
    std::vector<std::string> values;   // pre-formatted, one per system, same order as systemNames
};

// Shared by printStatsTable() (--validate's static structure/summary) and
// printRuleCoverageTable() (--stats's dynamic rule coverage): one row per
// stat, one column per system, column width derived from each system's
// name and its widest formatted value in that column.
static void
printTable (const char* title, systemNamep* systemNames, int n, const std::vector<TableRow>& rows)
{
    size_t labelWidth = 0;
    for (const TableRow& r : rows)
        if (r.label.size () > labelWidth)
            labelWidth = r.label.size ();

    std::vector<size_t> colWidth (n);
    for (int s = 0; s < n; s++)   {
        colWidth[s] = strlen (systemNames[s]);
        for (const TableRow& r : rows)
            if (r.values[s].size () > colWidth[s])
                colWidth[s] = r.values[s].size ();
    }

    std::string line (labelWidth, ' ');
    for (int s = 0; s < n; s++)   {
        char buf[LINE_LENGTH];
        snprintf (buf, sizeof (buf), "  %*s", (int)colWidth[s], systemNames[s]);
        line += buf;
    }
    logInfo ("%s\n", title);
    logInfo ("%s\n", line.c_str ());

    for (const TableRow& r : rows)   {
        char labelBuf[LINE_LENGTH];
        snprintf (labelBuf, sizeof (labelBuf), "%-*s", (int)labelWidth, r.label.c_str ());
        line = labelBuf;
        for (int s = 0; s < n; s++)   {
            char buf[LINE_LENGTH];
            snprintf (buf, sizeof (buf), "  %*s", (int)colWidth[s], r.values[s].c_str ());
            line += buf;
        }
        logInfo ("%s\n", line.c_str ());
    }
}

static std::string
fmtLL (long long v)
{
    char buf[32];
    snprintf (buf, sizeof (buf), "%lld", v);
    return buf;
}

static std::string
fmtCoverage (int matched, int total)
{
    if (total == 0)
        return "-";   // this round was never reached at all for this system
    char buf[64];
    snprintf (buf, sizeof (buf), "%d/%d (%.1f%%)", matched, total, 100.0 * matched / total);
    return buf;
}

// --stats: rule coverage in practice -- how often nextBid() found a
// matching rule vs. had to fall through to suggestContract()'s guess,
// broken down by round with the same labels validateSystem() uses for its
// static bidsByDepth report. This is the real, empirical counterpart to
// that report's GAP percentage (a sampling-based prediction of the same
// thing) -- ground truth from the deals actually processed, vs. an
// estimate from 3000 random hands per decision point. One column per
// system, like printStatsTable() -- but unlike that table, rounds beyond
// NAMED_ROUNDS are NOT lumped into a single "Round 7+" bucket here: losing
// which specific deep round is under-covered would defeat the point of a
// per-round diagnostic.
static void
printRuleCoverageTable (systemNamep* systemNames)
{
    int n = numRuns;
    size_t maxRounds = 0;
    for (int s = 0; s < n; s++)
        if (matchedByRound[s].size () > maxRounds)
            maxRounds = matchedByRound[s].size ();

    std::vector<TableRow> rows;
    std::vector<long long> totalMatched (n, 0), totalAll (n, 0);
    for (size_t d = 1; d < maxRounds; d++)   {
        TableRow row;
        char label[MAXNAMELEN];
        if (d <= NAMED_ROUNDS)
            snprintf (label, sizeof (label), "%s", roundNames[d]);
        else
            snprintf (label, sizeof (label), "Round %zu (%s)", d, (d % 2) ? "North" : "South");
        row.label = label;
        row.values.resize (n);
        bool anyReached = false;
        for (int s = 0; s < n; s++)   {
            int m = ((size_t)d < matchedByRound[s].size ()) ? matchedByRound[s][d] : 0;
            int g = ((size_t)d < guessedByRound[s].size ()) ? guessedByRound[s][d] : 0;
            row.values[s] = fmtCoverage (m, m + g);
            if ((m + g) > 0)
                anyReached = true;
            totalMatched[s] += m;
            totalAll[s] += (m + g);
        }
        if (anyReached)
            rows.push_back (row);
    }

    TableRow overallRow;
    overallRow.label = "Overall";
    overallRow.values.resize (n);
    for (int s = 0; s < n; s++)
        overallRow.values[s] = fmtCoverage ((int)totalMatched[s], (int)totalAll[s]);
    rows.push_back (overallRow);

    printTable ("System rule coverage (--stats):", systemNames, n, rows);
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
    // Structural/size stats -- always printed with --validate, no flag.
    // bidsByDepth[d] = number of "$."-sequence rules defined at depth d
    // (1 = opening bid, 2 = response, ...); index 0 unused. Grown lazily
    // as walkValidate() encounters deeper nodes.
    std::vector<int> bidsByDepth;
    int waypoints;   // depth>0 nodes with no rule of their own (pure path prefixes)
    int maxDepth;    // deepest node visited, whether or not it has a rule
    int handPropertyRules;   // set separately, after the walk -- see validateSystem()
    int unusedTemplates;     // ask-templates declared ($.?.Name....) but never attached (:?)
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
        // so a NULL rule here (a waypoint) is common, not exceptional --
        // combineRule() itself now treats NULL as "no additional
        // constraint" on either side, so this needs no special handling.
        if (depth % 2 == 1)   // odd depth: North's own bid/rule
            northAcc = combineRule (northAcc, node->getRule ());
        else                  // even depth: South's own bid/rule
            southAcc = combineRule (southAcc, node->getRule ());

        // Structural/size bookkeeping -- always gathered, printed
        // unconditionally by validateSystem() (no separate flag).
        if (depth > stats->maxDepth)
            stats->maxDepth = depth;
        if (node->getRule () != NULL)   {
            if ((size_t)depth >= stats->bidsByDepth.size ())
                stats->bidsByDepth.resize (depth + 1, 0);
            stats->bidsByDepth[depth]++;
        } else
            stats->waypoints++;
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
            siblingAcc = combineRule (siblingAcc, negateRule (rule));
    }
}

// Runs the offline tree walk for one system, logging findings (OVERLAP/GAP/
// UNREACHABLE/DUPLICATE) as they're found -- those are inherently per-
// decision-point messages, not single numbers, so they don't fit into the
// cross-system comparison table printStatsTable() prints afterward. The
// structural/summary numbers this used to print inline are returned
// instead, so main() can collect all systems' stats before laying out that
// table -- one column per system, printed only once every system in this
// -i list has been walked.
static validateStats
validateSystem (biddingSystem* sys, const char* sysName)
{
    validateStats stats = {};
    void* anyRule = sys->findRule ("$ANY");   // mirrors bidHand()'s initial rules[bidder] reset
    logInfo ("Validating system %s...\n", sysName);
    walkValidate (sys, sys, 0, anyRule, anyRule, true, "", &stats);
    stats.handPropertyRules = sys->countHandPropertyRules ();
    for (const std::string& name : sys->getUnusedTemplates ())   {
        logWarning ("[validate] UNUSED-TEMPLATE: %s declared (\"$.?.%s....\") but never attached (\":?\")\n",
                    name.c_str (), name.c_str ());
        stats.unusedTemplates++;
    }
    return stats;
}

// One row per stat, one column per system -- the structural/size numbers
// validateSystem() used to print inline per system, laid out for
// side-by-side comparison across every -i system instead. A named-round
// row is included only if at least one system actually has bids at that
// depth, matching the old inline behavior of skipping all-zero rows.
static void
printStatsTable (systemNamep* systemNames, const std::vector<validateStats>& allStats)
{
    int n = (int)allStats.size ();
    std::vector<TableRow> rows;

    for (int d = 1; d <= NAMED_ROUNDS; d++)   {
        std::vector<long long> raw (n, 0);
        bool anyNonzero = false;
        for (int s = 0; s < n; s++)   {
            if ((size_t)d < allStats[s].bidsByDepth.size ())
                raw[s] = allStats[s].bidsByDepth[d];
            if (raw[s] != 0)
                anyNonzero = true;
        }
        if (anyNonzero)   {
            TableRow row;
            row.label = roundNames[d];
            for (int s = 0; s < n; s++)
                row.values.push_back (fmtLL (raw[s]));
            rows.push_back (row);
        }
    }

    {
        std::vector<long long> raw (n, 0);
        bool anyOverflow = false;
        for (int s = 0; s < n; s++)   {
            for (size_t d = NAMED_ROUNDS + 1; d < allStats[s].bidsByDepth.size (); d++)
                raw[s] += allStats[s].bidsByDepth[d];
            if (raw[s] != 0)
                anyOverflow = true;
        }
        if (anyOverflow)   {
            TableRow row;
            row.label = "Round 7+ (North/South)";
            for (int s = 0; s < n; s++)
                row.values.push_back (fmtLL (raw[s]));
            rows.push_back (row);
        }
    }

    TableRow totalRow;
    totalRow.label = "Total bid-sequence rules";
    for (int s = 0; s < n; s++)   {
        long long total = 0;
        for (size_t d = 1; d < allStats[s].bidsByDepth.size (); d++)
            total += allStats[s].bidsByDepth[d];
        totalRow.values.push_back (fmtLL (total));
    }
    rows.push_back (totalRow);

    struct { const char* label; int validateStats::*field; } simpleFields[] = {
        { "Hand-property rules", &validateStats::handPropertyRules },
        { "Pure path waypoints", &validateStats::waypoints },
        { "Max auction depth",   &validateStats::maxDepth },
        { "Decision points",     &validateStats::decisionPoints },
        { "Overlap",             &validateStats::overlaps },
        { "Gap",                 &validateStats::gaps },
        { "Unreachable",         &validateStats::unreachable },
        { "Duplicate-text",      &validateStats::duplicates },
        { "Unused templates",    &validateStats::unusedTemplates },
    };
    for (auto& f : simpleFields)   {
        TableRow row;
        row.label = f.label;
        for (int s = 0; s < n; s++)
            row.values.push_back (fmtLL (allStats[s].*(f.field)));
        rows.push_back (row);
    }

    printTable ("System structure & validation summary:", systemNames, n, rows);
}

static char inFileList[PATH_MAX];
static char path[PATH_MAX];
static char output[PATH_MAX];
static char pbnFile[PATH_MAX];
static char detailFile[PATH_MAX];

// --self-test: exercises combineRule()/negateRule()'s NULL handling
// directly. Needed because, with $ANY now always defined (see read_rules()),
// there's no longer any live code path that actually calls these with a
// NULL operand -- the load-bearing behavior would otherwise go completely
// unexercised by every other test in the suite. No rules file, no dealing.
static bool
runSelfTest ()
{
    bool ok = true;
    void* realRule = make_leaf (TINT, 1);   // stand-in for "some real rule"
    aHand dummyHand;                        // never dealt; TINT leaves don't look at it

    if (combineRule (NULL, realRule) != realRule)    {
        logError ("[self-test] FAILED: combineRule(NULL, r) should return r unchanged\n");
        ok = false;
    }
    if (combineRule (realRule, NULL) != realRule)    {
        logError ("[self-test] FAILED: combineRule(r, NULL) should return r unchanged\n");
        ok = false;
    }
    if (combineRule (NULL, NULL) != NULL)    {
        logError ("[self-test] FAILED: combineRule(NULL, NULL) should return NULL\n");
        ok = false;
    }
    void* alwaysFalse = negateRule (NULL);
    if (alwaysFalse == NULL)    {
        logError ("[self-test] FAILED: negateRule(NULL) should return a real leaf, not NULL\n");
        ok = false;
    } else if (dummyHand.checkHand (alwaysFalse) != false)    {
        logError ("[self-test] FAILED: negateRule(NULL) should evaluate to false (NOT(true))\n");
        ok = false;
    }
    if (dummyHand.checkHand (combineRule (NULL, alwaysFalse)) != false)    {
        logError ("[self-test] FAILED: combineRule(NULL, negateRule(NULL)) should evaluate to false\n");
        ok = false;
    }

    if (ok)
        logInfo ("[self-test] PASSED\n");
    return ok;
}

// ---- simplify() checks, exercised via --self-test ----
//
// Built directly with make_leaf()/match_string() rather than parsed from a
// rules file, matching --self-test's existing "no rules file needed"
// design. Every tree is combined with a filler TINT(1) via combineRule()
// (rather than calling simplifyRule() directly), so this exercises the
// exact same path production code goes through -- combineRule() is the
// sole hook (see tnode.cpp) -- not a separate, possibly-diverging entry
// point. TINT(1) is the AND identity element, so it never affects the
// result (see simplifyAnd()'s isTrueLeaf() filtering) -- it exists only so
// both operands are non-NULL, since combineRule(NULL, x) short-circuits to
// x unchanged without invoking simplify at all.
//
// Two things worth knowing about the exact expected strings below (see
// hand-spec.md's "Rule Simplification"): TGT/TLT are always canonicalized
// to TGEQ/TLEQ with an adjusted boundary (Points > 14 -> Points >= 15),
// even when nothing else about the clause changes -- so "unchanged" only
// ever means structurally/semantically unchanged, never guaranteed
// byte-identical to the input. And rebuilt AND/OR operand order follows
// unordered_map iteration order, not input source order -- both confirmed
// against this build's actual output before being written as expected
// strings here, not hand-derived and assumed correct.
static TPTR
atom (const char* name)
{
    char buf[64];
    snprintf (buf, sizeof (buf), "%s", name);
    return match_string (buf);
}
static TPTR
cmp (nodeType t, TPTR left, long long val)
{
    TPTR p = make_leaf (t, t);
    return add_leaves (p, left, make_leaf (TINT, val));
}
static TPTR
band (TPTR l, TPTR r) { TPTR p = make_leaf (TAND, TAND); return add_leaves (p, l, r); }
static TPTR
bor (TPTR l, TPTR r) { TPTR p = make_leaf (TOR, TOR); return add_leaves (p, l, r); }

static bool
checkSimplify (const char* label, void* result, const char* expected, bool* ok)
{
    const char* got = result ? ((TPTR)result)->t_desc : "(NULL)";
    if (strcmp (got, expected) != 0)   {
        logError ("[self-test] FAILED: simplify %s: expected \"%s\", got \"%s\"\n", label, expected, got);
        *ok = false;
        return false;
    }
    return true;
}

static bool
runSimplifyTests ()
{
    bool ok = true;
    TPTR one = make_leaf (TINT, 1);   // AND-identity filler; see comment above

    // (10 TO 18 Points) AND (Points > 15) -> the two upper/lower bounds on
    // Points merge into one tightened range.
    TPTR ex1 = band (band (cmp (TGEQ, atom ("POINTS"), 10), cmp (TLEQ, atom ("POINTS"), 18)),
                      cmp (TGT, atom ("POINTS"), 15));
    checkSimplify ("ex1 (interval merge)", combineRule (one, ex1),
                   "((POINTS >= 16) && (POINTS <= 18))", &ok);

    // (10 TO 18 Points) AND (((Points > 14) AND (Spades ?= 4)) OR (Spades > 4))
    // -- Points>14 is NOT fully implied by 10 TO 18 alone (a 12-point hand
    // satisfies the outer range but not this clause), so nothing is
    // dropped; this is the "sound no-op" case (mod GT->GEQ).
    TPTR ex2branch1 = band (cmp (TGT, atom ("POINTS"), 14), cmp (TEQU, atom ("SPADES"), 4));
    TPTR ex2branch2 = cmp (TGT, atom ("SPADES"), 4);
    TPTR ex2 = band (band (cmp (TGEQ, atom ("POINTS"), 10), cmp (TLEQ, atom ("POINTS"), 18)),
                      bor (ex2branch1, ex2branch2));
    checkSimplify ("ex2 (OR branch not redundant)", combineRule (one, ex2),
                   "(((POINTS >= 10) && (POINTS <= 18)) && (((SPADES ?= 4) && (POINTS >= 15)) || (SPADES >= 5)))", &ok);

    // Same shape, but Points>5 IS fully implied by 10 TO 18 Points (context
    // already guarantees >=10) -- that clause is dropped from the branch.
    TPTR ex3branch1 = band (cmp (TGT, atom ("POINTS"), 5), cmp (TEQU, atom ("SPADES"), 4));
    TPTR ex3branch2 = cmp (TGT, atom ("SPADES"), 4);
    TPTR ex3 = band (band (cmp (TGEQ, atom ("POINTS"), 10), cmp (TLEQ, atom ("POINTS"), 18)),
                      bor (ex3branch1, ex3branch2));
    checkSimplify ("ex3 (OR branch redundant clause dropped)", combineRule (one, ex3),
                   "(((POINTS >= 10) && (POINTS <= 18)) && ((SPADES ?= 4) || (SPADES >= 5)))", &ok);

    // NOT(Hearts >= 4) -- as negateRule() produces it, a raw TNOT -- folds
    // to a directly-usable Hearts < 4 via De Morgan/comparison negation.
    TPTR ex4 = band (cmp (TGEQ, atom ("SPADES"), 4), (TPTR)negateRule (cmp (TGEQ, atom ("HEARTS"), 4)));
    checkSimplify ("ex4 (NOT-pushdown on negateRule() output)", combineRule (one, ex4),
                   "((SPADES >= 4) && (HEARTS <= 3))", &ok);

    // Hearts < 4 AND Hearts >= 4 -- a real contradiction, caught exactly
    // (empty interval), not just approximated by sampling like --validate's
    // UNREACHABLE does. Must come back non-NULL (NULL means "always true"
    // everywhere else in this codebase, so "always false" needs a real leaf).
    TPTR ex5 = band ((TPTR)negateRule (cmp (TGEQ, atom ("HEARTS"), 4)), cmp (TGEQ, atom ("HEARTS"), 4));
    void* ex5r = combineRule (one, ex5);
    if (ex5r == NULL)   {
        logError ("[self-test] FAILED: simplify ex5 (contradiction): expected a real false-leaf, got NULL\n");
        ok = false;
    } else
        checkSimplify ("ex5 (contradiction)", ex5r, "0", &ok);

    // (Sl >= 4) AND (Slen <= 6) -- Sl/Slen are two spellings of the same
    // underlying function (suit_len); should fold into one range despite
    // never sharing exact keyword text.
    TPTR ex6 = band (cmp (TGEQ, atom ("SL"), 4), cmp (TLEQ, atom ("SLEN"), 6));
    checkSimplify ("ex6 (Sl/Slen alias collapsing)", combineRule (one, ex6),
                   "((SL >= 4) && (SL <= 6))", &ok);

    // (NLTC <= 16) AND (NLTC >= 10) -- NLTC is just another TKWORD entry;
    // needs zero special-casing to fold the same way Points does.
    TPTR ex7 = band (cmp (TLEQ, atom ("NLTC"), 16), cmp (TGEQ, atom ("NLTC"), 10));
    checkSimplify ("ex7 (NLTC, no special-casing)", combineRule (one, ex7),
                   "((NLTC >= 10) && (NLTC <= 16))", &ok);

    if (ok)
        logInfo ("[self-test] simplify checks PASSED\n");
    return ok;
}

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
        if (strcmp (argv[i], "--self-test") == 0)    {
            selfTestMode = true;
            i += 1;
            continue;
        }
        if (strcmp (argv[i], "--stats") == 0)    {
            statsMode = true;
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
        if (strcmp (argv[i], "--nrules") == 0)    {
            parseNrulesList (argv[i + 1]);
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
    if (nrulesList.empty ())
        nrulesList.push_back (-1);   // no --nrules given: unlimited, single run (today's behavior)
    if (selfTestMode)   {
        bool ok = runSelfTest ();
        ok = runSimplifyTests () && ok;
        return ok ? 0 : 1;
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
        std::vector<validateStats> allStats;
        for (int vi = 0; vi < numSystems; vi++)
            allStats.push_back (validateSystem (systemsList[vi], systemNames[vi]));
        printStatsTable (systemNames, allStats);
        return 0;
    }

    // Build the flat list of "runs" -- one per (system, --nrules value) pair,
    // system-major so a single system's nrules progression reads left to
    // right in the output. With a single (default, unlimited) --nrules
    // value this collapses to exactly one run per system, same as before
    // --nrules existed, so column names are left unchanged in that case.
    numRuns = numSystems * (int)nrulesList.size ();
    bool multiNrules = nrulesList.size () > 1;
    std::vector<std::string> runNameStrings (numRuns);
    std::vector<int> runSysIndex (numRuns);
    std::vector<int> runNrulesLimit (numRuns);
    {
        int idx = 0;
        for (int sysI = 0; sysI < numSystems; sysI++)
            for (size_t ni = 0; ni < nrulesList.size (); ni++, idx++)   {
                runSysIndex[idx] = sysI;
                runNrulesLimit[idx] = nrulesList[ni];
                if (multiNrules)   {
                    char buf[MAXNAMELEN];
                    if (nrulesList[ni] < 0)
                        snprintf (buf, sizeof (buf), "%s (nrules=all)", systemNames[sysI]);
                    else
                        snprintf (buf, sizeof (buf), "%s (nrules=%d)", systemNames[sysI], nrulesList[ni]);
                    runNameStrings[idx] = buf;
                } else
                    runNameStrings[idx] = systemNames[sysI];
            }
    }
    systemNamep* runNames = new systemNamep[numRuns];
    for (int k = 0; k < numRuns; k++)
        runNames[k] = const_cast<char*> (runNameStrings[k].c_str ());

    impSum = new int[numRuns]();
    parMatches = new int[numRuns]();
    matchedByRound.resize (numRuns);
    guessedByRound.resize (numRuns);

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
    auction::writeHeaders (runNames);
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
            // Bracket one hand's worth of accumulator-building (all
            // numSystems bid the same deal) so make_leaf() allocates from
            // the transient-node arena instead of malloc() -- see
            // tnode.cpp's "Transient-node arena" comment. Safe: bidHand()
            // resets rules[bidder] to a fresh $ANY lookup at the start of
            // each system's turn (see its "reset to no info" comment), and
            // nothing built here is read after this rep ends.
            tnodeArenaBegin ();
            auction a (rulep);
            (void)a.createDeal (NULL);
            boardsScored++;
            for (snum = 0; snum < numRuns; snum++)
                a.bidHand (systemsList[runSysIndex[snum]], snum, runNrulesLimit[snum]);
            tnodeArenaEnd ();
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
            tnodeArenaBegin ();   // see the non-PBN loop above for why this is safe
            auction a (rulep);
            if (a.createDeal (pbnDeal))   {
                boardsScored++;
                for (snum = 0; snum < numRuns; snum++)
                    a.bidHand (systemsList[runSysIndex[snum]], snum, runNrulesLimit[snum]);
                tnodeArenaEnd ();
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
        auction::writeSummary (runNames);
    if (statsMode)
        printRuleCoverageTable (runNames);
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
