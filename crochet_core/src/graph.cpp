// Resolved graph passes that the importer runs on every import: RS/WS facing
// and RS-relative loop insertion (plan section 5.5), and the point seams the
// mesh stage builds from the `#! link` attachments (section 6.7). The graph
// types and the rules are documented in graph.hpp and docs/CROCHET_DSL.md.
#include "failoring/crochet/graph.hpp"

#include <stdexcept>
#include <string>

#include "failoring/error.hpp"

namespace fl {

void resolveFacing(StitchGraph& g) {
    const size_t n = g.rows.size();
    if (n == 0) return;

    // A row-0 that has stitch nodes and every one of them of type `ch` is the
    // foundation chain: its `turn` is part of working back along the chain and
    // must not flip row 1 ("row 1 is RS by default", plan section 5.5).
    bool foundation = !g.st.empty();
    int row0Nodes = 0;
    for (const Stitch& s : g.st) {
        if (s.row != 0) continue;
        ++row0Nodes;
        if (s.type != "ch") {
            foundation = false;
            break;
        }
    }
    foundation = foundation && row0Nodes > 0;

    // rs(0) = true; every later row flips when the row before it turned.
    std::vector<char> rs(n, 1);
    for (size_t r = 1; r < n; ++r) {
        const bool flip = r == 1 ? (foundation ? false : g.rows[0].turned) : g.rows[r - 1].turned;
        rs[r] = flip ? !rs[r - 1] : rs[r - 1];
    }

    for (Stitch& s : g.st) {
        if (s.row < 0 || s.row >= static_cast<int>(n))
            FL_THROW(std::runtime_error, "resolveFacing: stitch " + std::to_string(s.id) + " has row " +
                                             std::to_string(s.row) + ", but the graph has " + std::to_string(n) + " rows");
        s.rs = rs[s.row] != 0;
        // `blo` and `flo` are worker-relative as written, so on a WS row the
        // back and front loops swap; both/fpost/bpost/space copy through.
        if (!s.rs && s.loop == "blo") s.loopRs = "flo";
        else if (!s.rs && s.loop == "flo") s.loopRs = "blo";
        else s.loopRs = s.loop;
    }
}

std::vector<PointSeam> pointSeams(const StitchGraph& g) {
    std::vector<PointSeam> out;
    out.reserve(g.links.size());
    for (const Link& l : g.links) {
        const Stitch& seam = g.st[l.a];  // link endpoints are validated at import
        if (seam.targets.empty())
            FL_THROW(std::runtime_error, "point seam: seam stitch " + std::to_string(l.a) +
                                             " has no target in the export (the link needs its primary attachment)");
        for (const int t : seam.targets)
            if (t == l.b)
                FL_THROW(std::runtime_error, "point seam: stitch " + std::to_string(l.b) + " is already a target of seam stitch " +
                                                 std::to_string(l.a) + " (the link must be the missing second attachment)");
        out.push_back(PointSeam{l.a, l.b, l.kind});
    }
    return out;
}

}  // namespace fl
