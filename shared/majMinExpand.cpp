// Preprocessing pass that expands the Maj/Min/OMaj/OMin/BMaj/BMin macros
// (see shared/hand-spec.md) into ordinary, concrete rule definitions before
// the file is handed to the Flex/Bison parser. See majMinExpand.hpp.
//
// This module is deliberately written with std::string/std::vector rather
// than the fixed-size char-buffer style used elsewhere in this codebase:
// the statement-splitting/combinatorial-expansion work here is inherently
// variable-length and this is new, self-contained code, so the safety and
// clarity STL gives outweighs matching the surrounding C idiom.

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "bid.hpp"
#include "majMinExpand.hpp"

namespace {

// ── Small text utilities ──────────────────────────────────────────────────

std::string upper (const std::string& s)
{
    std::string r = s;
    for (char& c : r)
        c = (char)toupper ((unsigned char)c);
    return r;
}

bool isIdentChar (char c)   { return isalpha ((unsigned char)c) != 0; }

bool matchesWordCI (const std::string& text, size_t pos, const char* word)
{
    size_t len = strlen (word);
    if (pos + len > text.size())
        return false;
    if (pos > 0 && isIdentChar (text[pos - 1]))
        return false;
    if (pos + len < text.size() && isIdentChar (text[pos + len]))
        return false;
    for (size_t i = 0; i < len; i++)
        if (toupper ((unsigned char)text[pos + i]) != toupper ((unsigned char)word[i]))
            return false;
    return true;
}

// ── Suits and macro keywords ──────────────────────────────────────────────

enum Suit { SPADES, HEARTS, DIAMONDS, CLUBS, SUIT_NONE };

const char* suitWord (Suit s)
{
    static const char* names[] = { "Spades", "Hearts", "Diamonds", "Clubs" };
    return names[s];
}

char suitLetter (Suit s)
{
    static const char letters[] = { 'S', 'H', 'D', 'C' };
    return letters[s];
}

Suit complementSuit (Suit s)
{
    switch (s) {
        case SPADES:   return HEARTS;
        case HEARTS:   return SPADES;
        case DIAMONDS: return CLUBS;
        case CLUBS:    return DIAMONDS;
        default:       return SUIT_NONE;
    }
}

enum KeywordKind { KW_NONE, KW_MAJ, KW_OMAJ, KW_MIN, KW_OMIN, KW_BMAJ, KW_BMIN };

bool isMajorPair (KeywordKind k)   { return k == KW_MAJ || k == KW_OMAJ; }
bool isMinorPair (KeywordKind k)   { return k == KW_MIN || k == KW_OMIN; }

KeywordKind classifyWord (const std::string& wUpper)
{
    if (wUpper == "MAJ"  || wUpper == "MAJOR")       return KW_MAJ;
    if (wUpper == "OMAJ" || wUpper == "OMAJOR")      return KW_OMAJ;
    if (wUpper == "MIN"  || wUpper == "MINOR")       return KW_MIN;
    if (wUpper == "OMIN" || wUpper == "OMINOR")      return KW_OMIN;
    if (wUpper == "BMAJ" || wUpper == "BOTHMAJORS")  return KW_BMAJ;
    if (wUpper == "BMIN" || wUpper == "BOTHMINORS")  return KW_BMIN;
    return KW_NONE;
}

const char* keywordDisplayName (KeywordKind k)
{
    switch (k) {
        case KW_MAJ:  return "MAJ";
        case KW_OMAJ: return "OMAJ";
        case KW_MIN:  return "MIN";
        case KW_OMIN: return "OMIN";
        case KW_BMAJ: return "BMAJ";
        case KW_BMIN: return "BMIN";
        default:      return "?";
    }
}

struct KeywordHit { size_t pos; size_t len; KeywordKind kind; };

// Finds the next maximal run of letters, at or after fromPos, whose whole
// (case-insensitive) text is one of our macro keywords. Word-boundary safe:
// compares the *entire* letter run, so "Minor" is never mistaken for a
// truncated "Min" and "AdMin"-style false positives can't happen.
bool findNextKeywordWord (const std::string& text, size_t fromPos, KeywordHit& hit)
{
    size_t i = fromPos, n = text.size();
    while (i < n) {
        if (isIdentChar (text[i])) {
            size_t start = i;
            while (i < n && isIdentChar (text[i]))
                i++;
            KeywordKind k = classifyWord (upper (text.substr (start, i - start)));
            if (k != KW_NONE) {
                hit = { start, i - start, k };
                return true;
            }
            continue;
        }
        i++;
    }
    return false;
}

// ── Fatal / warning reporting ──────────────────────────────────────────────

[[noreturn]] void fatal (const std::string& msg)
{
    fprintf (stderr, "%s\n", msg.c_str());
    exit (1);
}

void warn (const std::string& msg)
{
    fprintf (stderr, "warning: %s\n", msg.c_str());
}

// ── Statement splitting ────────────────────────────────────────────────────

struct Stmt {
    int line;
    std::string name;   // e.g. "$.1N.2C.2Maj." or "$X"; empty for a bare end marker
    std::string op;     // ":=", ":&", or ":|"; empty for a bare end marker
    std::string body;   // empty for a bare end marker
    bool isEndMarker;
};

std::vector<Stmt> splitStatements (const std::string& text)
{
    std::vector<Stmt> out;
    size_t i = 0, n = text.size();
    int line = 1;

    auto skipWsAndComments = [&] () {
        for (;;) {
            while (i < n && isspace ((unsigned char)text[i])) {
                if (text[i] == '\n') line++;
                i++;
            }
            if (i + 1 < n && text[i] == '#' && text[i + 1] == '#') {
                while (i < n && text[i] != '\n')
                    i++;
                continue;
            }
            break;
        }
    };

    while (true) {
        skipWsAndComments();
        if (i >= n)
            break;
        int stmtLine = line;
        if (text[i] == '$') {
            size_t start = i;
            i++;
            while (i < n && (isalnum ((unsigned char)text[i]) || text[i] == '_' || text[i] == '-' || text[i] == '.'))
                i++;
            std::string name = text.substr (start, i - start);
            skipWsAndComments();
            if (!(i + 1 < n && text[i] == ':' &&
                  (text[i + 1] == '=' || text[i + 1] == '&' || text[i + 1] == '|')))
                fatal ("majMinExpand: expected \":=\", \":&\", or \":|\" after rule name " + name +
                       " at line " + std::to_string (stmtLine));
            std::string op = text.substr (i, 2);
            i += 2;
            skipWsAndComments();
            size_t bodyStart = i;
            int depth = 0;
            while (i < n) {
                char c = text[i];
                if (c == '(' || c == '[')
                    depth++;
                else if (c == ')' || c == ']')
                    depth--;
                else if (c == '\n')
                    line++;
                if (depth == 0 && c == ';')
                    break;
                if (depth == 0 && matchesWordCI (text, i, "end"))
                    break;
                i++;
            }
            std::string body = text.substr (bodyStart, i - bodyStart);
            while (!body.empty() && isspace ((unsigned char)body.back()))
                body.pop_back();
            if (i < n && text[i] == ';')
                i++;
            out.push_back ({ stmtLine, name, op, body, false });
        } else if (matchesWordCI (text, i, "end")) {
            i += 3;
            out.push_back ({ stmtLine, "", "", "", true });
        } else {
            // Content our simple splitter doesn't recognize (should not
            // happen for well-formed input) — stop here and let whatever
            // was already collected be handled; the real parser will
            // surface any genuine syntax error in the remainder.
            break;
        }
    }
    return out;
}

// ── Name-side bid-token analysis ───────────────────────────────────────────

struct BidNameToken {
    std::string raw;        // original token text, e.g. "2Maj", "1N", "P"
    bool isPass;
    Suit concreteSuit;      // SUIT_NONE if this is a placeholder
    KeywordKind placeholder;// KW_NONE if this is concrete/pass
    std::string levelDigit; // e.g. "2" ("" for Pass)
};

// Returns false if name doesn't start "$." or any token fails to parse as a
// legal bid-sequence token shape — callers treat that as "ordinary name,
// no forking", exactly like the original hand-written parseBid() does.
bool analyzeBidName (const std::string& name, std::vector<BidNameToken>& tokens)
{
    if (name.size() < 2 || name[0] != '$' || name[1] != '.')
        return false;
    size_t i = 2;
    while (i < name.size()) {
        size_t dot = name.find ('.', i);
        if (dot == std::string::npos)
            return false;
        std::string token = name.substr (i, dot - i);
        i = dot + 1;
        if (token.empty())
            return false;
        std::string up = upper (token);
        BidNameToken t;
        t.raw = token;
        if (up == "P") {
            t.isPass = true;
            t.concreteSuit = SUIT_NONE;
            t.placeholder = KW_NONE;
            tokens.push_back (t);
            continue;
        }
        if (!isdigit ((unsigned char)token[0]))
            return false;
        t.isPass = false;
        t.levelDigit = token.substr (0, 1);
        std::string rest = upper (token.substr (1));
        if (rest == "C")      { t.concreteSuit = CLUBS;    t.placeholder = KW_NONE; }
        else if (rest == "D") { t.concreteSuit = DIAMONDS; t.placeholder = KW_NONE; }
        else if (rest == "H") { t.concreteSuit = HEARTS;   t.placeholder = KW_NONE; }
        else if (rest == "S") { t.concreteSuit = SPADES;   t.placeholder = KW_NONE; }
        else if (rest == "N") { t.concreteSuit = SUIT_NONE; t.placeholder = KW_NONE; /* NT: not a suit */ }
        else {
            KeywordKind k = classifyWord (rest);
            if (k == KW_NONE)
                return false;
            if (k == KW_BMAJ || k == KW_BMIN)
                fatal ("illegal use of " + std::string (keywordDisplayName (k)) +
                       " as a bid token in rule name " + name +
                       " — BMAJ/BMIN describe both suits at once and cannot name a single call");
            t.concreteSuit = SUIT_NONE;
            t.placeholder = k;
        }
        tokens.push_back (t);
    }
    return true;
}

// ── Anchor-order validation (Maj must precede OMaj; Min must precede OMin) ──

void checkAnchorOrder (const std::string& ruleLabel, const std::string& name,
                        const std::vector<BidNameToken>& nameTokens, bool nameIsBidSequence,
                        const std::string& body)
{
    bool majSeen = false, minSeen = false;
    auto consider = [&] (KeywordKind k) {
        if (k == KW_MAJ)  majSeen = true;
        else if (k == KW_MIN) minSeen = true;
        else if (k == KW_OMAJ && !majSeen)
            fatal ("OMAJ/OMAJOR used before a preceding MAJ/MAJOR in rule " + ruleLabel +
                   " (\"" + name + "\") — OMAJ only has meaning relative to an already-established MAJ");
        else if (k == KW_OMIN && !minSeen)
            fatal ("OMIN/OMINOR used before a preceding MIN/MINOR in rule " + ruleLabel +
                   " (\"" + name + "\") — OMIN only has meaning relative to an already-established MIN");
    };
    if (nameIsBidSequence)
        for (const auto& t : nameTokens)
            if (t.placeholder != KW_NONE)
                consider (t.placeholder);
    size_t pos = 0;
    KeywordHit hit;
    while (findNextKeywordWord (body, pos, hit)) {
        if (isMajorPair (hit.kind) || isMinorPair (hit.kind))
            consider (hit.kind);
        pos = hit.pos + hit.len;
    }
}

// ── BMaj/BMin local, paren-matched substitution ────────────────────────────

// Requires every BMaj/BMin occurrence to be the sole content of its own
// enclosing parens: "(BMaj <op> <value>)". Replaces the whole parenthesized
// unit with "((Spades <op> <value>) AND (Hearts <op> <value>))" (or the
// Diamonds/Clubs equivalent for BMin). Operates independently of any
// Maj/Min substitution — BMaj/BMin never need an anchor.
std::string substituteBothMacros (const std::string& body, const std::string& ruleLabel)
{
    std::string result;
    size_t pos = 0;
    while (true) {
        KeywordHit hit;
        if (!findNextKeywordWord (body, pos, hit)) {
            result += body.substr (pos);
            break;
        }
        if (hit.kind != KW_BMAJ && hit.kind != KW_BMIN) {
            result += body.substr (pos, hit.pos + hit.len - pos);
            pos = hit.pos + hit.len;
            continue;
        }
        // Require an immediately preceding '(' (skipping only whitespace).
        long open = (long)hit.pos - 1;
        while (open >= 0 && isspace ((unsigned char)body[(size_t)open]))
            open--;
        if (open < 0 || body[(size_t)open] != '(')
            fatal (std::string (keywordDisplayName (hit.kind)) + " in rule " + ruleLabel +
                   " must be written as its own parenthesized comparison, e.g. \"(" +
                   keywordDisplayName (hit.kind) + " > 3)\"");
        // Copy everything up to (but not including) the '(' we found.
        result += body.substr (pos, (size_t)open - pos);
        // Find the matching ')' for this '('.
        size_t depth = 0, i = (size_t)open;
        size_t closeParen = std::string::npos;
        for (; i < body.size(); i++) {
            if (body[i] == '(')
                depth++;
            else if (body[i] == ')') {
                depth--;
                if (depth == 0) { closeParen = i; break; }
            }
        }
        if (closeParen == std::string::npos)
            fatal ("unmatched '(' around " + std::string (keywordDisplayName (hit.kind)) + " in rule " + ruleLabel);
        // Inner content is "<op> <value>" following the keyword, up to closeParen.
        std::string inner = body.substr (hit.pos + hit.len, closeParen - (hit.pos + hit.len));
        // Must not contain any further top-level '(' — i.e. nothing besides the operator/value.
        Suit s1 = (hit.kind == KW_BMAJ) ? SPADES   : DIAMONDS;
        Suit s2 = (hit.kind == KW_BMAJ) ? HEARTS   : CLUBS;
        result += "((" + std::string (suitWord (s1)) + inner + ") AND (" + std::string (suitWord (s2)) + inner + "))";
        pos = closeParen + 1;
    }
    return result;
}

// ── Maj/OMaj/Min/OMin substitution (name tokens + body words) ─────────────

std::string substituteBodyChoice (const std::string& body, Suit majorChoice, Suit minorChoice)
{
    std::string result;
    size_t pos = 0;
    KeywordHit hit;
    while (findNextKeywordWord (body, pos, hit)) {
        result += body.substr (pos, hit.pos - pos);
        switch (hit.kind) {
            case KW_MAJ:  result += suitWord (majorChoice); break;
            case KW_OMAJ: result += suitWord (complementSuit (majorChoice)); break;
            case KW_MIN:  result += suitWord (minorChoice); break;
            case KW_OMIN: result += suitWord (complementSuit (minorChoice)); break;
            default:      result += body.substr (hit.pos, hit.len); break; // BMAJ/BMIN untouched here
        }
        pos = hit.pos + hit.len;
    }
    result += body.substr (pos);
    return result;
}

std::string substituteNameTokens (const std::vector<BidNameToken>& tokens, Suit majorChoice, Suit minorChoice)
{
    std::string out = "$.";
    for (const auto& t : tokens) {
        if (t.isPass) {
            out += "P.";
            continue;
        }
        char letter;
        if (t.placeholder == KW_MAJ)       letter = suitLetter (majorChoice);
        else if (t.placeholder == KW_OMAJ) letter = suitLetter (complementSuit (majorChoice));
        else if (t.placeholder == KW_MIN)  letter = suitLetter (minorChoice);
        else if (t.placeholder == KW_OMIN) letter = suitLetter (complementSuit (minorChoice));
        else if (t.concreteSuit != SUIT_NONE) letter = suitLetter (t.concreteSuit);
        else letter = 'N'; // no-trump
        out += t.levelDigit + std::string (1, letter) + ".";
    }
    return out;
}

// ── Bid-sequence legality (ascending rank; Pass exempt) ────────────────────

bool isLegalBidSequence (const std::vector<BidNameToken>& tokens)
{
    bid lastReal = bidInvalid;
    for (const auto& t : tokens) {
        if (t.isPass)
            continue;
        int strain;
        if (t.concreteSuit == CLUBS)        strain = bidClub;
        else if (t.concreteSuit == DIAMONDS) strain = bidDiamond;
        else if (t.concreteSuit == HEARTS)   strain = bidHeart;
        else if (t.concreteSuit == SPADES)   strain = bidSpade;
        else                                 strain = bidNT;
        bid b = makeBid (atoi (t.levelDigit.c_str()), strain);
        if ((lastReal != bidInvalid) && (b <= lastReal))
            return false;
        lastReal = b;
    }
    return true;
}

// ── Line-number map for yyerror() ──────────────────────────────────────────

std::vector<int> g_lineMap;

void addMappedLine (int originalLine)
{
    g_lineMap.push_back (originalLine);
}

} // namespace

int mapExpandedLineToOriginal (int expandedLine)
{
    if (expandedLine < 1 || (size_t)expandedLine > g_lineMap.size())
        return expandedLine;
    return g_lineMap[(size_t)expandedLine - 1];
}

std::string expandMajMinMacros (const std::string& rawText, const char* inFile)
{
    g_lineMap.clear();
    std::vector<Stmt> stmts = splitStatements (rawText);
    std::string expanded;

    for (const Stmt& stmt : stmts) {
        if (stmt.isEndMarker) {
            expanded += "END\n";
            addMappedLine (stmt.line);
            continue;
        }

        std::vector<BidNameToken> nameTokens;
        bool nameIsBidSequence = analyzeBidName (stmt.name, nameTokens);

        checkAnchorOrder (stmt.name, stmt.name, nameTokens, nameIsBidSequence, stmt.body);

        bool majorInName = false, minorInName = false;
        if (nameIsBidSequence)
            for (const auto& t : nameTokens) {
                if (isMajorPair (t.placeholder)) majorInName = true;
                if (isMinorPair (t.placeholder)) minorInName = true;
            }

        bool majorInBody = false, minorInBody = false;
        {
            size_t pos = 0;
            KeywordHit hit;
            while (findNextKeywordWord (stmt.body, pos, hit)) {
                if (isMajorPair (hit.kind)) majorInBody = true;
                if (isMinorPair (hit.kind)) minorInBody = true;
                pos = hit.pos + hit.len;
            }
        }

        std::vector<Suit> majorNameChoices  = majorInName ? std::vector<Suit>{ SPADES, HEARTS }     : std::vector<Suit>{ SUIT_NONE };
        std::vector<Suit> minorNameChoices  = minorInName ? std::vector<Suit>{ DIAMONDS, CLUBS }     : std::vector<Suit>{ SUIT_NONE };
        std::vector<Suit> majorBodyChoices  = (majorInBody && !majorInName) ? std::vector<Suit>{ SPADES, HEARTS } : std::vector<Suit>{ SUIT_NONE };
        std::vector<Suit> minorBodyChoices  = (minorInBody && !minorInName) ? std::vector<Suit>{ DIAMONDS, CLUBS } : std::vector<Suit>{ SUIT_NONE };

        struct Generated { std::string name; std::string body; };
        std::vector<Generated> generated;

        for (Suit majN : majorNameChoices) {
            for (Suit minN : minorNameChoices) {
                std::string newName = nameIsBidSequence
                    ? substituteNameTokens (nameTokens, majN, minN)
                    : stmt.name;

                std::vector<std::string> orParts;
                for (Suit majB : majorBodyChoices) {
                    for (Suit minB : minorBodyChoices) {
                        Suit majorChoice = majorInName ? majN : majB;
                        Suit minorChoice = minorInName ? minN : minB;
                        std::string b = substituteBodyChoice (stmt.body, majorChoice, minorChoice);
                        b = substituteBothMacros (b, stmt.name);
                        orParts.push_back (b);
                    }
                }
                std::string finalBody;
                if (orParts.size() > 1) {
                    finalBody = "(" + orParts[0] + ")";
                    for (size_t k = 1; k < orParts.size(); k++)
                        finalBody += " OR (" + orParts[k] + ")";
                } else {
                    finalBody = orParts[0];
                }
                generated.push_back ({ newName, finalBody });
            }
        }

#ifdef DEBUG2
        if (generated.size() > 1) {
            std::string trace = stmt.name + " ->";
            for (auto& g : generated) trace += " " + g.name + ",";
            fprintf (stderr, "[majMinExpand] %s\n", trace.c_str());
        } else if (generated[0].body != stmt.body) {
            fprintf (stderr, "[majMinExpand] %s: body expanded\n", stmt.name.c_str());
        }
#endif

        for (const Generated& g : generated) {
            std::vector<BidNameToken> genTokens;
            bool genIsBidSeq = analyzeBidName (g.name, genTokens);
            if (genIsBidSeq && !isLegalBidSequence (genTokens)) {
                if (generated.size() > 1) {
                    warn ("dropping illegal bid sequence " + g.name +
                          " (a call must rank higher than every earlier call in the same auction)");
                    continue;
                }
                fatal ("illegal bid sequence in rule " + g.name +
                       ": a call must rank higher than every earlier call in the same auction");
            }
            expanded += g.name + " " + stmt.op + " " + g.body + ";\n";
            addMappedLine (stmt.line);
        }
    }

    std::string expandedFileName = std::string (inFile) + ".expanded.txt";
    FILE* f = fopen (expandedFileName.c_str(), "w");
    if (f) {
        fputs (expanded.c_str(), f);
        fclose (f);
    }

    return expanded;
}
