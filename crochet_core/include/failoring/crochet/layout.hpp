#pragma once
// Initial 3D positions for a CrochetModel (docs/CROCHET_PLAN.md section 6.8,
// ticket T5.3). The layout is only a guess the relaxation (6.9) improves: it
// must be topologically right (no tangling, the fold on the right side, the strap
// a loop above the opening) and roughly the right size; the rest shapes in the
// IntrinsicMesh decide the final geometry.
//
// Decisions recorded for T5.3 (the plan's six directives are inferred from the
// graph, no text directive is needed for the fixtures):
//
//  - Native layout, not CrochetPARADE's positions (the plan's option A/B): the
//    T4.0 spike measured a 66 s cold layout for the bag and a tube whose radius
//    drifts 2.07 -> 4.90, and a text-export of positions would need node at run
//    time. The plan's section 6.8 rules are used for every piece.
//  - Regions. Rows are grouped in made order:
//      Foundation  rows without targets (the chain, the magic ring)
//      Flat        turned rows worked off the foundation or each other
//      Revolve     closed rounds worked off the foundation or each other
//      Rim         a round that is not closed (border, strap-end join), placed
//                  along the boundary of what exists, one stitch height out
//      Strip       turned rows worked off a Revolve/Rim region (the strap)
//    A row whose stitches are all dropped (a surface sl st row) has no region.
//  - World frame: y is up. Flat pieces hang in the xy plane (foundation at
//    the top, wale down, the right side facing +z); revolve pieces rise from a
//    base in the xz plane (axis +y, right side up on the base, inside on the
//    walls: the RS continues smoothly from the base, as when a bag is worked in
//    the round and not turned). `mirror` flags below are chosen so that the
//    winding normal of the first face of a region agrees with that rule.
//  - Flat: row by row, x from the mean of the targets, smoothed along the row by
//    the plan's tridiagonal least squares, y = targets + stitch height.
//  - Revolve: plan rule r_k = (C_k - 2L)/2pi, y_k = y_{k-1} + sqrt(max(h^2 -
//    dr^2, 0)) on the offset curve of the centre path (a point for a magic ring,
//    a segment of length L for a chain); the oval's first ring has r_1 = h (the
//    legs have the stitch height) and takes L from the perimeter.
//  - Fold. When the point seams of a Flat region pair its rows i and j with
//    constant i + j (the pouch: both_layers, i + j = 51), the rows after the
//    middle are reflected about the line between rows k and k+1 and put behind
//    at z = -(thickness + 1 mm), so the mouth (rows 1 and 50) is on top.
//  - Rim: position = mean target + stitch height x the outward direction of the
//    boundary of the placed faces; runs of stitches that share targets are
//    spread along the rim polyline by the same least squares. A pair of
//    stitches whose targets are further apart than 3 stitch widths (a graph
//    jump) breaks the run.
//  - Strip: the rows are a flat strip in (lambda, sigma), then embedded on a
//    circular arc (symmetric about the chord) in the plane through the chord from
//    the start to the join targets and the up direction, bulging up, arc length
//    = the strip's length; without a join the strip leaves the rim straight.
//    Start footprint and orientation come from the rim it is worked off.
#include <string>
#include <vector>

#include "failoring/crochet/model.hpp"
#include "failoring/math.hpp"

namespace fl {

struct LayoutOptions {
    Vec3 up{0, 1, 0};
    Real foldGap = 1.0e-3;  // m, added to the fabric thickness between the two layers of a fold
};

struct LayoutRegion {
    enum Type { Foundation, Flat, Revolve, Rim, Strip };
    Type type = Foundation;
    int firstRow = -1, lastRow = -1;
    bool mirrored = false;  // the orientation fix of the region (see header)
};

struct LayoutReport {
    std::vector<LayoutRegion> regions;
    int foldAfterRow = -1;       // Flat region folded between this row and the next, -1 = no fold
    std::vector<int> openingRows;  // the rows that form the mouth of a bag: the last revolve round, or the two edge rows of a folded piece
    Real depth = 0;              // m, rest depth of the bag from base (or fold) to mouth
    Real stripArcLength = 0;     // m, total arc length of the strap (0 = none)
    Real stripChord = 0;         // m
    std::vector<std::string> warnings;
};

// Fill model.mesh.x0 and model.mesh.layoutUV (one entry per vertex). Never
// throws on a pattern it cannot place; it places what it can and warns.
LayoutReport layoutModel(CrochetModel& model, const StitchGraph& g, const LayoutOptions& opt = {});

}  // namespace fl
