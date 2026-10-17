#pragma once
// The stitch graph: one node per stitch, one Row per pattern row or round, and
// point-seam links for the attachments the CrochetPARADE grammar cannot write
// (the plan's `through A & B`). This is the in-memory form of the import
// described in docs/CROCHET_DSL.md; the fields mirror the JSON schema of
// docs/CROCHET_PLAN.md section 5.0 exactly, so importing is field-for-field.
// Later tickets add the members they need and use (T4.2: facing/RS; T5.x:
// names, surface, overrides, layout); nothing here is pre-declared for them.
#include <string>
#include <vector>

namespace fl {

// One stitch. Ids are graph positions: st[i].id == i.
struct Stitch {
    int id = -1;               // graph index, equal to the position in StitchGraph::st
    std::string type;          // "ch" | "slst" | "sc" | "hdc" | "dc" | "tr"
    std::string piece;         // piece name; CrochetPARADE exports "main" for every stitch
    int row = -1;              // Row::id of the row or round it belongs to
    int index = -1;            // position in that row, in made order (turning chains included)
    std::string loop;          // as-written (worker-relative) loop: both|blo|flo|fpost|bpost|space
    std::vector<int> targets;  // stitch ids it was worked into, in written order (empty for chains)
    int prev = -1;             // stitch made just before it in the working sequence, -1 at a start
    int line = -1;             // 1-based source line in the pattern file
};

// One row (turned flat work) or round (joined/spiral).
struct Row {
    int id = -1;          // row id; ids are 0..rows.size()-1 and Stitch::row indexes rows
    std::string piece;    // piece name, as in Stitch::piece
    std::string kind;     // "row" | "round"
    bool turned = false;  // the pattern turns the work at the end of this row
};

// A point seam (plan section 6.7): a second attachment a stitch cannot state
// in the exported pattern text, supplied by a `#! link` extension line. For
// both_layers, a is the seam stitch CrochetPARADE attached to one layer and b
// is the partner-layer stitch it could not attach. For join, a is the closing
// sl st of a strap end and b is the strap-end stitch it also sews to; a's own
// target (from the export) is the rim stitch.
struct Link {
    int a = -1;        // the stitch that carries the seam
    int b = -1;        // its second target
    std::string kind;  // "both_layers" | "join"
};

struct StitchGraph {
    std::vector<Stitch> st;
    std::vector<Row> rows;
    std::vector<Link> links;
};

}  // namespace fl
