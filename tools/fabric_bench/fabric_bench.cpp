#include "fabric_bench.hpp"

#include <algorithm>
#include <ctime>
#include <cmath>
#include <cstdio>
#include <limits>
#include <sstream>

#include "failoring/collider.hpp"
#include "failoring/mesher.hpp"

namespace fb {
using namespace fl;

namespace {
// Process CPU time in seconds: the build machines are shared, so wall time is not a usable baseline.
double now() { return static_cast<double>(std::clock()) / CLOCKS_PER_SEC; }
}  // namespace

// =====================================================================================================
// beam theory
// =====================================================================================================
namespace {
struct ElasticaEnd { double omega, x, y, theta; };
ElasticaEnd shoot(double lambda, double kappa0) {
    const int steps = 400;
    const double du = 1.0 / steps;
    double s[4] = {0, kappa0, 0, 0};  // theta, omega, x, y
    auto rhs = [&](double u, const double* q, double* d) {
        d[0] = q[1];
        d[1] = -lambda * (1 - u) * std::cos(q[0]);
        d[2] = std::cos(q[0]);
        d[3] = std::sin(q[0]);
    };
    for (int i = 0; i < steps; ++i) {
        const double u = i * du;
        double k1[4], k2[4], k3[4], k4[4], t[4];
        rhs(u, s, k1);
        for (int j = 0; j < 4; ++j) t[j] = s[j] + 0.5 * du * k1[j];
        rhs(u + 0.5 * du, t, k2);
        for (int j = 0; j < 4; ++j) t[j] = s[j] + 0.5 * du * k2[j];
        rhs(u + 0.5 * du, t, k3);
        for (int j = 0; j < 4; ++j) t[j] = s[j] + du * k3[j];
        rhs(u + du, t, k4);
        for (int j = 0; j < 4; ++j) s[j] += du / 6 * (k1[j] + 2 * k2[j] + 2 * k3[j] + k4[j]);
    }
    return {s[1], s[2], s[3], s[0]};
}
}  // namespace

double elasticaChordAngle(double lambda) {
    double lo = 0, hi = lambda;  // omega(1) is negative at kappa0=0 and positive at kappa0=lambda
    for (int i = 0; i < 60; ++i) {
        const double mid = 0.5 * (lo + hi);
        (shoot(lambda, mid).omega < 0 ? lo : hi) = mid;
    }
    const ElasticaEnd e = shoot(lambda, 0.5 * (lo + hi));
    return std::atan2(e.y, e.x);
}

double elasticaLambdaForChord(double chordRad) {
    double lo = std::log(1e-6), hi = std::log(1e6);
    for (int i = 0; i < 60; ++i) {
        const double mid = 0.5 * (lo + hi);
        (elasticaChordAngle(std::exp(mid)) < chordRad ? lo : hi) = mid;
    }
    return std::exp(0.5 * (lo + hi));
}

double bendingFromOverhangElastica(double l, double rho, double chordRad) {
    return rho * kG * l * l * l / elasticaLambdaForChord(chordRad);
}
double overhangFromBendingElastica(double B, double rho, double chordRad) {
    return std::cbrt(elasticaLambdaForChord(chordRad) * B / (rho * kG));
}
double bendingFromOverhangPeirce(double l, double rho, double chordRad) {
    const double f = std::cbrt(std::cos(chordRad) / (8 * std::tan(chordRad)));
    const double c = l * f;
    return rho * kG * c * c * c;
}
double bendingFromOverhangHalf(double l, double rho) {
    const double c = 0.5 * l;
    return rho * kG * c * c * c;
}
double bendingAtAngleModel(const FabricMaterial& m, double psi) {
    const double c2 = std::cos(psi) * std::cos(psi), s2 = std::sin(psi) * std::sin(psi);
    return 1.0 / (c2 * c2 / m.bendWarp + s2 * s2 / m.bendWeft + 2 * c2 * s2 / std::sqrt(m.bendWarp * m.bendWeft));
}

// =====================================================================================================
// simulations
// =====================================================================================================
namespace {
SolverParams staticParams() {
    SolverParams sp;
    sp.substeps = 1;
    sp.selfCollision = false;
    sp.newtonTolerance = 1e-9;
    sp.seamStiffness = 1e5;
    return sp;
}
}  // namespace

// Regular equilateral-lattice strip, length `length` along u (nCols+1 columns, spacing a = length/nCols, edge h = a/0.866),
// nRows nodes per column, odd columns shifted by h/2. A regular lattice gives a well-defined clamp and tip and a
// smooth dependence on the overhang (the generic Delaunay mesher's irregular boundary made B jump by +-15 %).
// Outline edges follow meshRect: 0 = v min (+u), 1 = u max (+v), 2 = v max (-u), 3 = u min (-v).
static PanelMesh meshStripLattice(double length, int nCols, int nRows, double* widthEff, double hRow = -1) {
    const double a = length / nCols, h = hRow > 0 ? hRow : a / 0.8660254037844386;
    PanelMesh m;
    auto id = [&](int i, int j) { return i * nRows + j; };
    for (int i = 0; i <= nCols; ++i)
        for (int j = 0; j < nRows; ++j) m.verts.push_back({i * a, j * h + ((i & 1) ? 0.5 * h : 0.0)});
    for (int i = 0; i < nCols; ++i)
        for (int j = 0; j + 1 < nRows; ++j) {
            if (!(i & 1)) {
                m.tris.push_back({id(i, j), id(i + 1, j), id(i, j + 1)});
                m.tris.push_back({id(i + 1, j), id(i + 1, j + 1), id(i, j + 1)});
            } else {
                m.tris.push_back({id(i + 1, j), id(i + 1, j + 1), id(i, j)});
                m.tris.push_back({id(i, j), id(i + 1, j + 1), id(i, j + 1)});
            }
        }
    double area = 0;
    for (const auto& t : m.tris) {
        const Vec2 &p = m.verts[t[0]], &q = m.verts[t[1]], &r = m.verts[t[2]];
        area += 0.5 * ((q.x - p.x) * (r.y - p.y) - (r.x - p.x) * (q.y - p.y));
    }
    if (widthEff) *widthEff = area / length;
    std::vector<int> e0, e1, e2, e3;
    for (int i = 0; i <= nCols; ++i) e0.push_back(id(i, 0));
    for (int j = 0; j < nRows; ++j) e1.push_back(id(nCols, j));
    for (int i = nCols; i >= 0; --i) e2.push_back(id(i, nRows - 1));
    for (int j = nRows - 1; j >= 0; --j) e3.push_back(id(0, j));
    m.edgeChains = {e0, e1, e2, e3};
    return m;
}

HangGeom hangGeometry(double Wnom, double Ltot, double hNom) {
    HangGeom g;
    g.nCols = std::max(2, 2 * static_cast<int>(std::lround(Ltot / (0.866 * hNom) / 2)));  // even: tip column is not shifted
    g.h = Ltot / g.nCols / 0.866;
    g.nRows = std::max(3, static_cast<int>(std::lround(Wnom / g.h)) + 1);
    meshStripLattice(Ltot, g.nCols, g.nRows, &g.widthEff);
    return g;
}

CantileverSim simulateCantilever(const FabricMaterial& mIn, double grain, double overhang, int nRows, int nCols) {
    const double t0 = now();
    FabricMaterial m = mIn;
    const double Bmin = std::min(m.bendWarp, m.bendWeft);
    const double w0 = 3.52 * std::sqrt(Bmin / m.density) / (overhang * overhang);
    m.damping = std::min(300.0, std::max(5.0, 2 * w0));  // near-critical damping: statics only, does not change the equilibrium

    // columns 0 and 1 are pinned (a single pinned column would be a hinge, not a clamp); overhang = from column 1 to the tip
    const int cols = nCols + 1;
    double widthEff = 0;
    PanelMesh mesh = meshStripLattice(overhang * cols / nCols, cols, nRows, &widthEff);
    ClothSolver cs(staticParams());
    PanelPlacement pl;
    pl.ex = Vec3(1, 0, 0);
    pl.ey = Vec3(0, 0, 1);  // strip lies flat in the x-z plane, length along +x; gravity acts along -y
    cs.addPanel(mesh, m, pl, grain);
    const double a = overhang / nCols;
    for (int v = 0; v < cs.vertexCount(); ++v)
        if (mesh.verts[v].x < 1.5 * a) cs.pinVertex(v);
    const std::vector<int>& tip = mesh.edgeChains[1];

    CantileverSim r;
    r.overhang = overhang;
    r.widthEff = widthEff;
    std::vector<double> hist;
    int f = 0;
    for (; f < 900; ++f) {
        cs.step();
        double ty = 0;
        for (int v : tip) ty += cs.positions()[v].y;
        hist.push_back(ty / tip.size());
        if (f >= 40 && std::fabs(hist[f] - hist[f - 10]) < 1e-7 * overhang) { r.settled = true; break; }
    }
    r.frames = f;
    double ty = 0, tx = 0;
    for (int v : tip) { ty += cs.positions()[v].y; tx += cs.positions()[v].x; }
    ty /= tip.size();
    tx /= tip.size();
    r.tipDrop = -ty;
    r.chordDeg = std::atan2(-ty, tx - a) * 180.0 / kPi;
    r.seconds = now() - t0;
    return r;
}

HangSim simulateHanging(const FabricMaterial& mIn, double grain, double Wnom, double Ltot, double hNom, double massKg) {
    const double t0 = now();
    FabricMaterial m = mIn;
    // near-critical damping for the axial mode (statics only)
    const double kEff = std::min(m.tensileWarp, m.tensileWeft) * Wnom / Ltot;
    const double mtot = massKg + m.density * Wnom * Ltot;
    m.damping = std::min(500.0, std::max(10.0, 2 * std::sqrt(kEff / mtot)));

    const HangGeom g = hangGeometry(Wnom, Ltot, hNom);
    const int nCols = g.nCols, nRows = g.nRows;
    const double h = g.h, widthEff = g.widthEff;
    PanelMesh mesh = meshStripLattice(Ltot, nCols, nRows, nullptr);
    ClothSolver cs(staticParams());
    PanelPlacement pl;
    pl.ex = Vec3(0, -1, 0);  // strip length (u) hangs down from y = 0; v runs along +x
    pl.ey = Vec3(1, 0, 0);
    cs.addPanel(mesh, m, pl, grain, "strip");
    for (int v = 0; v < cs.vertexCount(); ++v)
        if (mesh.verts[v].x < 1e-9) cs.pinVertex(v);
    const std::vector<int> bottom = mesh.edgeChains[1];
    if (massKg > 0) {
        const double Hw = 0.01;
        FabricMaterial wm;
        wm.id = "weight";
        wm.thickness = m.thickness;
        wm.tensileWarp = wm.tensileWeft = 5.0e4;
        wm.shear = 5.0e3;
        wm.bendWarp = wm.bendWeft = 1.0e-3;
        wm.compressionRatio = 1.0;
        wm.damping = m.damping;
        double wW = 0;
        PanelMesh wmesh = meshStripLattice(Hw, 2, nRows, &wW, h);  // same row pitch as the strip, so the seam nodes coincide
        wm.density = massKg / (wW * Hw);
        PanelPlacement wp;
        wp.origin = Vec3(0, -Ltot, 0);
        wp.ex = Vec3(0, -1, 0);
        wp.ey = Vec3(1, 0, 0);
        cs.addPanel(wmesh, wm, wp, 0.0, "weight");
        cs.addSeam(0, 1, 1, 3, true);  // strip tip edge (running +v) to the weight's near edge (running -v)
    }
    HangSim r;
    r.gauge = Ltot;
    r.widthEff = widthEff;
    std::vector<double> hist;
    int f = 0;
    for (; f < 900; ++f) {
        cs.step();
        double by = 0;
        for (int v : bottom) by += cs.positions()[v].y;
        hist.push_back(by / bottom.size());
        if (f >= 30 && std::fabs(hist[f] - hist[f - 10]) < 1e-8) { r.settled = true; break; }
    }
    r.frames = f;
    r.elongation = -hist.back() - Ltot;
    r.seconds = now() - t0;
    return r;
}

// ---- shadow raster + folds ---------------------------------------------------------------------------
double shadowRaster(const std::vector<Vec3>& x, const std::vector<std::array<int, 3>>& tris, double extent, int res,
                    std::vector<double>* radial) {
    std::vector<char> covered(static_cast<size_t>(res) * res, 0);
    const double cell = 2 * extent / res;
    auto toGrid = [&](double v) { return (v + extent) / cell; };
    for (const auto& t : tris) {
        const Vec3 &a = x[t[0]], &b = x[t[1]], &c = x[t[2]];
        const double ax = toGrid(a.x), az = toGrid(a.z), bx = toGrid(b.x), bz = toGrid(b.z), cx = toGrid(c.x), cz = toGrid(c.z);
        const int x0 = std::max(0, static_cast<int>(std::floor(std::min({ax, bx, cx})))), x1 = std::min(res - 1, static_cast<int>(std::ceil(std::max({ax, bx, cx}))));
        const int z0 = std::max(0, static_cast<int>(std::floor(std::min({az, bz, cz})))), z1 = std::min(res - 1, static_cast<int>(std::ceil(std::max({az, bz, cz}))));
        const double det = (bz - cz) * (ax - cx) + (cx - bx) * (az - cz);
        if (std::fabs(det) < 1e-12) continue;
        for (int iz = z0; iz <= z1; ++iz)
            for (int ix = x0; ix <= x1; ++ix) {
                const double px = ix + 0.5, pz = iz + 0.5;
                const double l1 = ((bz - cz) * (px - cx) + (cx - bx) * (pz - cz)) / det;
                const double l2 = ((cz - az) * (px - cx) + (ax - cx) * (pz - cz)) / det;
                const double l3 = 1 - l1 - l2;
                if (l1 >= 0 && l2 >= 0 && l3 >= 0) covered[static_cast<size_t>(iz) * res + ix] = 1;
            }
    }
    size_t n = 0;
    if (radial) radial->assign(360, 0.0);
    for (int iz = 0; iz < res; ++iz)
        for (int ix = 0; ix < res; ++ix)
            if (covered[static_cast<size_t>(iz) * res + ix]) {
                ++n;
                if (radial) {
                    const double px = (ix + 0.5) * cell - extent, pz = (iz + 0.5) * cell - extent;
                    double ang = std::atan2(pz, px) * 180.0 / kPi;
                    if (ang < 0) ang += 360;
                    const int bin = std::min(359, static_cast<int>(ang));
                    (*radial)[bin] = std::max((*radial)[bin], std::sqrt(px * px + pz * pz));
                }
            }
    return static_cast<double>(n) * cell * cell;
}

int countFolds(const std::vector<double>& rad) {
    const int n = static_cast<int>(rad.size());
    if (n < 16) return 0;
    std::vector<double> s(n);
    for (int i = 0; i < n; ++i) {  // circular moving average over 5 samples
        double a = 0;
        for (int k = -2; k <= 2; ++k) a += rad[(i + k + n) % n];
        s[i] = a / 5;
    }
    const double rmax = *std::max_element(s.begin(), s.end()), rmin = *std::min_element(s.begin(), s.end());
    const double minProm = std::max(0.003, 0.15 * (rmax - rmin));
    int count = 0;
    for (int i = 0; i < n; ++i) {
        bool isMax = true;
        for (int k = -4; k <= 4 && isMax; ++k) {
            if (k == 0) continue;
            const double o = s[(i + k + n) % n];
            if (o > s[i] || (o == s[i] && k < 0)) isMax = false;
        }
        if (!isMax) continue;
        // prominence: walk both ways until a higher value (or a full turn), remember the lowest point passed
        double lmin = s[i], rmn = s[i];
        for (int k = 1; k < n; ++k) {
            const double o = s[(i - k + n) % n];
            if (o > s[i]) break;
            lmin = std::min(lmin, o);
        }
        for (int k = 1; k < n; ++k) {
            const double o = s[(i + k) % n];
            if (o > s[i]) break;
            rmn = std::min(rmn, o);
        }
        if (s[i] - std::max(lmin, rmn) >= minProm) ++count;
    }
    return count;
}

DrapeSim simulateDrape(const FabricMaterial& mat, double h, int maxFrames) {
    // Same rig as cloth_core/tests/test_drape.cpp: 30 cm disc, 18 cm pedestal, disc 12 mm above the pedestal top.
    const double R = 0.15, Rs = 0.09;
    PanelMesh disc = meshDisc(R, h);
    SolverParams sp;
    sp.substeps = 1;
    ClothSolver cs(sp);
    PanelPlacement pl;
    pl.origin = Vec3(0, 0.012, 0);
    pl.ex = Vec3(1, 0, 0);
    pl.ey = Vec3(0, 0, 1);
    cs.addPanel(disc, mat, pl, 0.0);
    cs.setCollider(std::make_shared<CylinderCollider>(Vec3(0, -0.10, 0), Rs, 0.10));
    const double t0 = now();
    int f = 0;
    for (; f < maxFrames; ++f) {
        cs.step();
        if (f > 100 && cs.kineticEnergy() < 1e-8) break;
    }
    DrapeSim d;
    d.frames = f;
    d.ke = cs.kineticEnergy();
    d.clearance = cs.minBodyClearance();
    d.shadowArea = shadowRaster(cs.positions(), cs.triangles(), 0.2, 1000, &d.radial);
    const double aDisc = kPi * R * R, aSupport = kPi * Rs * Rs;
    d.dcPercent = 100.0 * (d.shadowArea - aSupport) / (aDisc - aSupport);
    d.rMin = *std::min_element(d.radial.begin(), d.radial.end());
    d.rMax = *std::max_element(d.radial.begin(), d.radial.end());
    d.folds = countFolds(d.radial);
    d.seconds = now() - t0;
    return d;
}

// =====================================================================================================
// bench driver
// =====================================================================================================
BendLevel benchBendingLevel(const FabricMaterial& m, double grain, int n, const BenchConfig& cfg, double Bref) {
    const double t0 = now();
    const double theta = cfg.angleDeg;
    const double thetaRad = theta * kPi / 180.0;
    const double l0 = overhangFromBendingElastica(Bref, m.density, thetaRad);
    const double h0 = l0 / (0.866 * n) * 0.866;  // lattice edge ~ l0/n (column pitch 0.866 h)
    const int nRows = std::max(3, static_cast<int>(std::lround(cfg.stripWidth / h0)) + 1);
    struct Pt { double lnl, err; CantileverSim s; };
    std::vector<Pt> pts;
    auto eval = [&](double l) {
        CantileverSim s = simulateCantilever(m, grain, l, nRows, n);
        pts.push_back({std::log(l), s.chordDeg - theta, s});
    };
    eval(l0);
    eval(l0 * (pts[0].err > 0 ? 0.93 : 1.07));
    for (int it = 0; it < 6; ++it) {
        const Pt &a = pts[pts.size() - 2], &b = pts.back();
        if (std::fabs(b.err) < 0.02) break;
        const double slope = (b.err - a.err) / (b.lnl - a.lnl);
        double next = b.lnl - b.err / slope;
        next = std::min(std::max(next, b.lnl - 0.3), b.lnl + 0.3);
        eval(std::exp(next));
    }
    std::vector<Pt> sorted = pts;
    std::sort(sorted.begin(), sorted.end(), [](const Pt& a, const Pt& b) { return std::fabs(a.err) < std::fabs(b.err); });
    const Pt &p = sorted[0], &q = sorted[1];
    double lnl = p.lnl;
    if (std::fabs(q.lnl - p.lnl) > 1e-9) lnl = p.lnl - p.err * (q.lnl - p.lnl) / (q.err - p.err);
    BendLevel r;
    r.n = n;
    r.h = std::exp(lnl) / n / 0.866;
    r.overhang = std::exp(lnl);
    r.Belastica = bendingFromOverhangElastica(r.overhang, m.density, thetaRad);
    r.ratio = r.Belastica / Bref;
    r.sims = static_cast<int>(pts.size());
    r.chordErrDeg = sorted[0].err;
    r.seconds = now() - t0;
    return r;
}

namespace {
// least-squares slope and intercept of y over x
void linfit(const std::vector<double>& x, const std::vector<double>& y, double& slope, double& icpt) {
    const double n = static_cast<double>(x.size());
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (size_t i = 0; i < x.size(); ++i) { sx += x[i]; sy += y[i]; sxx += x[i] * x[i]; sxy += x[i] * y[i]; }
    slope = (n * sxy - sx * sy) / (n * sxx - sx * sx);
    icpt = (sy - slope * sx) / n;
}

TensileDir runTensile(const FabricMaterial& m, const std::string& dir, double grain, double kRef, const std::vector<double>& strains,
                      double W, double L, double h) {
    const double t0 = now();
    TensileDir t;
    t.dir = dir;
    t.kInput = kRef;
    for (double e : strains) {
        const double T = e * kRef;
        const double mass = T * hangGeometry(W, L, h).widthEff / kG;
        HangSim s = simulateHanging(m, grain, W, L, h, mass);
        t.mass.push_back(mass);
        t.tension.push_back(T);
        t.strain.push_back(s.elongation / s.gauge);
    }
    double icpt = 0;
    linfit(t.strain, t.tension, t.kDerived, icpt);
    t.ratio = t.kDerived / kRef;
    t.seconds = now() - t0;
    return t;
}
}  // namespace

FabricBench runFabricBench(const FabricMaterial& m, const BenchConfig& cfg) {
    FabricBench b;
    b.mat = m;
    const double t0 = now();
    b.bendingLengthWarp = std::cbrt(m.bendWarp / (m.density * kG));
    b.bendingLengthWeft = std::cbrt(m.bendWeft / (m.density * kG));
    auto say = [&](const char* s, double sec) {
        if (cfg.verbose) std::fprintf(stderr, "  [%s] %s %.1f s\n", m.id.c_str(), s, sec);
    };

    if (cfg.doBending) {
        const struct { const char* name; double grain; } dirs[3] = {{"warp", 0.0}, {"weft", kPi / 2}, {"bias", kPi / 4}};
        for (const auto& d : dirs) {
            BendDir bd;
            bd.dir = d.name;
            bd.grainDeg = d.grain * 180.0 / kPi;
            bd.Binput = bendingAtAngleModel(m, d.grain);
            for (int n : cfg.bendLevels) bd.levels.push_back(benchBendingLevel(m, d.grain, n, cfg, bd.Binput));
            bd.headline = bd.levels.size() / 2;
            const BendLevel& hl = bd.levels[bd.headline];
            bd.Bpeirce = bendingFromOverhangPeirce(hl.overhang, m.density, cfg.angleDeg * kPi / 180);
            bd.Bhalf = bendingFromOverhangHalf(hl.overhang, m.density);
            bd.ratioPeirce = bd.Bpeirce / bd.Binput;
            bd.ratioHalf = bd.Bhalf / bd.Binput;
            b.bending.push_back(bd);
            double s = 0;
            for (const auto& l : bd.levels) s += l.seconds;
            say(("bending " + bd.dir).c_str(), s);
        }
    }
    if (cfg.doTensile) {
        const double E45 = 4.0 / (1.0 / m.tensileWarp + 1.0 / m.tensileWeft + 1.0 / m.shear);
        b.tensile.push_back(runTensile(m, "warp", 0.0, m.tensileWarp, cfg.warpWeftStrains, cfg.tensileWidth, cfg.tensileLength, cfg.tensileMesh));
        say("tensile warp", b.tensile.back().seconds);
        b.tensile.push_back(runTensile(m, "weft", kPi / 2, m.tensileWeft, cfg.warpWeftStrains, cfg.tensileWidth, cfg.tensileLength, cfg.tensileMesh));
        say("tensile weft", b.tensile.back().seconds);
        if (cfg.doShear) {
            b.tensile.push_back(runTensile(m, "bias", kPi / 4, E45, cfg.biasStrains, cfg.tensileWidth, cfg.biasLength, cfg.tensileMesh));
            say("bias extension", b.tensile.back().seconds);
            const double Eb = b.tensile.back().kDerived;
            b.shear.Ginput = m.shear;
            const double inv = 4.0 / Eb - 1.0 / m.tensileWarp - 1.0 / m.tensileWeft;
            b.shear.Gnaive = inv > 0 ? 1.0 / inv : std::numeric_limits<double>::quiet_NaN();
            b.shear.ratio = b.shear.Gnaive / m.shear;
        }
    }
    if (cfg.doDrape) {
        for (double h : cfg.drapeLevels) {
            b.drape.push_back({h, simulateDrape(m, h, cfg.drapeFrames)});
            say(("drape h=" + std::to_string(1000 * h).substr(0, 4) + " mm").c_str(), b.drape.back().sim.seconds);
        }
        b.drapeHeadline = b.drape.size() / 2;
    }
    b.secondsTotal = now() - t0;
    return b;
}

std::vector<BiasPoint> runBiasStrip(const FabricMaterial& m, const BenchConfig& cfg, const std::vector<double>& anglesDeg, int n) {
    std::vector<BiasPoint> out;
    for (double a : anglesDeg) {
        const double psi = a * kPi / 180.0;
        BiasPoint p;
        p.angleDeg = a;
        p.n = n;
        p.Bmodel = bendingAtAngleModel(m, psi);
        const BendLevel L = benchBendingLevel(m, psi, n, cfg, p.Bmodel);
        p.Bderived = L.Belastica;
        p.ratio = L.ratio;
        p.ratioLo = p.ratioHi = L.ratio;
        out.push_back(p);
        if (cfg.verbose) std::fprintf(stderr, "  [bias-strip %s] %5.1f deg: B model %.3e, derived %.3e, ratio %.3f\n", m.id.c_str(), a, p.Bmodel, p.Bderived, p.ratio);
    }
    return out;
}

// =====================================================================================================
// output
// =====================================================================================================
std::string jnum(double v) {
    if (!std::isfinite(v)) return "null";
    char buf[48];
    std::snprintf(buf, sizeof buf, "%.6g", v);
    return buf;
}
std::string jstr(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if (c == '\n') o += "\\n";
        else o += c;
    }
    return o + "\"";
}

std::string benchToJson(const FabricBench& b, const BenchConfig& cfg) {
    std::ostringstream o;
    const FabricMaterial& m = b.mat;
    o << "{\n  \"fabric\": " << jstr(m.id) << ",\n  \"name\": " << jstr(m.name) << ",\n";
    o << "  \"notes\": \"Simulated-preset validation. Textbook formulas marked UNVERIFIED in the bench source (tools/fabric_bench). The cantilever angle is the chord clamp->tip.\",\n";
    o << "  \"cantilever_angle_deg\": " << jnum(cfg.angleDeg) << ",\n  \"strip_width_m\": " << jnum(cfg.stripWidth) << ",\n";
    o << "  \"input\": {\"density_kg_m2\": " << jnum(m.density) << ", \"thickness_m\": " << jnum(m.thickness) << ", \"tensile_warp_N_m\": " << jnum(m.tensileWarp)
      << ", \"tensile_weft_N_m\": " << jnum(m.tensileWeft) << ", \"shear_N_m_rad\": " << jnum(m.shear) << ", \"bending_warp_Nm\": " << jnum(m.bendWarp)
      << ", \"bending_weft_Nm\": " << jnum(m.bendWeft) << ", \"compression_ratio\": " << jnum(m.compressionRatio) << "},\n";
    o << "  \"bending_length_input_m\": {\"warp\": " << jnum(b.bendingLengthWarp) << ", \"weft\": " << jnum(b.bendingLengthWeft) << "},\n";
    o << "  \"bending\": [";
    for (size_t i = 0; i < b.bending.size(); ++i) {
        const BendDir& d = b.bending[i];
        o << (i ? "," : "") << "\n    {\"dir\": " << jstr(d.dir) << ", \"grain_deg\": " << jnum(d.grainDeg) << ", \"B_reference_Nm\": " << jnum(d.Binput)
          << ", \"reference_kind\": " << jstr(d.dir == "bias" ? "compliance-mixing model of cloth.cpp (not an input)" : "preset input") << ",\n     \"headline_level\": " << d.headline
          << ", \"B_peirce_Nm\": " << jnum(d.Bpeirce) << ", \"ratio_peirce\": " << jnum(d.ratioPeirce) << ", \"B_half_overhang_Nm\": " << jnum(d.Bhalf)
          << ", \"ratio_half_overhang\": " << jnum(d.ratioHalf) << ",\n     \"levels\": [";
        for (size_t k = 0; k < d.levels.size(); ++k) {
            const BendLevel& l = d.levels[k];
            o << (k ? ", " : "") << "{\"elements_along_overhang\": " << l.n << ", \"h_m\": " << jnum(l.h) << ", \"overhang_m\": " << jnum(l.overhang)
              << ", \"B_elastica_Nm\": " << jnum(l.Belastica) << ", \"ratio\": " << jnum(l.ratio) << ", \"sims\": " << l.sims << ", \"seconds\": " << jnum(l.seconds) << "}";
        }
        o << "]}";
    }
    o << "\n  ],\n  \"tensile\": [";
    for (size_t i = 0; i < b.tensile.size(); ++i) {
        const TensileDir& t = b.tensile[i];
        o << (i ? "," : "") << "\n    {\"dir\": " << jstr(t.dir) << ", \"k_reference_N_m\": " << jnum(t.kInput) << ", \"k_derived_N_m\": " << jnum(t.kDerived)
          << ", \"ratio\": " << jnum(t.ratio) << ", \"seconds\": " << jnum(t.seconds) << ", \"loads\": [";
        for (size_t k = 0; k < t.mass.size(); ++k)
            o << (k ? ", " : "") << "{\"mass_kg\": " << jnum(t.mass[k]) << ", \"tension_N_m\": " << jnum(t.tension[k]) << ", \"strain\": " << jnum(t.strain[k]) << "}";
        o << "]}";
    }
    o << "\n  ],\n  \"shear\": {\"G_input_N_m_rad\": " << jnum(b.shear.Ginput) << ", \"G_bias_extension_naive_N_m_rad\": " << jnum(b.shear.Gnaive)
      << ", \"ratio\": " << jnum(b.shear.ratio) << ", \"method\": \"bias extension, 1/G = 4/E45 - 1/ku - 1/kv (uniform uniaxial stress; ignores clamp zones)\"},\n";
    o << "  \"drape\": [";
    for (size_t i = 0; i < b.drape.size(); ++i) {
        const DrapeSim& d = b.drape[i].sim;
        o << (i ? "," : "") << "\n    {\"h_m\": " << jnum(b.drape[i].h) << ", \"drape_coefficient_pct\": " << jnum(d.dcPercent) << ", \"folds\": " << d.folds << ", \"r_min_m\": " << jnum(d.rMin)
          << ", \"r_max_m\": " << jnum(d.rMax) << ", \"shadow_area_m2\": " << jnum(d.shadowArea) << ", \"frames\": " << d.frames << ", \"kinetic_energy_J\": " << jnum(d.ke)
          << ", \"min_clearance_m\": " << jnum(d.clearance) << ", \"seconds\": " << jnum(d.seconds) << "}";
    }
    o << "\n  ],\n  \"drape_headline_level\": " << b.drapeHeadline << ",\n  \"seconds_total\": " << jnum(b.secondsTotal) << "\n}\n";
    return o.str();
}

namespace {
std::string fmt(const char* f, double v) {
    if (!std::isfinite(v)) return "n/a";
    char buf[64];
    std::snprintf(buf, sizeof buf, f, v);
    return buf;
}
}  // namespace

std::string benchToMarkdown(const FabricBench& b, const BenchConfig& cfg) {
    std::ostringstream o;
    const FabricMaterial& m = b.mat;
    o << "### " << m.id << " (" << m.name << ")\n\n";
    if (!b.bending.empty()) {
        o << "Cantilever, chord angle " << cfg.angleDeg << " deg, strip " << 1000 * cfg.stripWidth << " mm wide. B from the exact elastica relation; ratio = derived / reference.\n\n";
        o << "| direction | B reference (N m) | overhang l* (mm) | B derived (N m) | ratio | spread over mesh levels | Peirce ratio (UNVERIFIED) | l/2 ratio (UNVERIFIED) |\n|---|---|---|---|---|---|---|---|\n";
        for (const BendDir& d : b.bending) {
            double lo = 1e30, hi = -1e30;
            for (const auto& l : d.levels) { lo = std::min(lo, l.ratio); hi = std::max(hi, l.ratio); }
            const BendLevel& h = d.levels[d.headline];
            o << "| " << d.dir << (d.dir == "bias" ? " (ref = model)" : "") << " | " << fmt("%.3g", d.Binput) << " | " << fmt("%.1f", 1000 * h.overhang) << " | " << fmt("%.3g", h.Belastica)
              << " | " << fmt("%.3f", h.ratio) << " | " << fmt("%.3f", lo) << " .. " << fmt("%.3f", hi) << " | " << fmt("%.3f", d.ratioPeirce) << " | " << fmt("%.3f", d.ratioHalf) << " |\n";
        }
        o << "\nMesh levels (elements along the overhang -> ratio): ";
        for (const BendDir& d : b.bending) {
            o << d.dir << " ";
            for (const auto& l : d.levels) o << l.n << "->" << fmt("%.3f", l.ratio) << " ";
            o << "; ";
        }
        o << "\n\n";
    }
    if (!b.tensile.empty()) {
        o << "Hanging strip with weights (50 mm wide, 300 mm long). k = d(tension per width)/d(strain) from a linear fit over the loads.\n\n";
        o << "| direction | reference k (N/m) | derived k (N/m) | ratio | strains (%) | seconds |\n|---|---|---|---|---|---|\n";
        for (const TensileDir& t : b.tensile) {
            o << "| " << t.dir << (t.dir == "bias" ? " (ref = 4/(1/ku+1/kv+1/G))" : "") << " | " << fmt("%.4g", t.kInput) << " | " << fmt("%.4g", t.kDerived) << " | " << fmt("%.3f", t.ratio) << " | ";
            for (size_t k = 0; k < t.strain.size(); ++k) o << (k ? ", " : "") << fmt("%.3f", 100 * t.strain[k]);
            o << " | " << fmt("%.1f", t.seconds) << " |\n";
        }
        if (b.shear.Ginput > 0)
            o << "\nShear from bias extension (naive uniform-stress formula): G = " << fmt("%.3g", b.shear.Gnaive) << " N/m/rad vs input " << fmt("%.3g", b.shear.Ginput)
              << ", ratio " << fmt("%.3f", b.shear.ratio) << ".\n";
        o << "\n";
    }
    if (!b.drape.empty()) {
        o << "Cusick drape (30 cm disc, 18 cm pedestal).\n\n| edge h (mm) | DC (%) | folds | r_min (mm) | r_max (mm) | frames | KE (J) | seconds |\n|---|---|---|---|---|---|---|---|\n";
        for (const auto& d : b.drape)
            o << "| " << fmt("%.1f", 1000 * d.h) << " | " << fmt("%.1f", d.sim.dcPercent) << " | " << d.sim.folds << " | " << fmt("%.0f", 1000 * d.sim.rMin) << " | "
              << fmt("%.0f", 1000 * d.sim.rMax) << " | " << d.sim.frames << " | " << fmt("%.1e", d.sim.ke) << " | " << fmt("%.1f", d.sim.seconds) << " |\n";
        o << "\n";
    }
    o << "Total bench time: " << fmt("%.0f", b.secondsTotal) << " s.\n";
    return o.str();
}

std::string summaryMarkdown(const std::vector<FabricBench>& all, const BenchConfig& cfg) {
    std::ostringstream o;
    o << "| fabric | B warp in (N m) | B warp derived/in | B weft derived/in | B bias derived/model | k warp derived/in | k weft derived/in | bias-stretch derived/ideal | G naive/in | DC % (spread) | bench s |\n"
      << "|---|---|---|---|---|---|---|---|---|---|---|\n";
    for (const FabricBench& b : all) {
        auto bend = [&](size_t i) { return i < b.bending.size() ? b.bending[i].levels[b.bending[i].headline].ratio : std::nan(""); };
        auto ten = [&](size_t i) { return i < b.tensile.size() ? b.tensile[i].ratio : std::nan(""); };
        double dclo = 1e30, dchi = -1e30, dc = std::nan("");
        for (const auto& d : b.drape) { dclo = std::min(dclo, d.sim.dcPercent); dchi = std::max(dchi, d.sim.dcPercent); }
        if (!b.drape.empty()) dc = b.drape[b.drapeHeadline].sim.dcPercent;
        o << "| " << b.mat.id << " | " << fmt("%.2g", b.mat.bendWarp) << " | " << fmt("%.3f", bend(0)) << " | " << fmt("%.3f", bend(1)) << " | " << fmt("%.3f", bend(2)) << " | "
          << fmt("%.3f", ten(0)) << " | " << fmt("%.3f", ten(1)) << " | " << fmt("%.3f", ten(2)) << " | " << fmt("%.2f", b.shear.ratio) << " | " << fmt("%.1f", dc);
        if (!b.drape.empty()) o << " (" << fmt("%.1f", dclo) << ".." << fmt("%.1f", dchi) << ")";
        o << " | " << fmt("%.0f", b.secondsTotal) << " |\n";
    }
    (void)cfg;
    return o.str();
}

}  // namespace fb
