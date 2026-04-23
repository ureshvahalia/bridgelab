%{
#define _CRT_SECURE_NO_DEPRECATE
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "bridge.parser.hh"

char* makeDef (char*);
extern FILE *lexin;
/* # define input() (((yytchar=yysptr>yysbuf?U(*--yysptr):getc(lexin))==10?(yylineno++,yytchar):yytchar)==EOF?0:yytchar) */
%}
%option nounput
%%
[ \t]	;
"\r\n"	;
"<="	{
		return (LEQ);
	}
">="	{
		return (GEQ);
	}
"?="	{
		return (EQU);
	}
"!="	{
		return (NEQ);
	}
"||"	{
		return (OR);
	}
"&&"	{
		return (AND);
	}
":="	{
		return (GETS);
	}
"AND"	{
		return (AND);
	}
"OR"	{
		return (OR);
	}
"NOT"   {
        return (UNOT);
    }
"TO"    {
        return (TO);
    }
end	{
		return (END);
	}
"and"	{
		return (AND);
	}
"or"	{
		return (OR);
	}
"not"   {
        return (UNOT);
    }
"to"    {
        return (TO);
    }
"END"	{
		return (END);
	}
shape	{
		return (SHAPE);
	}
Shape	{
		return (SHAPE);
	}
Pattern	{
		return (PATTERN);
	}
pattern	{
		return (PATTERN);
	}
\$[a-zA-Z0-9_\-\.][a-zA-Z0-9_\-\.]*	{
            yylval = (TPTR)(makeDef (yytext));
            return (DEFNAME);
        }
[a-zA-Z][a-zA-Z0-9]*	{
			for (char* p = yytext; *p; p++) *p = toupper((unsigned char)*p);
			yylval = (TPTR)yytext;
            return (KEYVAR);
        }
[0-9][0-9]*	{
			yylval = (TPTR)(long long)atoi (yytext);
			return (NUMBER);
		}
\n		;
[^a-z0-9]	{
			return(yytext[0]);
		}
##.*$	;
%%

int
yywrap ()
{
    return 1;
}

/* Allocate a string to store value of text */
char*
makeDef (char* s)
{
	char* s2 = (char*)malloc (strlen (s) + 1);
	char* p = s2;
	for (; *s; s++, p++) *p = toupper((unsigned char)*s);
	*p = '\0';
	return s2;
}
