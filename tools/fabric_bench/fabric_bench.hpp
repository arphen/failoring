#pragma once
// Virtual fabric test bench: standard textile-style tests run through the Failoring cloth solver.
// Standard library + cloth_core only. See docs/FABRIC_TESTING.md (the swatch protocol; the bench itself is documented in its header and in the output).
#include <string>
#include <vector>

#include "failoring/cloth.hpp"
#include "failoring/material.hpp"

namespace fb {

using fl::FabricMaterial;

constexpr double kG = 9.81;                     // m/s^2, same value as SolverParams::gravity
constexpr double kCantileverAngleDeg = 41.5;    // Peirce / ASTM D1388 cantilever angle (UNVERIFIED, from memory)
constexpr double kPi = 3.14159265358979323846;

// ---- beam theory helpers ----------------------------------------------------------------------------
// Nondimensional load lambda = q l^3 / B (q = rho*g per unit length per unit width) of an inextensible
// elastica cantilever, uniformly loaded, whose chord (clamp -> tip) points chordRad below horizontal.
// Derived in this tool (theta'' = -lambda (1-u) cos(theta), theta(0)=0, theta'(1)=0) and solved by shooting;
// its small-angle limit (tip deflection = lambda/8 * l) is checked in test_fabric_bench.
double elasticaChordAngle(double lambda);               // radians
double elasticaLambdaForChord(double chordRad);          // inverse (bisection)
// Bending rigidity per unit width from an overhang l (m) at chord angle, areal density rho (kg/m2).
double bendingFromOverhangElastica(double overhang, double rho, double chordRad);
// Peirce bending length c = l * (cos t / (8 tan t))^(1/3), B = rho g c^3   (formula UNVERIFIED, from memory)
double bendingFromOverhangPeirce(double overhang, double rho, double chordRad);
// ASTM-style shortcut c = l/2, B = rho g c^3 (UNVERIFIED, from memory)
double bendingFromOverhangHalf(double overhang, double rho);
// Inverse of the elastica relation: overhang that gives the chord angle for rigidity B.
double overhangFromBendingElastica(double B, double rho, double chordRad);
// Directional bending rigidity of the solver's compliance-mixing hinge model at angle psi to the warp
// (the model's own assumption in cloth.cpp; not an independent textile result).
double bendingAtAngleModel(const FabricMaterial& m, double psiRad);

// ---- simulations --------------------------------------------------------------------------------------
struct CantileverSim {
    double chordDeg = 0;     // chord angle below horizontal of the clamp -> tip line
    double overhang = 0;     // actual overhang length (m), measured from the last pinned column
    double tipDrop = 0;      // m
    int frames = 0;
    bool settled = false;
    double seconds = 0;
    double widthEff = 0;     // m, mesh area / length
};
// Strip on a regular equilateral lattice, grain at `grainRad` to the strip length. Columns 0 and 1 are pinned (clamp);
// the overhang (m) runs from column 1 to the tip and is divided into nCols column pitches; nRows nodes across.
CantileverSim simulateCantilever(const FabricMaterial& m, double grainRad, double overhang, int nRows, int nCols);
struct HangGeom { int nCols, nRows; double h, widthEff; };
HangGeom hangGeometry(double widthNominal, double length, double hNominal);

struct HangSim {
    double elongation = 0;   // m, change of the length between the last pinned row and the bottom edge
    double gauge = 0;        // m, rest length of that span
    double widthEff = 0;     // m, mesh area / length (tension per width = m g / widthEff)
    int frames = 0;
    bool settled = false;
    double seconds = 0;
};
// Strip hanging from a pinned top edge with a mass (kg) attached along the bottom edge.
HangSim simulateHanging(const FabricMaterial& m, double grainRad, double widthNominal, double length, double hNominal, double massKg);

struct DrapeSim {
    double dcPercent = 0;
    int folds = 0;
    double rMin = 0, rMax = 0;       // m, min / max radius of the shadow outline
    double shadowArea = 0;           // m2
    int frames = 0;
    double ke = 0, clearance = 0;
    double seconds = 0;
    std::vector<double> radial;      // outline radius per degree (m)
};
DrapeSim simulateDrape(const FabricMaterial& m, double h, int maxFrames = 300);
// Fold counting on a periodic radial profile (heuristic: prominence >= max(3 mm, 15 % of the range)).
int countFolds(const std::vector<double>& radial);
// Area (m2) of the union of the projected triangles; same rasterisation as fl::projectedShadowArea, plus the outline.
double shadowRaster(const std::vector<fl::Vec3>& x, const std::vector<std::array<int, 3>>& tris, double extent, int res,
                    std::vector<double>* radialPerDegree);

// ---- bench driver -------------------------------------------------------------------------------------
struct BenchConfig {
    double stripWidth = 0.025;                 // cantilever strip width (m)
    double angleDeg = kCantileverAngleDeg;
    std::vector<int> bendLevels{6, 10, 16};    // elements along the overhang; the middle one is the headline
    std::vector<double> drapeLevels{0.014, 0.010, 0.007};  // edge lengths (m); middle is the headline
    int drapeFrames = 300;                     // frame cap per drape run (the run stops early when KE < 1e-8)
    double tensileWidth = 0.05, tensileLength = 0.30, tensileMesh = 0.00625;
    double biasLength = 0.30;
    std::vector<double> warpWeftStrains{0.004, 0.008, 0.012};
    std::vector<double> biasStrains{0.01, 0.02, 0.03};
    bool doBending = true, doTensile = true, doShear = true, doDrape = true;
    bool verbose = true;
};

struct BendLevel {
    int n = 0;
    double h = 0, overhang = 0, Belastica = 0, ratio = 0;
    int sims = 0;
    double seconds = 0, chordErrDeg = 0;
};
struct BendDir {
    std::string dir;                 // warp | weft | bias
    double grainDeg = 0;
    double Binput = 0;               // N*m (compliance-mixing model reference for bias)
    std::vector<BendLevel> levels;   // per mesh level
    size_t headline = 0;
    double Bpeirce = 0, Bhalf = 0;   // from the headline overhang
    double ratioPeirce = 0, ratioHalf = 0;
};
struct TensileDir {
    std::string dir;
    double kInput = 0;               // N/m (warp, weft) or effective bias modulus 4/(1/ku+1/kv+1/G) for bias
    std::vector<double> mass, tension, strain;   // kg, N/m, -
    double kDerived = 0;             // regression slope d(tension)/d(strain)
    double ratio = 0;
    double seconds = 0;
};
struct ShearResult {
    double Ginput = 0;
    double Gnaive = 0;               // 1/G = 4/E45 - 1/ku - 1/kv  (uniform uniaxial stress; ignores clamp zones)
    double ratio = 0;
};
struct DrapeLevel {
    double h;
    DrapeSim sim;
};
struct FabricBench {
    FabricMaterial mat;
    std::vector<BendDir> bending;
    std::vector<TensileDir> tensile;  // warp, weft, bias
    ShearResult shear;
    std::vector<DrapeLevel> drape;
    size_t drapeHeadline = 1;
    double secondsTotal = 0;
    double bendingLengthWarp = 0;     // (B/(rho g))^(1/3) from the INPUT B, plain definition
    double bendingLengthWeft = 0;
};

FabricBench runFabricBench(const FabricMaterial& m, const BenchConfig& cfg);
std::string benchToJson(const FabricBench& b, const BenchConfig& cfg);
std::string benchToMarkdown(const FabricBench& b, const BenchConfig& cfg);
// Bias-angle strip check: cantilever strips at several angles to the warp, compared with the compliance-mixing
// model B(psi) used by the solver. Uses one mesh level (n elements along the overhang).
struct BiasPoint {
    double angleDeg = 0, Bmodel = 0, Bderived = 0, ratio = 0, ratioLo = 0, ratioHi = 0;
    int n = 0;
};
std::vector<BiasPoint> runBiasStrip(const FabricMaterial& m, const BenchConfig& cfg, const std::vector<double>& anglesDeg, int n);
// Combined hand-panel / summary tables for many fabrics.
std::string summaryMarkdown(const std::vector<FabricBench>& all, const BenchConfig& cfg);

// Overhang that gives the target chord angle for the solver (secant search), at n elements along the overhang.
BendLevel benchBendingLevel(const FabricMaterial& m, double grainRad, int n, const BenchConfig& cfg, double Bref);

// ---- tiny JSON writer helpers -------------------------------------------------------------------------
std::string jnum(double v);
std::string jstr(const std::string& s);

}  // namespace fb
