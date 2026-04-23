#ifndef _PARSE_RULES_H_
#define _PARSE_RULES_H_

// Parse a rules file using the flex/bison scanner/parser.
// Opens inFile, runs yyparse(), closes the file.
// Returns defroot (the parsed rule tree root), or NULL on error.
void* read_rules (const char* inFile);

#endif  // _PARSE_RULES_H_
