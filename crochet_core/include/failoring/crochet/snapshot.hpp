#pragma once
// A tiny orthographic software renderer + PNG writer for the crochet layouts and
// scenes (docs/CROCHET_PLAN.md sections 6.8 and 7.1): no dependencies, used by the
// tests and by crochet_cli to write the snapshots under docs/crochet-shots/.
// Four tiles side by side: front (camera on +z), side (camera on +x), top
// (camera on +y) and an oblique view. Faces whose winding normal looks toward
// the camera are drawn in the right-side colour, the others in the wrong-side
// colour, so a flipped surface is visible at a glance.
#include <array>
#include <string>
#include <vector>

#include "failoring/math.hpp"

namespace fl {

struct SnapshotColour {
    unsigned char r = 200, g = 120, b = 60;
};

// colours may be empty (one colour) or have one entry per triangle. Returns false when the file cannot be written.
bool writeSnapshotPng(const std::string& path, const std::vector<Vec3>& pos, const std::vector<std::array<int, 3>>& tris,
                      const std::vector<SnapshotColour>& colours = {}, int tile = 360);

}  // namespace fl
