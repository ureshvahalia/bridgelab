%{
#include <stdlib.h>
#include <stdio.h>
#include <strings.h>
#include <ctype.h>
#include "tnode.h"
#include "majMinExpand.hpp"
#include "log.h"
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
	logError ("%s at line %d (near \"%s\")\n", msg, mapExpandedLineToOriginal (yylineno), yytext);
}

%}

%start list

%token NUMBER AVAR KEYVAR SHAPE PATTERN DEFNAME
%token LEQ GEQ EQU NEQ OR AND END TO

%right GETS ANDGETS ORGETS
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

// "def END"/"def ';' END" are handled directly here rather than through an
// intermediate "deflist" nonterminal (as this grammar had previously): with
// an intermediate step, "def" reducing up to it was triggered by *any*
// lookahead that couldn't extend "def" further (not specifically END) --
// including a stray DEFNAME caused by a missing ';' between two rules,
// which that reduction couldn't tell apart from a legitimate, fully-parsed
// file. That silently discarded everything from the missing ';' onward
// with no error at all, since the reduction's own action unconditionally
// returned from yyparse() right there. Requiring END as a real, shifted
// token in "list"'s own alternatives (rather than deducing "we must be
// done" from what DIDN'T match) means a missing ';' has no valid
// continuation at all in that state and falls through to bison's own
// syntax-error handling below instead.
list	:    /* empty */
	|   END
		{   logDebug ("Exit 1\n"); return 1;	}
	|   list stat END
		{   logDebug ("Exit 2\n"); return 2;	}
	|   list def END
		{   logDebug ("Exit 3\n"); return 3;	}
	|   list def ';' END
		{   logDebug ("Exit 3\n"); return 3;	}
	|   list error
		{   yyerrok; yyerror ("syntax error"); exit (1);	}
	;

def	:   DEFNAME GETS expr
		{
		    logDebug ("Making def %s\n", (char*)$1);
		    if (find_def_node (defroot, (const char*)$1) != NULL)	{
                char msg[300];
                snprintf (msg, sizeof (msg), "warning: redefining %s", (char*)$1);
                yyerror (msg);
		    }
		    parent = make_leaf (TDEFINE, (long long)$1);
		    $$ = add_leaves (parent, NULL, (TPTR)$3);
		    if (defroot == NULL)
                lastDef = defroot = parent;
		    index_def ((const char*)$1, parent);
		    logDebug ("Making def complete, defroot %p\n", defroot);
		}
	|   DEFNAME ANDGETS expr
		{
		    parent = (TPTR)find_def_node (defroot, (const char*)$1);
		    if (parent == NULL)	{
                char msg[300];
                snprintf (msg, sizeof (msg), "\":&\" requires an earlier definition of %s", (char*)$1);
                yyerrok;
                yyerror (msg);
                exit (1);
		    }
		    parent1 = make_leaf (TAND, TAND);
		    (void)add_leaves (parent1, parent->t_right, (TPTR)$3);
		    (void)add_leaves (parent, NULL, parent1);
		    $$ = NULL;
		}
	|   DEFNAME ORGETS expr
		{
		    parent = (TPTR)find_def_node (defroot, (const char*)$1);
		    if (parent == NULL)	{
                char msg[300];
                snprintf (msg, sizeof (msg), "\":|\" requires an earlier definition of %s", (char*)$1);
                yyerrok;
                yyerror (msg);
                exit (1);
		    }
		    parent1 = make_leaf (TOR, TOR);
		    (void)add_leaves (parent1, parent->t_right, (TPTR)$3);
		    (void)add_leaves (parent, NULL, parent1);
		    $$ = NULL;
		}
	|   def ';' def
		{
		    logDebug ("Adding def %p to %p\n", $1, $3);
		    /* ANDGETS/ORGETS reductions above yield $$ = NULL: they modify
		     * an existing definition node in place rather than adding a new
		     * one to the chain, so there is nothing new to link in and
		     * lastDef must not move. */
		    if ($3 != NULL)	{
                (void)add_leaves ((TPTR)lastDef, (TPTR)$3, NULL);
                lastDef = (TPTR)$3;
		    }
		    $$ = lastDef;
		    logDebug ("Adding def complete, defroot %p\n", defroot);
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
		    if ($$ == 0)	{
                char msg[256];
                snprintf (msg, sizeof (msg), "undefined rule reference %s", (char*)$1);
                yyerrok;
                yyerror (msg);
                exit (1);
		    }
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
