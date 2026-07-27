#ifndef _MAJMINEXPAND_HPP_
#define _MAJMINEXPAND_HPP_

#include <string>

// Expands the Maj/Min/OMaj/OMin/BMaj/BMin macros (see shared/hand-spec.md)
// in rawText (the full contents of a rules file just read from inFile),
// validates bid-sequence legality on every "$."-shaped rule name it
// produces (dropping illegal auto-generated variants with a warning,
// failing hard on an illegal hand-typed name), and returns the fully
// expanded, ordinary, re-parseable rules text — identical in every
// functional respect to what the parser would have seen directly, but
// with every macro already resolved to concrete suits.
//
// Also writes the expanded text to "<inFile>.expanded.txt", and (only in
// -DDEBUG2 builds) prints a trace of every expansion performed.
//
// Populates the line-number map consulted by mapExpandedLineToOriginal(),
// so parser error messages report original source line numbers even
// though the parser only ever sees the expanded text.
std::string expandMajMinMacros (const std::string& rawText, const char* inFile);

// Translates a 1-based line number in the most recently expanded text back
// to the corresponding 1-based line number in the original source file.
// Returns expandedLine unchanged if no expansion has run yet.
int mapExpandedLineToOriginal (int expandedLine);

#endif  // _MAJMINEXPAND_HPP_
