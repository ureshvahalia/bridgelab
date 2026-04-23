/*
   DDS, a bridge double dummy solver.

   Copyright (C) 2006-2014 by Bo Haglund /
   2014 by Bo Haglund & Soren Hein.

   See LICENSE and README.
*/


// Test program for the CalcAllTablesPBN function.
// Uses the hands pre-set in hands.cpp.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "dll.h"
#include "consts.h"
#include "hands.h"
#include "ddsinfo.hpp"

class handInfo  {
    int points;
    int pat[NSUITS];
  public:
    char* initialize (char* handp); // initializes from a PBN format hand
    char* convertToText (char* cp); // Returns position at end of text
};

char*
handInfo::initialize (char* handp)
{
    // Assume strict format
    // spades.hearts.diamonds.clubs followed by space or newline or NULL character
    // Can add more tolerance or error checks later
    // This means 17 chars at most, fewer if it is a partial deal
    char* cp = handp;
    if (*cp == '-')
        return (cp + 2);    // Skip following space
    int suit = 0;
    for (int i = 0; i < 17; i++)    {
        switch (*cp++)  {
        case 'A':
        case 'a':
            points += 4;
            pat[suit]++;
            break;
        case 'K':
        case 'k':
            points += 3;
            pat[suit]++;
            break;
        case 'Q':
        case 'q':
            points += 2;
            pat[suit]++;
            break;
        case 'J':
        case 'j':
            points += 1;
            pat[suit]++;
            break;
        case 'T':
        case 't':
        case '9':
        case '8':
        case '7':
        case '6':
        case '5':
        case '4':
        case '3':
        case '2':
            pat[suit]++;
            break;
        case '.':
            if (++suit == DDS_SUITS)
                return NULL;    // Too many '.'s
            break;
        case ' ':
        case '\n':
        case '\0':
        default:
            return (suit == (DDS_SUITS - 1) ? cp : NULL);
            break;
        }
    }
    return NULL;
}

char*
handInfo::convertToText (char* cp)
{
    sprintf (cp, "%d,%d,%d,%d,%d,", points,
             pat[0], pat[1], pat[2], pat[3]);
    while (*cp)
        cp++;
    return cp;
}

class dealInfo {
    handInfo hi[DDS_HANDS];
    void convertToText (char* cp, vulnerabilityCodes vcode);
  public:
    int maxTricks[DDS_STRAINS];
    dealInfo* initialize (char* PBNdeal);
    static void writeHeader (FILE* oh);
    void writeCSVoutput (FILE* oh, char* pbnStr, vulnerabilityCodes vcode);
};

dealInfo*
dealInfo::initialize (char* PBNdeal)
{
    // Assume rigid structure
    // X:hand1 hand2 hand3 hand4
    // where X is N, S, E, or W, and hand[1-4] are in PBN format with one single space between them
    // Can add more tolerance and error checking later
    // Returns filled-in dip on success, NULL on failure
    char* cp = PBNdeal;
    int first;
    switch (*cp)    {
    case 'N':
    case 'n':
        first = 0;
        break;
    case 'E':
    case 'e':
        first = 1;
        break;
    case 'S':
    case 's':
        first = 2;
        break;
    case 'W':
    case 'w':
        first = 3;
        break;
    default:
        return NULL;
        break;
    }
    cp += 2;    // Skip the ':'

    memset (this, 0, sizeof(dealInfo));
    for (int i = 0; i < DDS_HANDS; i++) {
        if ((cp = hi[first].initialize (cp)) == NULL)
            return NULL;
        if (++first > 3)
            first = 0;
    }
    return this;
}

void
dealInfo::convertToText (char* strp, vulnerabilityCodes vcode)
{
    char* cp = strp;
    int i;
    for (i = 0; i < DDS_HANDS; i++)
        cp = hi[i].convertToText (cp);
    for (i = 0; i < DDS_STRAINS; i++)   {
        sprintf (cp, "%d,", maxTricks[i]);
        cp += (2 + maxTricks[i] / 10);
    }
    i = NSparDD (maxTricks, cp, (vcode == vulNS) || (vcode == vulBoth), true);
    while (*cp)
        cp++;
    sprintf (cp, ",%d\n", i);
}


void
dealInfo::writeCSVoutput (FILE* oh, char* pbnStr, vulnerabilityCodes vcode)
{
    char line[1024];
    char* cp = pbnStr + 2;  // Skip dealer and ':'
    char* lp = line;
    while ((*cp != '\n') && (*cp != '\0'))
        *lp++ = *cp++;
    if (lp > line + 67)
        printf ("Could not print hand %s", pbnStr);
    *lp++ = ',';
    convertToText (lp, vcode);
    fprintf (oh, "%s", line);
}

void
ddsMain (int handCount, ddsInfo* ddip, FILE* outFp, const char* filter, int declarer)
{
    ddTableDealsPBN DDdealsPBN;
    ddTablesRes tableRes;
    allParResults pres;

    int mode = 0; // No par calculation
    int trumpFilter[DDS_STRAINS]; // Default filter = 00000 = All
    int res;
    enum { lineSize = 80 };
    char line[lineSize];

    SetMaxThreads(0);
    DDSInfo info;
    GetDDSInfo (&info);
    sscanf (filter, "%1d%1d%1d%1d%1d", trumpFilter, trumpFilter + 1, trumpFilter + 2,
            trumpFilter + 3, trumpFilter + 4);
    enum { maxHandsPerRound = 32 };
    int handNum = 1;
    bool done = false;
    // printf ("Current local time and date: %s", asctime(localtime(&t0)));
    int hnum = 0;
    ddip->restartPbn();
    while (!done)   {
        int handsThisRound;
        for (handsThisRound = 0; handsThisRound < maxHandsPerRound; handsThisRound++, hnum++) {
            if (hnum == handCount)    {
                done = true;
                break;
            }
            strcpy (DDdealsPBN.deals[handsThisRound].cards, ddip->nextPbn());
            // if (fgets (DDdealsPBN.deals[handsThisRound].cards, lineSize, ih) == NULL)    {
        }
        if (handsThisRound == 0)
            break;

        DDdealsPBN.noOfTables = handsThisRound;
        res = CalcAllTablesPBN(&DDdealsPBN, mode, trumpFilter,
                               &tableRes, &pres);
        if (res != RETURN_NO_FAULT) {
            ErrorMessage(res, line);
            printf("DDS error: %s\n", line);
            for (int k = 0; k < handsThisRound; k++)
                printf("%d: %s\n", k, DDdealsPBN.deals[k].cards);
            exit(1);
        }

        dealInfo deal;
        for (int handno = 0; handno < handsThisRound; handno++)    {
            if (deal.initialize (DDdealsPBN.deals[handno].cards) == NULL)
                printf ("Could not translate %s", DDdealsPBN.deals[handno].cards);
            for (int j = 0; j < DDS_STRAINS; j++)   {
                if (trumpFilter[j] == 0)    {
                    ddip->setMaxTricks (handNum - 1, j, deal.maxTricks[j] = tableRes.results[handno].resTable[j][declarer]);
                }
            }
            deal.writeCSVoutput (outFp, DDdealsPBN.deals[handno].cards, ddip->vulCode());
            handNum++;
        }
    }
}

