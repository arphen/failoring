#pragma once
// Intrinsic meshes (CROCHET_PLAN §2.1): a mesh whose triangles each carry their own rest shape and material, plus
// optional per-edge bending modifiers, chains and per-vertex lumped mass. Crochet uses it because its stitch graph
// is not a single flat pattern piece: every triangle's rest shape comes from the stitch sizes around it (§6.3).
// Header-only; the solver side is ClothSolver::addIntrinsicPanel (cloth.hpp).
#include <array>
#include <vector>

#include "failoring/math.hpp"

namespace fl {

// Bending modifier for one interior mesh edge (a hinge). `a` and `b` are LOCAL vertex ids of the shared edge
// (0-based indices into IntrinsicMesh::x0 / layoutUV); the pair is order-insensitive. Edges not listed keep
// bendScale 1 and restAngle 0, i.e. the plain mesh behaviour.
struct HingeMod {
    int a = 0, b = 0;      // local vertex ids of the shared edge (the hinge axis)
    Real bendScale = 1;    // dimensionless multiplier on the hinge's effective flexural rigidity
                           // (1 = the material's value; < 1 softer, e.g. BLO rounds; > 1 stiffer)
    Real restAngle = 0;    // rad, signed hinge rest angle, sign convention of BendC::rest in cloth.hpp
};

// A mesh with per-triangle rest shapes. All lengths are metres, mass is kg, angles radians.
struct IntrinsicMesh {
    std::vector<Vec3> x0;    // initial 3D positions (m), one per vertex
    std::vector<Vec2> layoutUV;
    // 2D parameterisation of the vertices (m): display/texture layout, seam arc lengths and the fallback rest
    // coordinates restUV_. May be empty; then each vertex's restUV_ entry is {0, 0}. It is NOT the triangles'
    // rest shape - that is triRest. Any sensible layout works; it must not be relied on for lengths (use chains
    // and triRest for those).
    std::vector<std::array<int, 3>> tris;        // vertex triples, indices into x0/layoutUV; consistently oriented
    std::vector<std::array<Vec2, 3>> triRest;    // rest corner positions per triangle, (u = wale, v = course), metres
    std::vector<int> triMaterial;                // index into the materials vector passed to addIntrinsicPanel
    std::vector<HingeMod> hingeMods;             // optional bending modifiers; empty = plain mesh behaviour
    std::vector<std::vector<int>> chains;        // optional named vertex lists (local ids), e.g. for seams/pins;
                                                 // becomes ClothSolver::edgeChain(panel, edge)
    std::vector<Real> extraMass;                 // optional additional lumped vertex mass (kg), one per vertex;
                                                 // empty = none. For yarn mass of nodes dropped from the mesh (§6.1).
};

}  // namespace fl
