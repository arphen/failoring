#pragma once
// Importer for the CrochetPARADE bridge (decision B, docs/CROCHET_PLAN.md
// section 5.0): the JSON produced by tools/crochet/cp_export.mjs plus, for the
// things the CrochetPARADE grammar cannot express, `#!` extension lines in the
// pattern file itself. The syntax is pinned in docs/CROCHET_DSL.md.
//
// JSON (restated from the plan's section 5.0 schema):
//   {"stitches": [{"id", "type", "piece", "row", "index", "loop", "targets",
//                  "prev", "line"}],
//    "rows":     [{"id", "piece", "kind", "turned"}],
//    "links":    [{"a", "b", "kind"}],
//    "positions":[[x, y, z], ...],
//    "errors":   [{"line", "msg"}]}
// "positions" (CrochetPARADE's 3D layout) is optional and ignored here; T5.3
// reads it. A non-empty "errors" array aborts the import with its first entry.
// Every stitch id must equal its position, every target must be an existing
// id below the stitch's own, prev must be -1 or existing, stitch rows and row
// ids must cover 0..N-1, loop must be one of both|blo|flo|fpost|bpost|space,
// row kind must be row|round, link kind must be both_layers|join, and a
// stitch may appear in at most one link.
//
// Extension lines: every line in the pattern file whose first two characters
// are `#!` is one. CrochetPARADE treats `#` as a comment start, so these lines
// are invisible to its parser, and comment lines never shift row numbers or
// source-line numbers (the export's `line` values are stable). Three keywords:
//
//   #! link [row,pos] [row,pos] <kind>     kind = both_layers | join
//   #! count [row] N
//   #! override <address> <modifier>       address = [row] | [row,pos] | [row,a-b]
//
// [row,pos] addresses a stitch by its exported row id and its index within
// that row, exactly the values in the JSON. Both addresses must name existing
// stitches. Whitespace between the tokens is free, but an address is one token:
// no spaces around its comma (`[51,0]`, not `[51, 0]`). The keyword is
// whitespace-tolerated after `#!`. The link is appended in written
// order: a = the first address, b = the second, nothing is sorted. For
// both_layers, a is the seam stitch the export attached to one layer and b is
// the partner-layer stitch; for join, a is the closing sl st of a strap end
// and b is the strap-end stitch (a's own export target is the rim stitch).
//
// `#! count` asserts the number of stitch nodes in the exported row `[row]`
// (one number in brackets, not the `[row,pos]` stitch address): exactly the
// stitches whose `row` is that row, turning chains and join sl sts included.
// A mismatch throws `crochet extension line <line>: row [<r>] has <A>
// stitches, the pattern says <N>`; a nonexistent row, arity or address errors
// throw before that. Every error carries the physical line number and the
// offending keyword, token or address.
//
// `#! override` is the "lego" edit of the chart (plan 9.2): it sets the loop (both | blo | flo | fpost |
// bpost | space, as written, i.e. worker-relative) or the type (sc | hdc | dc | tr | slst) of every stitch of
// the row, of the stitch at [row,pos], or of the stitches a..b (indices as in `[row,pos]`) of the row. Lines
// apply in written order, the last one wins. It never changes which stitches exist, what they are worked into
// or how many there are, so counts and links stay valid; chains are skipped (an address that names only
// chains is an error, as is one that names no stitch, an unknown modifier or a range with a > b).
#include <string>

#include "failoring/crochet/graph.hpp"

namespace fl {

// Import the export JSON, then apply the `#!` lines of extensionText (empty =
// none), then resolve RS/WS facing and RS-relative loop insertion for every
// stitch (resolveFacing, plan section 5.5). Throws std::runtime_error on
// malformed JSON/UTF-8, on schema violations and on extension-line errors,
// always naming the line, stitch or address involved.
StitchGraph importPattern(const std::string& jsonText, const std::string& extensionText = {});

// Read both files and call importPattern. patternPath may be empty, then no
// extension lines are read. Convenience for tests and tools.
StitchGraph importPatternFile(const std::string& jsonPath, const std::string& patternPath = {});

}  // namespace fl
