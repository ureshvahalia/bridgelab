%{
#include <stdlib.h>
#include <stdio.h>
#include <strings.h>
#include <ctype.h>
#include "tnode.h"
#define YYSTYPE TPTR
#define YYDEBUG 1
#include "bridge.parser.hh"

typedef struct key *KPTR;
typedef char *CPTR;
int base, uvar, ktype, karr, mac;
TPTR parent, parent1, parent2, arr, avar, ndx, name, lastDef, root;
int yylex ();
extern int yylineno;
extern char* yytext;

void
yyerror (char const* msg)
{
	fprintf (stderr, "%s at line %d (near \"%s\")\n", msg, yylineno, yytext);
}

%}

%start list

%token NUMBER AVAR KEYVAR SHAPE PATTERN DEFNAME
%token LEQ GEQ EQU NEQ OR AND END TO

%right GETS
%left ';'
%left OR
%left '^'
%left AND
%left '<' '>' EQU NEQ GEQ LEQ
%left '+' '-'
%left '*' '/' '%'
%left UMINUS
%left UNOT
%nonassoc TO
%right SHAPE PATTERN

%%

list	:    /* empty */
	|   END
		{   printf("Exit 1\n"); return 1;	}
	|   list stat END
		{   printf("Exit 2\n"); return 2;	}
	|   list deflist END
		{   printf("Exit 3\n"); return 3;	}
	|   list error
		{   yyerrok; yyerror ("syntax error"); exit (1);	}
	;

deflist	:   def
		{
#ifdef DEBUG
		    printf ("Making deflist with %p\n", $1);
#endif
		    return 2;
		}
	;

def	:   DEFNAME GETS expr
		{
#ifdef DEBUG
		    printf ("Making def %s\n", (char*)$1);
#endif
		    parent = make_leaf (TDEFINE, (long long)$1);
		    $$ = add_leaves (parent, NULL, (TPTR)$3);
		    if (defroot == NULL)
                lastDef = defroot = parent;
#ifdef DEBUG
		    printf ("Making def complete, defroot %p\n", defroot);
#endif
		}
	|   def ';' def
		{
#ifdef DEBUG
		    printf ("Adding def %p to %p\n", $1, $3);
#endif
		    (void)add_leaves ((TPTR)lastDef, (TPTR)$3, NULL);
		    $$ = lastDef = $3;
#ifdef DEBUG
		    printf ("Adding def complete, defroot %p\n", defroot);
#endif
		}
    |   def ';' END
        {
#ifdef DEBUG
		    printf ("Making deflist end with %p\n", $1);
#endif
        }
	;

stat	:    expr
		{
		    root = $1;
		}
	;

expr	:    '('	expr	')'
		{   $$ = $2;	}
	|   var
        {   $$ = $1;    }
	|   SHAPE expr
        {   $$ = $2;    }
	|   PATTERN expr
        {   $$ = $2;    }
	|   NUMBER
		{   $$ = make_leaf (TINT, (long long)$1);	}
	|   expr '<' expr
		{
		    parent = make_leaf (TLT, TLT);
		    $$ = add_leaves (parent, (TPTR)$1, (TPTR)$3);
		}
	|   expr LEQ expr
		{
		    parent = make_leaf (TLEQ, TLEQ);
		    $$ = add_leaves (parent, (TPTR)$1, (TPTR)$3);
		}
	|   expr '>' expr
		{
		    parent = make_leaf (TGT, TGT);
		    $$ = add_leaves (parent, (TPTR)$1, (TPTR)$3);
		}
	|   expr GEQ expr
		{
		    parent = make_leaf (TGEQ, TGEQ);
		    $$ = add_leaves (parent, (TPTR)$1, (TPTR)$3);
		}
	|   expr EQU expr
		{
		    parent = make_leaf (TEQU, TEQU);
		    $$ = add_leaves (parent, (TPTR)$1, (TPTR)$3);
		}
	|   expr NEQ expr
		{
		    parent = make_leaf (TNEQ, TNEQ);
		    $$ = add_leaves (parent, (TPTR)$1, (TPTR)$3);
		}
	|   expr OR expr
		{
		    parent = make_leaf (TOR, TOR);
		    $$ = add_leaves (parent, (TPTR)$1, (TPTR)$3);
		}
	|   expr AND expr
		{
		    parent = make_leaf (TAND, TAND);
		    $$ = add_leaves (parent, (TPTR)$1, (TPTR)$3);
		}
	|   expr '^' expr
		{
		    parent = make_leaf (TXOR, TXOR);
		    $$ = add_leaves (parent, (TPTR)$1, (TPTR)$3);
		}
	|   UNOT expr
		{
		    parent = make_leaf (TNOT, TNOT);
		    $$ = add_leaves (parent, (TPTR)0, (TPTR)$2);
		}
    |   '!' expr		%prec UNOT
		{
		    parent = make_leaf (TNOT, TNOT);
		    $$ = add_leaves (parent, (TPTR)0, (TPTR)$2);
		}
    |   expr TO expr expr
        {
            parent1 = make_leaf (TGEQ, TGEQ);
            (void)add_leaves (parent1, (TPTR)$4, (TPTR)$1);
            parent2 = make_leaf (TLEQ, TLEQ);
            (void)add_leaves (parent2, (TPTR)$4, (TPTR)$3);
            parent = make_leaf (TAND, TAND);
            $$ = add_leaves (parent, parent1, parent2);
        }
	;
var	:   KEYVAR
		{
		    $$ = match_string ((char*)$1);
		    if ($$ == 0)	{
                yyerrok;
                yyerror ("syntax error");
                exit (1);
		    }
		}
	|   '[' NUMBER ',' NUMBER ',' NUMBER ',' NUMBER ']'
		{
		    $$ = make_leaf (TSHAPE, ((long long)($2) << 24) +
					    ((long long)($4) << 16) +
					    ((long long)($6) << 8) + (long long)($8));
		}
	|   '[' NUMBER '.' NUMBER '.' NUMBER '.' NUMBER ']'
		{
		    $$ = make_leaf (TSHAPE, ((long long)($2) << 24) +
					    ((long long)($4) << 16) +
					    ((long long)($6) << 8) + (long long)($8));
		}
	|   '[' NUMBER '-' NUMBER '-' NUMBER '-' NUMBER ']'
		{
		    $$ = make_leaf (TPATTERN, ((long long)($2) << 24) +
					    ((long long)($4) << 16) +
					    ((long long)($6) << 8) + (long long)($8));
		}
	|   DEFNAME
		{
		    $$ = (TPTR)(find_rule (defroot, (const char*)$1));
		}
	;
%%


/*
%{
uvleq	:   '<' '=' ;
uvgeq	:   '>' '=' ;
uvequ	:   '?' '=' ;
uvneq	:   '!' '=' ;
uvor	:   '|' '|' ;
uvand	:   '&' '&' ;
%}

	|   expr '+' expr
		{
		    parent = make_leaf (TPLUS, TPLUS);
		    $$ = add_leaves (parent, (TPTR)$1, (TPTR)$3);
		}
	|   expr '-' expr
		{
		    parent = make_leaf (TMINUS, TMINUS);
		    $$ = add_leaves (parent, (TPTR)$1, (TPTR)$3);
		}
	|   expr '*' expr
		{
		    parent = make_leaf (TMULT, TMULT);
		    $$ = add_leaves (parent, (TPTR)$1, (TPTR)$3);
		}
	|   expr '/' expr
		{
		    parent = make_leaf (TDIV, TDIV);
		    $$ = add_leaves (parent, (TPTR)$1, (TPTR)$3);
		}
	|   expr '%' expr
		{
		    parent = make_leaf (TMOD, TMOD);
		    $$ = add_leaves (parent, (TPTR)$1, (TPTR)$3);
		}
	|   '-' expr	%prec UMINUS
		{
		    parent = make_leaf (TNEG, TNEG);
		    $$ = add_leaves (parent, (TPTR)0, (TPTR)$2);
		}
	|   SHAPE '[' NUMBER ',' NUMBER ',' NUMBER ',' NUMBER ']'
		{
		    $$ = make_leaf (TSHAPE, ((long)($3) << 24) +
					    ((long)($5) << 16) +
					    ((long)($7) << 8) + (long)($9));
		}
	|   PATTERN '[' NUMBER ',' NUMBER ',' NUMBER ',' NUMBER ']'
		{
		    $$ = make_leaf (TPATTERN, ((long)$3 << 24) +
					      ((long)$5 << 16) +
					      ((long)$7 << 8) + (long)$9);
		}
*/


#include "bridge.scanner.cc"
