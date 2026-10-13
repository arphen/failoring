#pragma once
#include <array>
#include <vector>

#include "failoring/math.hpp"

namespace fl {

// A flat pattern piece triangulated in its own 2D (u,v) plane, in metres.
struct PanelMesh {
    std::vector<Vec2> verts;
    std::vector<std::array<int, 3>> tris;  // counter-clockwise in (u,v)
    // For outline edge i (outline[i] -> outline[i+1]) the vertex indices lying on
    // it, ordered from outline[i] to outline[i+1]. Seams refer to these chains.
    std::vector<std::vector<int>> edgeChains;
    // False if the triangulation does not contain every boundary segment
    // (can happen with very thin features relative to the edge length).
    bool boundaryComplete = true;
};

// Triangulate a simple polygon (any winding) with target edge length `h`.
// Boundary points are placed exactly along the outline; interior points on a
// hexagonal lattice; Delaunay triangulation, clipped to the polygon.
PanelMesh meshPolygon(const std::vector<Vec2>& outline, Real h);

// The same with holes: each hole is a simple polygon (any winding) inside the outline and not touching another hole.
// Hole boundaries get exact boundary points like the outline's, and triangles inside a hole are dropped, so the hole
// edges are free edges of the cloth (they have no chains: seams only join outline edges). boundaryComplete also
// checks the hole segments. Holes are an empty list for a plain polygon.
PanelMesh meshPolygon(const std::vector<Vec2>& outline, const std::vector<std::vector<Vec2>>& holes, Real h);

// Convenience: rectangle with corners (0,0)-(w,height). Outline edges: bottom, right, top, left.
PanelMesh meshRect(Real w, Real height, Real h);

// Convenience: disc centred at the origin (outline is a regular polygon with ~h spacing).
PanelMesh meshDisc(Real radius, Real h);

// Area of a simple polygon (positive if counter-clockwise).
Real polygonSignedArea(const std::vector<Vec2>& poly);

}  // namespace fl
