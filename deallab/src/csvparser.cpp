#include "csvparser.hpp"
#include <stdio.h>
#include <assert.h>
#include "log.h"

csvParser::csvParser (char* csvStr)
{
    startChar = csvStr;
}

char*
csvParser::parseNext (char lim)
{
    char* s2 = startChar;
    char* retval = startChar;
    while (*s2) {
        if (*s2 == '\"')    {
            do
                s2++;
            while (*s2 && (*s2 != '\"'));
            if (!*s2)   {
                startChar = s2;
                return NULL;    // Incomplete quoted string
            }
            s2++;       // skip closing '\"'
        }
        if (*s2 == lim) {
            *s2 = 0;
            startChar = s2 + 1;
            return retval;
        } else if (*s2 == '\n') {   // reached end of line
            *s2 = 0;
            startChar = s2;
            return retval;
        }
        s2++;
    }
    if (*s2)   {
        logError ("Logic error in csvParse\n");
        return NULL;
    }
    if (s2 == startChar)    // started with empty string
        return NULL;
    assert (*s2 == 0);
    startChar = s2;
    return retval;
}
