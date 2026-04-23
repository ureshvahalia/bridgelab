#include <stdio.h>
#include "tnode.h"
#include "parse_rules.h"

extern int   yyparse ();
extern FILE* yyin;

void*
read_rules (const char* inFile)
{
    yyin = fopen (inFile, "r");
    if (yyin == NULL)   {
        printf ("Failed to open inFile %s", inFile);
        perror ("fopen");
        return NULL;
    }
    defroot = NULL;
    yyparse ();
    fclose (yyin);
    return defroot;
}
