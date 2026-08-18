#ifndef _TRUMPASKEXPAND_HPP_
#define _TRUMPASKEXPAND_HPP_

#include <string>
#include <vector>

// Two preprocessing passes over a rules file's text, run after
// expandMajMinMacros() (majMinExpand.hpp) and before the file is handed to
// the Flex/Bison parser -- see shared/hand-spec.md's "Trump context" and
// "Ask templates" sections.
//
// Order matters and mirrors why these are two separate functions rather
// than one: a template's own text has no bid-sequence anchor of its own
// (see "@" below), so Trump can only resolve once a template's rules have
// been grafted onto a concrete attachment point. graftAskTemplates() must
// therefore run to completion before resolveTrumpReferences() runs at all.
//
//   text = expandMajMinMacros (rawText, inFile);
//   text = graftAskTemplates  (text, inFile);
//   text = resolveTrumpReferences (text, inFile);
//
// Both take and return ordinary rules-file text in exactly the shape
// expandMajMinMacros() itself emits (one "$Name := body;"/"$Name :& body;"/
// "$Name :| body;" statement or a bare "END" per logical unit) and are
// complete no-ops on any file that uses none of '@', "Trump", ".?.", or
// ":?" -- true by construction, since all four are opt-in text patterns.

// Grafts ask-template declarations onto every node that attaches one.
//
// A declaration looks like "$.?.Name.<relative-bid-tokens>. := expr;" --
// "?" is a reserved first name segment (parallel to how "$ANY" is already
// a reserved full name), consumed here and never emitted to the parser.
// An attachment is a trailing " :? Name" clause on an ordinary rule's body
// (combinable with :=/:&/:| in one statement, or standalone -- a standalone
// "$.Name. :? Template;" means the same as "$.Name. := $ANY :? Template;").
// Grafting recursively synthesizes concrete "$.<attachment-path>.
// <relative-token>." statements from the template's entries, including any
// further template a response itself attaches. It is a fatal error to
// attach an undeclared template, or to attach onto a node that already has
// an explicitly-defined child under the name a template entry would
// produce (a node either takes a template's full response set or defines
// its own children directly -- never both).
std::string graftAskTemplates (const std::string& text, const char* inFile);

// Resolves "@" (a suffix on a bid token in a "$."-name marking it as
// setting Trump to its own suit) and "Trump<suffix>" (a prefix that
// substitutes to the resolved suit's letter, recombining with whatever
// suffix follows -- e.g. "Trumpkcs" -> "Hkcs" -- or, at a word boundary, to
// the full suit-name keyword). Anchor scoping is local to each statement's
// own full text (name, then body, left to right) -- the same rule
// majMinExpand.cpp's checkAnchorOrder() already applies to Maj-before-OMaj
// -- so it is a fatal error for "Trump" to appear with no "@" anywhere
// earlier in that statement's own text (no cross-statement/ancestor
// inheritance; the workaround is to repeat "@" at the same token position
// in a longer name). Also a fatal error for "@" attached to an "N" or "P"
// token (no suit to resolve). Strips all "@" markers from the emitted
// text -- they must never reach the real parser.
std::string resolveTrumpReferences (const std::string& text, const char* inFile);

// Names of ask-templates that were declared (via "$.?.Name....") but never
// attached (via ":?") anywhere in the file most recently processed by
// graftAskTemplates() -- consumed by bidlab's --validate for its
// UNUSED-TEMPLATE finding. Valid only after a graftAskTemplates() call;
// empty if none were declared or all were attached.
const std::vector<std::string>& unusedAskTemplateNames ();

#endif  // _TRUMPASKEXPAND_HPP_
