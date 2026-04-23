#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include "tnode.h"
#include "pack.hpp"
#include "ddsinfo.hpp"
#include "dealerDeal.hpp"
#include "csvparser.hpp"

extern "C"  {
    extern void printHandsFromPbn (char*, int, enum outputFormat);
    extern void openFiles (const char*);
}
#include "parse_rules.h"

static const char* filter = "00000";
static const char* handsFile;
static FILE* sdaFp;
static FILE* ddaFp;
static FILE* bboFp;
static int   bboBoardNum = 0;
static int declarer = 0;
static const int ruleNameLen = 64;
static vulnerabilityCodes currentVul = vulNone;
static enum outputFormat printStyle = normal;
static void* partnerRule = NULL;
static const char* partnerRuleName = NULL;

static void upcase_str (char* s) { for (; *s; s++) *s = toupper((unsigned char)*s); }

static void
pexit (const char* msg)
{
    perror (msg);
    exit (errno);
}

// Processes a deal that matches rules for all hands
// Inputs: dp is ptr to the matching deal
// howManyToPrint is number of hands to write (0, 2 or 4)
// Creates a pbn format hand and writes it to next pbn rec in ddsinfo for dds analysis.
// From that, print the hands to northhands.txt, easthands.txt,
// southhands.txt, westhands.txt, and bothhands.txt or allhands.txt
// based on the value of howManyToPrint. If howManyToPrint is zero, prints only the pbnfile.
// If ddip is NULL, we just create a temp pbnLine and write to the files from that
// In this case, we are called from runSDA, so we just write to sdaFp
static void
processMatch (oneDeal* dp, ddsInfo* ddip, int howManyToPrint)
{
    char pbnLine[256];
    char* cptr = ddip ? ddip->nextPbn() : pbnLine;
    *cptr++ = 'N';
    *cptr++ = ':';
    (void) dp->makePBNrec (cptr);
    // Write to nh, eh, sh, wh, bh, and ah as applicable
    if (howManyToPrint > 0)
        printHandsFromPbn (cptr, howManyToPrint, printStyle);   // Skip "N:"
    if (ddip == NULL)   // Called from runSDA
        fprintf (sdaFp, "%s", cptr);
    if (bboFp && howManyToPrint == 4) {
        char linBuf[256];
        dp->makeLINrec (linBuf, ++bboBoardNum);
        fputs (linBuf, bboFp);
    }
}

#if 0

struct probabilityInfo	{
	char	name[MAXNAMELEN];
	void*	rule;
	int		counter;
	probabilityInfo* next;
	probabilityInfo (char* cp, void* r)
        : rule (NULL), counter (0), next (NULL)    { strncpy (name, cp, MAXNAMELEN - 1); name[MAXNAMELEN - 1] = 0; }
};

struct piHead   {
    probabilityInfo* first;
    probabilityInfo* last;
};

static piHead piList[4];
static FILE* casep;

static void
readCases (char* caseFile)
{
    casep = fopen (caseFile, "r");
    char caseList[LINE_LENGTH];
    for (int i = 0; i < NHANDS; i++)   {
        piHead* listp = piList + i;
        if (fgets (caseList, LINE_LENGTH, casep) == NULL)
            break;
        char* cp = caseList;
        char* cp2;
        do  {
            if ((cp2 = strchr (cp, ',')) != NULL)
                *cp2 = 0;
            void* r = find_rule (defroot, cp);
            if (r != NULL)  {
                probabilityInfo* pip = new probabilityInfo (cp, r);
                if (listp->last != NULL)
                    listp->last->next = pip;
                else
                    listp->first = pip;
                listp->last = pip;
            } else
                printf ("Invalid rule %s\n", cp);
            cp = cp2 + 1;
        } while (cp2 != NULL);
    }
    fclose(casep);
}

void
processCaseList(oneDeal* dp, int howManyToPrint)
{
    // Now process the caselist if any
    if (casep)  {
        for (int i = 0; i < howManyToPrint; i++) {
            probabilityInfo* pip = piList[i].first;
            do  {
                if (dp->checkHand (i, pip->rule))
                    pip->counter++;
                pip = pip->next;
            } while (pip != NULL);
        }
    }
}

#endif  // 0

// Open the SDA File and write the header line to it
static FILE*
setupSDAFile(char* sdaFile, char* line)
{
    // Output header
    FILE* fp = fopen(sdaFile, "w");
    if (!fp)
        pexit ("Could not open sdaFile");
    fprintf (fp, "%s,%s,%s,%s", line,
                                 "N pts,N spades,N hearts,N diamonds,N clubs,N shape",
                                 "S pts,S spades,S hearts,S diamonds,S clubs,S shape",                                 
                                 "Par,Par Ave,S Ave,H Ave,D Ave,C Ave,N Ave,Pass");
    for (int bidLvl = 1; bidLvl <= numLevels; bidLvl++)
        for (int strain = 0; strain < DDS_STRAINS; strain++)
            fprintf (fp, ",%d%c", bidLvl, suitAbbrv[strain]);
    fprintf (fp, "\n");
    return fp;
}

static FILE*
setupDDAFile (char* ddaFile)
{
    FILE* fp = fopen (ddaFile, "w");
    if (!fp)
        pexit ("Could not open ddaFile");
    if (fp != NULL)
        fprintf (fp, "Hands,%s%s%s%s%s\n",
                "N pts,N spades,N hearts,N diamonds,N clubs,",
                "E pts,E spades,E hearts,E diamonds,E clubs,",
                "S pts,S spades,S hearts,S diamonds,S clubs,",
                "W pts,W spades,W hearts,W diamonds,W clubs,",
                "S makes,H makes,D makes,C makes,N makes,NS Par,NS Score");
    return fp;
}


// Generate hands only, no DDA or SDA analysis
static int
generateDriver (int reps, const char* ruleNames[NHANDS], int nHandsToPrint)
{
    int repno;
    dealerDeal deal (ruleNames, partnerRule);
    char pbnLine[256];
    for (repno = 0; repno < reps; repno++) {
        if (!deal.dealAndCheck ())
            break;
        char* cptr = pbnLine;
        *cptr++ = 'N';
        *cptr++ = ':';
        (void) deal.makePBNrec (cptr);
        printHandsFromPbn (cptr, nHandsToPrint, printStyle);
        if (bboFp) {
            char linBuf[256];
            deal.makeLINrec (linBuf, ++bboBoardNum);
            fputs (linBuf, bboFp);
        }
    }
    return repno;
}

static int
generateRemaining (ddsInfo* ddip, int iters, dealerDeal* dealp)
{
    dealp->saveNS ();
    // Now generate the E-W hands
    int num;
    for (num = 0; num < iters; num++)   {   // Begin hand generation
        if (!dealp->dealAndCheck ())
            break;                          // Exceeded MAX_TRIES, give up
        processMatch (dealp, ddip, 0);      // Don't write hands for each player
    }
    return num;
}

static void
writeSDASummary (ddsInfo* ddip, contract* expertContract, contract* myContract)
{
    // Find Par contract and its average score
    ddip->setPars (currentVul);
    fprintf (sdaFp, ",%s,%.2f", ddip->parBid(), ddip->parAve());
    for (int j = 0; j < DDS_STRAINS; j++)
        fprintf (sdaFp, ",%.2f", ddip->aveTricks (j));

    // Print the score vs Par for each contract
    fprintf (sdaFp, ",%7.2f", ddip->impsVsPar (0, -1));
    for (int bidLvl = 1; bidLvl <= numLevels; bidLvl++)
        for (int strain = 0; strain < DDS_STRAINS; strain++)
            fprintf (sdaFp, ",%7.2f", ddip->impsVsPar (bidLvl, strain));

    // Evaluate IMPS vs Par for expertContract and myContract if asked
    if (expertContract && expertContract->isValid())   {
        fprintf (sdaFp, ",%.2f", ddip->aveImps (expertContract, NULL));
        if (myContract && myContract->isValid())   {
            fprintf (sdaFp, ",%.2f", ddip->aveImps (myContract, NULL));
            fprintf (sdaFp, ",%.2f", ddip->aveImps (myContract, expertContract));
        }
    }
    fprintf (sdaFp, "\n");
}

static bool
runSDA (const char* ruleNames[NHANDS], int iters, int /*vulCode*/, int nHandsToPrint,
        contract* expertContract = NULL, contract* myContract = NULL)
{
    dealerDeal deal (ruleNames, partnerRule);
    if (!deal.dealAndCheck())
        return false;   // Exceeded MAX_TRIES, give up
    processMatch (&deal, NULL, nHandsToPrint);
    (void) deal.writeSummaries (sdaFp); // Write N and S points, pattern, and shape
    ddsInfo* ddip = new ddsInfo(iters);
    if (ddip == NULL)
        return false;
    iters = generateRemaining (ddip, iters, &deal);
    (void) ddsMain (iters, ddip, ddaFp, filter, declarer);
    writeSDASummary (ddip, expertContract, myContract);

    delete ddip;
    return true;
}

static int EWiters = 128;

// Function to deal a pair of hands
static int
SDAdriver (int reps, const char* ruleNames[NHANDS], char* sdaFile)
{
    char line[LINE_LENGTH];
    printf ("Enter SDAdriver\n");
    sprintf (line, "%s %s %s %s", ruleNames[0], ruleNames[1], ruleNames[2], ruleNames[3]);
    sdaFp = setupSDAFile (sdaFile, line);
	for (int repno = 0; repno < reps; repno++)	{   // Begin hand generation */
        if (!runSDA (ruleNames, EWiters, currentVul, 2))
            return repno;   // Exceeded MAX_TRIES, give up
        printf (".");
	}
    return reps;
}

// Deal three or four hands
static int
DDAdriver (int reps, const char* ruleNames[NHANDS], bool writeSDA)
{
    int repno;
	dealerDeal deal (ruleNames, partnerRule);
	ddsInfo* ddip = new ddsInfo (reps);
	if (ddip == NULL)
        pexit ("Could not allocate ddsInfo array\n");
	for (repno = 0; repno < reps; repno++)	{	/* Begin hand generation */
        if (!deal.dealAndCheck ())
            break;  // Exceeded MAX_TRIES, give up
        processMatch (&deal, ddip, 4);
	}
    if (repno > 0)
        (void) ddsMain (repno, ddip, ddaFp, filter, declarer);
    // Find Par contract and its average score
    if (writeSDA)   {
        deal.writeSummaries (sdaFp);
        writeSDASummary (ddip, NULL, NULL);
    } else
        ddip->summarize ();
    delete ddip;
    return repno;
}

// Reads a line from the file and strips the trailing newline
inline char*
readLine(char* buffer, int count, FILE* stream)
{
    if (fgets(buffer, count, stream) != NULL) {
        size_t len = strlen(buffer);
        if (buffer[len - 1] == '\n')    // Safe access
            buffer[len - 1] = '\0';
        return buffer;
    }
    return NULL;
}

// Analyze a file containing a set of N hands
// File format must be the following:
// NString,EString,SString,WString[,DealNo]
// The first four arguments are either hands in PBN format or rules with a leading $
// DealNo defines the vulnerability. If omitted, all deals are deal 1 (none Vul)
static void
analyzeHands (int reps, char* sdaFile, bool doSDA, bool generateOnly)
{
    FILE* hp = fopen (handsFile, "r");
    if (!hp)
        pexit ("Could not open handsFile");

    // Output header
    char line[LINE_LENGTH];
    // Process first line to get label names
    if (readLine (line, LINE_LENGTH, hp) == NULL)
        pexit ("Could not read handsFile");
    if (!generateOnly)
        sdaFp = setupSDAFile (sdaFile, line);

    // Now process the hands
    int hnum = 0;
    while (readLine(line, LINE_LENGTH, hp))   {
        hnum++;
        // For each hand in handsFile
        if (!generateOnly)
            fprintf (sdaFp, "%s", line);
        csvParser p (line);

        // Get known hands, rules, and vul
        const char* ruleNames[NHANDS];
        for (int i = 0; i < NHANDS; i++) {
            char* r = p.parseNext ();
            if (r == NULL)
                ruleNames[i] = "$ANY";
            else {
                upcase_str (r);
                ruleNames[i] = r;
            }
        }
        char* cp = p.parseNext ();
        if (cp != NULL) {
            int dno = atoi(cp);
            currentVul = VUL(dno);
        }

        printf ("Hand %d, Vul %d: Rules %s, %s, %s, %s\n", hnum, currentVul, ruleNames[0], ruleNames[1], ruleNames[2], ruleNames[3]);

        // We have the known hands and rules
        if (generateOnly)   {
            if (generateDriver (reps, ruleNames, 4) != reps)
                break;
        } else if (doSDA)  {
            if (!runSDA (ruleNames, reps, currentVul, 0))
                break;  // Exceeded MAX_TRIES, give up
        } else  {
            if (DDAdriver (reps, ruleNames, true) != reps)
                break;
        }
    }
}

const char* const usageStr = "\
Usage:  %s options reps rules\n\
        %s dealer -F handsFile\n\
Options:\n\
  -l:             Long-form output (one suit per line) in allhands\n\
  -S:             Single-dummy analysis (default is double-dummy)\n\
  -G:             Generate hands only (no double or single dummy analysis)\n\
  -P partnerRule: Rule applied to combined N+S hand (e.g. total points, fit)\n\
  -s seed:        RNG seed (0 = reproducible; omit for random seed from clock)\n\
  -h:             help\n\
  -V:             Vulnerable\n\
  -d directory:   Set working directory (default \".\")\n\
  -i rulesfile:   Rule definitions file (default \"input.txt\")\n\
  -p prefix:      Prefix for dda analysis file (default none)\n\
  -f filter:      Which strains not to analyze (default \"00000\" (analyze all))\n\
  -D declarer:    N | S | E | W (default N)\n\
  -E EWiters:     Number of EW hands to iterate over\n\
  -F handsFile:   input file for hands\n\
Rules:               \n\
                  [RuleN [RuleS]]\n\
                  RuleN RuleE RuleS [RuleW]\n\
                  Any missing rules are set to Any\n\
                  If two rules are supplied, they are applied to N and S\n\
                  Else rules are applied to N, E, S, W respectively\n\
";

int
main (int argc, char** argv)
{

    const char* inputFile = "input.txt";
    const char* filePrefix = "";
    bool doSDA = false;     // default is DDA
    bool generateOnly = false;
    //	yydebug = 0;
    printf ("Starting\n");

    int optChar;
    while ((optChar = getopt (argc, argv, "lSVGhd:i:p:f:D:E:F:P:s:")) != -1)  {
        switch (optChar)    {
            case 'S':
                doSDA = true;
                break;
            case 'G':
                generateOnly = true;
                break;
            case 'P':
                partnerRuleName = optarg;
                break;
            case 'V':
                currentVul = vulNS;      // long format output
                break;
            case 'd':
                if (chdir (optarg) != 0)  {
                    printf ("failed to change directory to %s\n", optarg);
                    pexit ("chdir");
                }
                break;
            case 'i':
                inputFile = optarg;
                break;
            case 'p':
                filePrefix = optarg;
                break;
            case 'f':
                filter = optarg;
                break;
            case 'D':
                switch (*optarg)  {
                    case 'N':
                    case 'n':
                        declarer = 0;
                        break;
                    case 'E':
                    case 'e':
                        declarer = 1;
                        break;
                    case 'S':
                    case 's':
                        declarer = 2;
                        break;
                    case 'W':
                    case 'w':
                        declarer = 3;
                        break;
                    default:
                        break;
                }
                break;
            case 'F':
                handsFile = optarg;
                break;
            case 'E':
                EWiters = atoi(optarg);
                break;
            case 's':
                setRngSeed ((unsigned long)atol (optarg));
                break;
            case '?':
            case 'h':
            default:
                printf (usageStr, argv[0], argv[0]);
                exit (EINVAL);
        }
    }

    char ddaFile[FILENAME_MAX];
    char sdaFile[FILENAME_MAX];
    char bboFile[FILENAME_MAX];
    sprintf (ddaFile, "%s%s", filePrefix, "DDA.csv");
    sprintf (sdaFile, "%s%s", filePrefix, "SDA.csv");
    sprintf (bboFile, "%s%s", filePrefix, "BBO.lin");
    ddaFp = setupDDAFile (ddaFile);     // Used for all options
    bboFp = fopen (bboFile, "w");
    if (!bboFp)
        pexit ("Could not open BBO.lin");
    printf ("Reading rules\n");
    (void) read_rules (inputFile);
    printf ("Read rules\n");

    if (partnerRuleName)    {
        char s[ruleNameLen];
        s[0] = '$'; s[1] = '\0';
        strncat (s, partnerRuleName, ruleNameLen - 2);
        upcase_str (s + 1);
        partnerRule = find_rule (defroot, s);
        if (partnerRule == NULL)    {
            printf ("Could not find partner rule $%s\n", partnerRuleName);
            exit (1);
        }
    }

    time_t start_time = time(0);
    if (handsFile)    {
        openFiles (filePrefix);
        analyzeHands (EWiters, sdaFile, doSDA, generateOnly);
    } else  {
            // Process the positional arguments
        int reps  = atoi(argv[optind++]);
        const char* ruleNames[NHANDS];
        for (int k = 0; k < NHANDS; k++)    {
            int m = optind + k;
            if (argc <= m)
                ruleNames[k] = "$ANY";
            else    {
                char* s = new char[ruleNameLen];
                s[0] = '$';
                s[1] = '\0';
                strncat (s, argv[m], ruleNameLen - 2);
                upcase_str (s + 1);
                ruleNames[k] = s;
            }
        }
        bool twoRules = (argc == (optind + 2));
        if (twoRules) {  // Only two rules, assign to N and S
            ruleNames[2] = ruleNames[1];
            ruleNames[1] = "$ANY";
        }
        openFiles (filePrefix);               // Create the output files

        (void) (generateOnly ? generateDriver (reps, ruleNames, twoRules ? 2 : 4) :
                doSDA ? SDAdriver (reps, ruleNames, sdaFile) : DDAdriver (reps, ruleNames, false));
    }
    time_t tt = time(0);
    printf ("Time taken := %.f seconds\n", difftime (tt, start_time));
    dealerDeal::printReport ();
    if (bboFp)
        fclose (bboFp);
    return 0;
}
