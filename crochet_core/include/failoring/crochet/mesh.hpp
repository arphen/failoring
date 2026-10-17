#pragma once
// From the stitch graph to a simulation mesh, topology part (docs/CROCHET_PLAN.md
// sections 6.1-6.4, ticket T5.1): which stitch nodes become mesh vertices, which
// triangles join them, each triangle's rest shape and the consistent orientation.
// Materials, hinge modifiers and seams (T5.2) and the initial 3D positions (T5.3)
// are not here. Everything is SI (metres); the only input that carries a size is
// the per-stitch StitchDims vector (graphDims builds it from the stitch model).
//
// Decisions recorded for T5.1 (the plan leaves them open):
//
//  - Nodes. One vertex per stitch node that ends up in a face. A closing sl st
//    (the last stitch of a round whose only target is a stitch of its own round,
//    `ss@[%,0]`) is not a node: it makes the round's closing course edge instead
//    (plan 6.7). Turning chains and any other target-less chain at a row start
//    or end, the chain `sk` consumed, and surface sl sts end up in no face; they
//    are dropped, and dropTo names the vertex that takes their yarn mass (the
//    nearest kept stitch over the graph's prev/target links), for T5.2's
//    IntrinsicMesh::extraMass.
//  - Faces are built per pair of consecutive anchors of one row (anchor = a
//    stitch with targets that is not a closing sl st), in made order, so edge
//    face counts reflect the surface built so far. A pair (p, s) with target-less
//    chains between them uses them as part of the top polyline. The base path is
//    a BFS over edges in fewer than two faces, at most 6 edges (plan 6.2).
//    Foundation chains join the graph as chain edges (prev links between two
//    target-less chains), so the first row's base is the chain.
//  - A round (CrochetPARADE `kind: "round"`) also pairs its last anchor with its
//    first one, but only when their base nodes are the same node or neighbours
//    (BFS path of at most one edge). A "round" whose ends are further apart is an
//    open row (the pouch border, the 5-sl-st strap-end join) and gets no
//    closing face.
//  - A pair whose base nodes no boundary path connects is never forced:
//      surface   an all-sl-st pair whose base nodes ARE connected, but only
//                through edges already in two faces (a surface sl st row lies on
//                the fabric): no face, no warning; the stitches end up dropped;
//      jump      the pair has no path at all, but one of its two stitches carries
//                a `#! link`: the point seam supplies the connection (the pouch
//                border at the fold): no face, recorded in CrochetMesh::jumps;
//      hole      anything else: no face and a warning "hole between stitch X and
//                Y". A fixture must produce none.
//  - Rest shapes follow plan 6.3 exactly, laid out per polygon with x = the
//    working direction (p -> s) and y up the wale; the corners are stored as
//    (u, v) = (y, x). Edge lengths for the base come from the rest lengths of
//    the edges as first created: course edge = width of the stitch at its right
//    end, leg (wale) edge = height of the upper stitch, chain edge = ch width.
//  - Orientation (plan 6.4). Each polygon is built counter-clockwise in its local
//    (x, y). A BFS over the triangles across manifold edges flips a neighbour
//    that traverses the shared edge in the same direction: it swaps vertices 1
//    and 2 AND mirrors the course coordinate of its rest corners, so that
//    afterwards every triangle's rest corners wind the same way as its vertices.
//    The triangle that was built first (row 1's first face) keeps its winding;
//    its normal (counter-clockwise, right-hand rule) is the RS normal, i.e. in
//    a flat layout with row 1 progressing along +x and the wale along +y it
//    points to +z. T5.3's layouts place faces so that this holds.
#include <array>
#include <string>
#include <vector>

#include "failoring/crochet/graph.hpp"
#include "failoring/crochet/stitch_model.hpp"
#include "failoring/crochet/yarn.hpp"
#include "failoring/math.hpp"

namespace fl {

// A pair of consecutive stitches that no face connects because a point seam
// does (see the "jump" case above).
struct MeshJump {
    int a = -1, b = -1;  // stitch ids, in made order
};

struct CrochetMesh {
    // Vertices.
    std::vector<int> vertexStitch;  // vertex -> stitch id (increasing)
    std::vector<int> stitchVertex;  // stitch id -> vertex, -1 when the stitch is in no face
    std::vector<int> dropTo;        // stitch id -> vertex that takes its yarn mass (itself for a kept stitch)

    // Triangles. tris are vertex ids, consistently oriented (see the header comment).
    std::vector<std::array<int, 3>> tris;
    std::vector<std::array<Vec2, 3>> triRest;  // rest corners (u = wale, v = course), metres
    std::vector<int> triSource;                // stitch id whose material the triangle gets
    std::vector<int> triComponent;             // connected-component index (through shared edges)
    int components = 0;

    // Diagnostics. A fixture must have no warnings.
    std::vector<std::string> warnings;
    std::vector<MeshJump> jumps;
};

// Per-stitch size (metres), indexed by stitch id, from the stitch model. The
// loop used is the RS-relative one (Stitch::loopRs). calibration may be null.
std::vector<StitchDims> graphDims(const StitchGraph& g, const Yarn& yarn, Real hook_m, Real tension,
                                  const Calibration* calibration);

// Build the topology. dims must have one entry per stitch. Throws
// std::runtime_error on a malformed input (wrong dims size, a surface that
// cannot be oriented, naming the stitch ids).
CrochetMesh buildMeshTopology(const StitchGraph& g, const std::vector<StitchDims>& dims);

}  // namespace fl
