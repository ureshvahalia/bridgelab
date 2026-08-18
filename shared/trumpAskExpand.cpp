// Preprocessing passes that graft ask-template declarations onto their
// attachment points and resolve Trump/'@' references into ordinary,
// concrete rule definitions before the file is handed to the Flex/Bison
// parser. See shared/trumpAskExpand.hpp and shared/hand-spec.md.
//
// Written in the same std::string/std::vector style as shared/majMinExpand.cpp
// for the same reason: this is new, self-contained, inherently variable-
// length text-processing code, not the fixed-size char-buffer style used
// elsewhere in this codebase.

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <set>
#include "majMinExpand.hpp"
#include "majMinExpandInternal.hpp"
#include "trumpAskExpand.hpp"
#include "log.h"

namespace {

// ── Fatal / warning reporting (mirrors shared/majMinExpand.cpp's) ─────────

[[noreturn]] void fatal (const std::string& msg)
{
    logError ("%s\n", msg.c_str());
    exit (1);
}

// ── Statement splitting ────────────────────────────────────────────────────
//
// Unlike majMinExpand.cpp's splitStatements(), this one only ever sees
// text that module already produced (or that this module itself re-emits),
// which is always in the canonical "$Name OP body;\n" / "END\n" shape it
// serializes to (see serialize() below) -- comments and original source
// whitespace are already gone by the time either pass here runs. It adds
// one thing majMinExpand's splitter doesn't need: recognizing a bare ":?"
// right after the name (the standalone ask-template attachment form),
// which it desugars immediately to "$Name := $ANY :? Template;" so every
// later stage only ever has to handle one shape (an ordinary op, with the
// attachment -- if any -- folded into the body text, split back out by
// splitTrailingAttach()).

struct Stmt {
    int line;
    std::string name;   // e.g. "$.1H.3H@.4N." or "$.?.RKCB.5C."; empty for END
    std::string op;     // ":=", ":&", or ":|" -- always one of these three
    std::string body;
    bool isEndMarker;
};

bool isNameChar (char c)
{
    return isalnum ((unsigned char) c) || c=='_' || c=='-' || c=='.' || c=='?' || c=='@';
}

// Case-insensitive fixed-length compare at a given position (no word-
// boundary check -- callers that need one do it themselves, since the two
// use sites here want different boundary rules: whole-word for "END", and
// a prefix-only end boundary for "Trump").
bool matchesCI (const std::string& text, size_t pos, const char* word, size_t len)
{
    if (pos + len > text.size())
        return false;
    for (size_t k = 0; k < len; k++)
        if (toupper ((unsigned char) text[pos + k]) != toupper ((unsigned char) word[k]))
            return false;
    return true;
}

bool atWholeWordCI (const std::string& text, size_t pos, const char* word)
{
    size_t len = strlen (word);
    if (!matchesCI (text, pos, word, len))
        return false;
    if (pos > 0 && isalnum ((unsigned char) text[pos - 1]))
        return false;
    if (pos + len < text.size() && isalnum ((unsigned char) text[pos + len]))
        return false;
    return true;
}

std::vector<Stmt> splitStatements (const std::string& text, const char* inFile)
{
    std::vector<Stmt> out;
    size_t i = 0, n = text.size();
    int line = 1;

    auto skipWs = [&] () {
        while (i < n && isspace ((unsigned char) text[i])) {
            if (text[i] == '\n') line++;
            i++;
        }
    };

    while (true) {
        skipWs();
        if (i >= n)
            break;
        int stmtLine = line;
        if (text[i] == '$') {
            size_t start = i;
            i++;
            while (i < n && isNameChar (text[i]))
                i++;
            std::string name = text.substr (start, i - start);
            skipWs();

            if (i + 1 < n && text[i] == ':' && text[i + 1] == '?') {
                // Standalone attach form: "$Name. :? Template;"
                i += 2;
                skipWs();
                size_t tstart = i;
                while (i < n && (isalnum ((unsigned char) text[i]) || text[i] == '_'))
                    i++;
                std::string tname = text.substr (tstart, i - tstart);
                if (tname.empty())
                    fatal (std::string (inFile) + ": expected an ask-template name after \":?\" for " +
                           name + " at line " + std::to_string (stmtLine));
                skipWs();
                if (i >= n || text[i] != ';')
                    fatal (std::string (inFile) + ": expected \";\" after \":? " + tname + "\" for " +
                           name + " at line " + std::to_string (stmtLine));
                i++;
                out.push_back ({ stmtLine, name, ":=", "$ANY :? " + tname, false });
                continue;
            }

            if (!(i + 1 < n && text[i] == ':' &&
                  (text[i + 1] == '=' || text[i + 1] == '&' || text[i + 1] == '|')))
                fatal (std::string (inFile) + ": expected \":=\", \":&\", \":|\", or \":?\" after rule name " +
                       name + " at line " + std::to_string (stmtLine));
            std::string op = text.substr (i, 2);
            i += 2;
            skipWs();
            size_t bodyStart = i;
            int depth = 0;
            while (i < n) {
                char c = text[i];
                if (c == '(' || c == '[') depth++;
                else if (c == ')' || c == ']') depth--;
                else if (c == '\n') line++;
                if (depth == 0 && c == ';')
                    break;
                i++;
            }
            std::string body = text.substr (bodyStart, i - bodyStart);
            while (!body.empty() && isspace ((unsigned char) body.back()))
                body.pop_back();
            if (i < n && text[i] == ';')
                i++;
            out.push_back ({ stmtLine, name, op, body, false });
        } else if (atWholeWordCI (text, i, "END")) {
            i += 3;
            out.push_back ({ stmtLine, "", "", "", true });
        } else {
            fatal (std::string (inFile) + ": internal error -- unexpected content at line " +
                   std::to_string (stmtLine) + " while grafting ask-templates/resolving Trump");
        }
    }
    return out;
}

std::string serialize (const std::vector<Stmt>& stmts)
{
    std::string out;
    for (const Stmt& s : stmts) {
        if (s.isEndMarker)
            out += "END\n";
        else
            out += s.name + " " + s.op + " " + s.body + ";\n";
    }
    return out;
}

// ── Splitting a trailing " :? Template" attachment off a body ─────────────
//
// Applies to both a real rule's body ("(Points>=12) :? RKCB" -> "(Points>=12)",
// "RKCB") and, recursively, a template entry's own body (an entry can
// itself attach a further template -- see graftAt() below). The ":?" must
// be at paren-depth 0.

void splitTrailingAttach (const std::string& body, std::string& realBody, std::string& attached)
{
    int depth = 0;
    for (size_t i = 0; i + 1 < body.size(); i++) {
        char c = body[i];
        if (c == '(' || c == '[') depth++;
        else if (c == ')' || c == ']') depth--;
        else if (depth == 0 && c == ':' && body[i + 1] == '?') {
            realBody = body.substr (0, i);
            while (!realBody.empty() && isspace ((unsigned char) realBody.back()))
                realBody.pop_back();
            size_t j = i + 2;
            while (j < body.size() && isspace ((unsigned char) body[j]))
                j++;
            size_t tstart = j;
            while (j < body.size() && (isalnum ((unsigned char) body[j]) || body[j] == '_'))
                j++;
            attached = body.substr (tstart, j - tstart);
            return;
        }
    }
    realBody = body;
    attached.clear();
}

// ── Ask-template declarations: "$.?.Name.<relative-bid-tokens>." ──────────

struct TemplateEntry {
    std::string relativeName;      // dot-joined relative tokens, no leading/trailing dot, e.g. "5C" or "5N.6D"
    std::string bodyText;
    std::string attachedTemplate;  // name of a nested template this entry itself attaches; "" if none
};

// Returns false if `name` isn't shaped "$.?.Name.<tokens>." at all (an
// ordinary or real bid-sequence rule) -- once the "$.?." prefix matches,
// though, this is unambiguously a template declaration (no ordinary rule
// name can start that way; '?' isn't a legal DEFNAME character), so every
// further structural problem is reported as a fatal error here rather than
// falling through to a confusing downstream parser error.
bool parseTemplateDeclName (const std::string& name, const char* inFile, int origLine,
                             std::string& templateName, std::string& relativePath)
{
    if (name.size() < 5 || name[0] != '$' || name[1] != '.' || name[2] != '?' || name[3] != '.')
        return false;
    size_t dot = name.find ('.', 4);
    if (dot == std::string::npos)
        fatal (std::string (inFile) + ": malformed ask-template declaration " + name +
               " (near line " + std::to_string (mapExpandedLineToOriginal (origLine)) + ")");
    templateName = name.substr (4, dot - 4);
    if (templateName.empty())
        fatal (std::string (inFile) + ": ask-template declaration " + name +
               " is missing a name after \"$.?.\" (near line " +
               std::to_string (mapExpandedLineToOriginal (origLine)) + ")");
    if (name.back() != '.')
        fatal (std::string (inFile) + ": ask-template declaration " + name +
               " must end with \".\", like any bid-sequence name (near line " +
               std::to_string (mapExpandedLineToOriginal (origLine)) + ")");
    size_t relStart = dot + 1;
    relativePath = name.substr (relStart, name.size() - 1 - relStart);
    if (relativePath.empty())
        fatal (std::string (inFile) + ": ask-template " + templateName + " (" + name +
               ") declares no relative bid tokens (near line " +
               std::to_string (mapExpandedLineToOriginal (origLine)) + ")");
    return true;
}

// ── Grafting ────────────────────────────────────────────────────────────

struct GraftContext {
    const std::map<std::string, std::vector<TemplateEntry>>* templates;
    std::set<std::string>* existingNames;
    std::set<std::string>* referenced;
    std::vector<Stmt>* output;
    const char* inFile;
};

void graftAt (GraftContext& ctx, const std::string& attachPath, const std::string& templateName, int line)
{
    ctx.referenced->insert (templateName);
    auto it = ctx.templates->find (templateName);
    if (it == ctx.templates->end())
        fatal (std::string (ctx.inFile) + ": \":? " + templateName + "\" (near line " +
               std::to_string (mapExpandedLineToOriginal (line)) +
               ") references an undeclared ask-template -- no \"$.?." + templateName +
               ".\" declaration found");
    for (const TemplateEntry& e : it->second) {
        std::string newName = attachPath + e.relativeName + ".";
        if (ctx.existingNames->count (newName))
            fatal (std::string (ctx.inFile) + ": ask-template " + templateName + " attached at " +
                   attachPath + " (near line " + std::to_string (mapExpandedLineToOriginal (line)) +
                   ") would define " + newName + ", but that rule already has an explicit "
                   "definition -- a node may not mix an attached template with locally-defined children");
        if (!isWellFormedConcreteBidSequenceName (newName))
            fatal (std::string (ctx.inFile) + ": grafting ask-template " + templateName + " at " +
                   attachPath + " (near line " + std::to_string (mapExpandedLineToOriginal (line)) +
                   ") produces an illegal bid sequence " + newName +
                   " (a call must rank higher than every earlier call in the same auction)");
        ctx.existingNames->insert (newName);
        ctx.output->push_back ({ line, newName, ":=", e.bodyText, false });
        if (!e.attachedTemplate.empty())
            graftAt (ctx, newName, e.attachedTemplate, line);
    }
}

// ── Trump anchor resolution ────────────────────────────────────────────────

const char* suitWordFor (char letter)
{
    switch (letter) {
        case 'S': return "Spades";
        case 'H': return "Hearts";
        case 'D': return "Diamonds";
        case 'C': return "Clubs";
        default:  return "";
    }
}

// Strips every '@' marker from `name` (bid tokens only ever appear in a
// rule's own name, never its body, so this is the only place '@' can
// occur) and reports the suit resolved by the LAST marked token -- there's
// nothing "between" the name and the body that could set a different
// anchor, so the last one in the name is, by construction, the nearest
// preceding anchor for every position in this statement's own body. Fatal
// if a marked token doesn't name a real suit (Pass or notrump).
std::string stripAndFindAnchor (const std::string& name, const char* inFile, int origLine,
                                 char& anchorLetter, bool& haveAnchor)
{
    anchorLetter = 0;
    haveAnchor = false;
    if (name.size() < 2 || name[0] != '$' || name[1] != '.')
        return name;   // not a bid-sequence name -- '@' can't appear, nothing to do

    std::string out = "$.";
    size_t i = 2, n = name.size();
    while (i < n) {
        size_t dot = name.find ('.', i);
        if (dot == std::string::npos) {
            out += name.substr (i);
            break;
        }
        std::string seg = name.substr (i, dot - i);
        i = dot + 1;
        bool marked = !seg.empty() && seg.back() == '@';
        if (marked)
            seg.pop_back();
        if (marked) {
            std::string up = seg;
            for (char& c : up) c = (char) toupper ((unsigned char) c);
            char suitCh = 0;
            if (up.size() == 2 && isdigit ((unsigned char) up[0])) {
                char s = up[1];
                if (s == 'C' || s == 'D' || s == 'H' || s == 'S')
                    suitCh = s;
            }
            if (suitCh == 0)
                fatal (std::string (inFile) + ": '@' on \"" + seg + "\" in " + name +
                       " (near line " + std::to_string (mapExpandedLineToOriginal (origLine)) +
                       ") -- '@' must mark a real suit call, not Pass or notrump");
            anchorLetter = suitCh;
            haveAnchor = true;
        }
        out += seg + ".";
    }
    return out;
}

// Substitutes every "Trump"-prefixed occurrence in `body`: followed by
// another identifier character, the resolved suit's letter (so
// "Trumpkcs" -> "Hkcs", recombining with whatever suffix follows, no
// suffix vocabulary needed here -- an unknown suffix just surfaces as an
// ordinary "unknown keyword" error from the real parser, same as a
// hand-typed typo); at a word boundary, the full suit-name keyword (so
// bare "Trump" -> "Hearts", mirroring why bare Maj/Min already substitute
// to the word rather than a lone letter -- a lone letter isn't a valid
// standalone keyword).
std::string substituteTrumpInBody (const std::string& body, char anchorLetter, bool haveAnchor,
                                    const std::string& ruleLabel, const char* inFile, int origLine)
{
    std::string result;
    size_t i = 0, n = body.size();
    while (i < n) {
        if (body[i] == '$') {
            // DEFNAME reference -- skip whole, mirrors majMinExpand.cpp's
            // identical treatment; "Trump" is never a macro token inside one.
            result += body[i++];
            while (i < n && (isalnum ((unsigned char) body[i]) || body[i] == '_' ||
                              body[i] == '-' || body[i] == '.'))
                result += body[i++];
            continue;
        }
        bool boundaryOk = (i == 0) || (!isalnum ((unsigned char) body[i - 1]) && body[i - 1] != '_');
        if (boundaryOk && isalpha ((unsigned char) body[i]) && matchesCI (body, i, "TRUMP", 5)) {
            if (!haveAnchor)
                fatal (std::string (inFile) + ": \"Trump\" referenced in " + ruleLabel +
                       " (near line " + std::to_string (mapExpandedLineToOriginal (origLine)) +
                       ") but no '@' establishes Trump anywhere in this rule's own name");
            bool followedByIdentChar = (i + 5 < n) &&
                (isalnum ((unsigned char) body[i + 5]) || body[i + 5] == '_');
            if (followedByIdentChar)
                result += anchorLetter;
            else
                result += suitWordFor (anchorLetter);
            i += 5;
            continue;
        }
        result += body[i++];
    }
    return result;
}

std::vector<std::string> g_unusedTemplates;

// Diagnostic mirror of majMinExpand.cpp's own "<inFile>.expanded.txt" (a
// different filename so it never collides with -- or gets diffed against
// in run_tests.sh's Maj/Min regression check -- that file): the fully
// resolved text resolveTrumpReferences() hands back to parse_rules.cpp,
// for inspecting what these two passes actually produced.
void writeTrumpAskDebugFile (const std::string& text, const char* inFile)
{
    std::string fileName = std::string (inFile) + ".trumpask.expanded.txt";
    FILE* f = fopen (fileName.c_str(), "w");
    if (f) {
        fputs (text.c_str(), f);
        fclose (f);
    }
}

} // namespace

std::string graftAskTemplates (const std::string& text, const char* inFile)
{
    g_unusedTemplates.clear();
    std::vector<Stmt> stmts = splitStatements (text, inFile);

    std::map<std::string, std::vector<TemplateEntry>> templates;
    std::set<std::string> declared;
    std::vector<Stmt> remaining;
    remaining.reserve (stmts.size());

    for (Stmt& stmt : stmts) {
        if (stmt.isEndMarker) { remaining.push_back (stmt); continue; }
        std::string templateName, relativePath;
        if (parseTemplateDeclName (stmt.name, inFile, stmt.line, templateName, relativePath)) {
            if (stmt.op != ":=")
                fatal (std::string (inFile) + ": ask-template entry " + stmt.name +
                       " must use \":=\" (near line " + std::to_string (mapExpandedLineToOriginal (stmt.line)) +
                       ") -- \":&\"/\":|\" refine an existing rule, which doesn't apply to a template entry");
            declared.insert (templateName);
            std::string realBody, attached;
            splitTrailingAttach (stmt.body, realBody, attached);
            std::string relAsName = "$." + relativePath + ".";
            if (!isWellFormedConcreteBidSequenceName (relAsName))
                fatal (std::string (inFile) + ": ask-template " + templateName +
                       "'s relative sequence \"" + relativePath + "\" (near line " +
                       std::to_string (mapExpandedLineToOriginal (stmt.line)) +
                       ") is not a legally-ranked bid sequence");
            for (const TemplateEntry& existing : templates[templateName])
                if (existing.relativeName == relativePath)
                    fatal (std::string (inFile) + ": ask-template " + templateName +
                           " declares \"" + relativePath + "\" more than once (near line " +
                           std::to_string (mapExpandedLineToOriginal (stmt.line)) + ")");
            templates[templateName].push_back ({ relativePath, realBody, attached });
            continue;
        }
        remaining.push_back (stmt);
    }

    std::set<std::string> existingNames;
    for (const Stmt& s : remaining)
        if (!s.isEndMarker)
            existingNames.insert (s.name);

    std::set<std::string> referenced;
    std::vector<Stmt> output;
    output.reserve (remaining.size());
    GraftContext ctx { &templates, &existingNames, &referenced, &output, inFile };

    for (const Stmt& stmt : remaining) {
        if (stmt.isEndMarker) { output.push_back (stmt); continue; }
        std::string realBody, attached;
        splitTrailingAttach (stmt.body, realBody, attached);
        Stmt cleaned = stmt;
        cleaned.body = realBody;
        output.push_back (cleaned);
        if (!attached.empty())
            graftAt (ctx, stmt.name, attached, stmt.line);
    }

    for (const std::string& name : declared)
        if (!referenced.count (name))
            g_unusedTemplates.push_back (name);

    return serialize (output);
}

std::string resolveTrumpReferences (const std::string& text, const char* inFile)
{
    std::vector<Stmt> stmts = splitStatements (text, inFile);
    std::vector<Stmt> output;
    output.reserve (stmts.size());
    for (Stmt& stmt : stmts) {
        if (stmt.isEndMarker) { output.push_back (stmt); continue; }
        char anchorLetter = 0;
        bool haveAnchor = false;
        std::string strippedName = stripAndFindAnchor (stmt.name, inFile, stmt.line, anchorLetter, haveAnchor);
        std::string newBody = substituteTrumpInBody (stmt.body, anchorLetter, haveAnchor, stmt.name,
                                                       inFile, stmt.line);
        output.push_back ({ stmt.line, strippedName, stmt.op, newBody, false });
    }
    std::string result = serialize (output);
    writeTrumpAskDebugFile (result, inFile);
    return result;
}

const std::vector<std::string>& unusedAskTemplateNames ()
{
    return g_unusedTemplates;
}
