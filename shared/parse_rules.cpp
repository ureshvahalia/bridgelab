#include <stdio.h>
#include <string>
#include "tnode.h"
#include "parse_rules.h"
#include "majMinExpand.hpp"
#include "trumpAskExpand.hpp"
#include "log.h"

extern int   yyparse ();

// Declared and defined by the Flex-generated scanner (bridge.scanner.cc,
// included into bridge.parser.cc) — like yylineno/yytext in bridge.y, no
// public header is generated for these, so forward-declare them directly.
struct yy_buffer_state;
typedef struct yy_buffer_state* YY_BUFFER_STATE;
extern YY_BUFFER_STATE yy_scan_string (const char* yy_str);
extern void yy_delete_buffer (YY_BUFFER_STATE b);
extern int  yylineno;

void*
read_rules (const char* inFile)
{
    FILE* fp = fopen (inFile, "r");
    if (fp == NULL)   {
        logError ("Failed to open inFile %s", inFile);
        perror ("fopen");
        return NULL;
    }
    std::string rawText;
    char buf[4096];
    size_t n;
    while ((n = fread (buf, 1, sizeof (buf), fp)) > 0)
        rawText.append (buf, n);
    fclose (fp);

    std::string expandedText = expandMajMinMacros (rawText, inFile);
    expandedText = graftAskTemplates (expandedText, inFile);
    expandedText = resolveTrumpReferences (expandedText, inFile);

    defroot = NULL;
    yylineno = 1;
    YY_BUFFER_STATE bufState = yy_scan_string (expandedText.c_str());
    yyparse ();
    yy_delete_buffer (bufState);
    return defroot;
}
