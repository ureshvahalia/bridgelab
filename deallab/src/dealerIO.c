#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "consts.h"
#include <stdio.h>

static const char* southFile = "_south.txt";
static const char* northFile = "_north.txt";
static const char* westFile  = "_west.txt";
static const char* eastFile  = "_east.txt";
static const char* allFile   = "_all.txt";
static FILE *nh, *eh, *sh, *wh, *ah;

void
openFiles (const char* prefix)
{
    char path[FILENAME_MAX];
    // Open output files
    snprintf (path, sizeof path, "%s%s", prefix, northFile);  nh = fopen (path, "w");
    snprintf (path, sizeof path, "%s%s", prefix, eastFile);   eh = fopen (path, "w");
    snprintf (path, sizeof path, "%s%s", prefix, southFile);  sh = fopen (path, "w");
    snprintf (path, sizeof path, "%s%s", prefix, westFile);   wh = fopen (path, "w");
    snprintf (path, sizeof path, "%s%s", prefix, allFile);    ah = fopen (path, "w");
}

/* Convert integer representation to 4 */
/* character strings, one for each suit */
static char*
PBN2record (char* pbnLine, record rec)
{
	char* from = pbnLine;
	char* startSuit = pbnLine;
	int suitNum = 0;
	char* to = rec[suitNum];
	if (*from != '-')   {
        while ((*from != 0) && (*from != ' '))  {
            if (*from == '.')   {
                if (from == startSuit)    // void
                    *to++ = '-';
                *to = 0;
                to = rec[++suitNum];
                startSuit = ++from;
            } else
                *to++ = *from++;
        }
        assert (suitNum == (NSUITS - 1));
        assert (from == (pbnLine + NCARDS_IN_HAND + NSUITS - 1));
        if (from == startSuit)   // void
            *to++ = '-';
        *to = 0;
	} else  // Empty hand
        from++;
	while (*from == ' ')
        from++;
    return from;
}

/* We need to resort to a kludge in the I/O functions, probably because of */
/* a compiler bug.  The rec arguments being passed to the print functions */
/* are actually 4 x 16 character arrays, but we treat them as a single 64 */
/* char array and do the index calculations manually.  This is stupid, and */
/* I will try to fix it in future -- Uresh, 8/9/91 */

static void
print_1_hand (FILE* file, record rec, int hno, enum outputFormat style)
{
	char *format;

	format = style ? "Hand %3d:\n\n%s\n%s\n%s\n%s\n\n"
				   : "%3d: %s,%s,%s,%s\n";
	fprintf (file, format, hno, rec[0], rec[1], rec[2], rec[3]);
}


static void
print_2_hands (FILE* file, record wrec, record erec, int hno, enum outputFormat style)
{
	int i, j;

	if (style)	{
		fprintf (file, "Hand %3d:\n\n", hno);
		for (i = 0; i < NSUITS; i++)	{
			fprintf (file, "%s", wrec[i]);
			for (j = strlen (wrec[i]); j < 16; j++)
				fprintf (file, " ");
			fprintf (file, "%s\n", erec[i]);
		}
		fprintf (file, "\n");
	} else	{
		fprintf (file, "%3d: %s,%s,%s,%s\n",
				hno, wrec[0], wrec[1], wrec[2], wrec[3]);
		fprintf (file, "     %s,%s,%s,%s\n\n",
				erec[0], erec[1], erec[2], erec[3]);
	}
}

static void
print_4_hands (FILE* file, record nrec, record erec, record srec, record wrec, int hno, enum outputFormat style)
{
	int i, j;

	if (style)	{
		fprintf (file, "Hand %3d:\n", hno);
		for (i = 0; i < NSUITS; i++)	{
			fprintf (file, "        %s\n", nrec[i]);
		}
		for (i = 0; i < NSUITS; i++)	{
			fprintf (file, "%s", wrec[i]);
			for (j = strlen (wrec[i]); j < 16; j++)
				fprintf (file, " ");
			fprintf (file, "%s\n", erec[i]);
		}
		for (i = 0; i < NSUITS; i++)	{
			fprintf (file, "        %s\n", srec[i]);
		}
		fprintf (file, "\n");
	} else	{
		fprintf (file, "%3d:            N: %s,%s,%s,%s\n",
				hno, nrec[0], nrec[1], nrec[2], nrec[3]);
		fprintf (file, "     W: %s,%s,%s,%s",
				wrec[0], wrec[1], wrec[2], wrec[3]);
		fprintf (file, "        E: %s,%s,%s,%s\n",
				erec[0], erec[1], erec[2], erec[3]);
		fprintf (file, "                S: %s,%s,%s,%s\n\n",
				srec[0], srec[1], srec[2], srec[3]);
	}
}

void
printHandsFromPbn (char* pbnLine, int c, enum outputFormat style)
{
    static int hno = 1;
    assert ((c == 1) || (c == 2) || (c == 4));
    record nrec, erec, srec, wrec;
    char* cp = PBN2record (pbnLine, nrec);
    print_1_hand (nh, nrec, hno, style);
    if (c == 2)  {
        cp = PBN2record (cp, erec);
        cp = PBN2record (cp, srec);
        print_1_hand (sh, srec, hno, style);
        print_2_hands (ah, nrec, srec, hno, style);
    } else if (c == 4)  {
        cp = PBN2record (cp, erec);
        cp = PBN2record (cp, srec);
        cp = PBN2record (cp, wrec);
        print_1_hand (eh, erec, hno, style);
        print_1_hand (sh, srec, hno, style);
        print_1_hand (wh, wrec, hno, style);
        print_4_hands (ah, nrec, erec, srec, wrec, hno, style);
    }
    hno++;
}
