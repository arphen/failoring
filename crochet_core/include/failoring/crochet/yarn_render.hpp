#pragma once
// Yarn mode: the yarn drawn. A tube along each rod chain, its plies drawn as helices that follow the rod's material frame (so a strand
// that has been twisted shows it), and a small software renderer (perspective, z-buffer, smooth shading, 2x supersampling) that writes
// PNGs for the tests and the CLI. The Godot view builds the same mesh and shades it with a shader instead.
#include <array>
#include <string>
#include <vector>

#include "failoring/crochet/rod.hpp"
#include "failoring/math.hpp"

namespace fl {

struct YarnMeshOptions {
    int sides = 10;            // vertices round the tube
    int subdiv = 3;            // samples per rod segment
    Real radiusScale = 1.0;    // the drawn radius as a multiple of the rod's (the plan's "yarn thickness" slider)
    int plies = 3;             // strands twisted together
    Real plyPitch = 0.007;     // m of yarn per turn of one ply
    Real plyContrast = 0.35;   // 0 = a plain tube
    Vec3 colour{0.80, 0.30, 0.22};
    std::vector<Vec3> segmentColour;  // optional: one colour per rod segment (overrides `colour`)
    // A flat yarn (see RodParams::ribbonWidth): the section is an ellipse of these half widths at the rod's radius, its thin axis along `ribbonNormal` (squared to the yarn). 0 = round.
    Real ribbonWidth = 0.0, ribbonThickness = 0.0;
    Vec3 ribbonNormal{0, 0, 1};
    int maxParticles = -1;     // draw only the first this many particles of each chain (the yarn growing as it is made); -1 = all
};

struct YarnMesh {
    std::vector<Vec3> pos, nrm, col;
    std::vector<std::array<int, 3>> tri;
};

YarnMesh buildYarnMesh(const RodSystem& rs, const YarnMeshOptions& o = {});
// A yarn of the given radius along a free curve (the run to the ball), with the same plies; rounded at both ends. Every yarn drawn by buildYarnMesh is rounded at its free ends too.
YarnMesh buildTubeMesh(const std::vector<Vec3>& pts, Real radius, const Vec3& colour, const YarnMeshOptions& o = {});

struct YarnCamera {
    Vec3 target{0, 0, 0};
    Vec3 dir{0, 0, 1};      // from the target toward the camera
    Vec3 up{0, 1, 0};
    Real distance = 0.1;    // m
    Real fovDeg = 35.0;     // vertical
};

// Renders `mesh` and writes a PNG; returns false when the file cannot be written.
bool renderYarnPng(const std::string& path, const YarnMesh& mesh, const YarnCamera& cam, int width, int height,
                   const Vec3& background = {0.93, 0.93, 0.91});
// The same into an RGB buffer (width * height * 3), for putting several views side by side.
std::vector<unsigned char> renderYarnRgb(const YarnMesh& mesh, const YarnCamera& cam, int width, int height,
                                         const Vec3& background = {0.93, 0.93, 0.91});

}  // namespace fl
