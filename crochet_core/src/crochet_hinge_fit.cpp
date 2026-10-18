// See failoring/crochet/hinge_fit.hpp.
#include "failoring/crochet/hinge_fit.hpp"

#include <cmath>
#include <cstdio>

#include "failoring/crochet/layout.hpp"
#include "failoring/crochet/model.hpp"
#include "failoring/crochet/relax.hpp"

namespace fl {
namespace {

int addRow(StitchGraph& g, bool turned) {
    Row r;
    r.id = static_cast<int>(g.rows.size());
    r.piece = "main";
    r.kind = "row";
    r.turned = turned;
    g.rows.push_back(r);
    return r.id;
}

int addStitch(StitchGraph& g, const char* type, int row, std::vector<int> targets, const char* loop) {
    Stitch s;
    s.id = static_cast<int>(g.st.size());
    s.type = type;
    s.piece = "main";
    s.row = row;
    for (const Stitch& q : g.st) s.index += q.row == row;
    s.loop = loop;
    s.loopRs = s.loop;
    s.targets = std::move(targets);
    s.prev = g.st.empty() ? -1 : s.id - 1;
    g.st.push_back(s);
    return s.id;
}

}  // namespace

StitchGraph makeHingeSwatch(int cols, int rowsBelow, int rowsAbove, const std::string& construction) {
    StitchGraph g;
    const int hingeRow = rowsBelow + 1, rows = rowsBelow + 1 + rowsAbove;
    const int r0 = addRow(g, true);
    for (int i = 0; i <= cols; ++i) addStitch(g, "ch", r0, {}, "space");
    std::vector<int> prev;
    for (int c = cols - 1; c >= 0; --c) prev.push_back(c);
    for (int r = 1; r <= rows; ++r) {
        const int row = addRow(g, true);
        std::vector<int> cur;
        if (r > 1) addStitch(g, "ch", row, {}, "space");  // the turning chain
        const bool hinge = r == hingeRow;
        const char* type = hinge && construction == "slst" ? "slst" : "sc";
        const char* loop = hinge ? (construction == "flo" ? "flo" : "blo") : "both";
        for (int c = 0; c < cols; ++c) cur.push_back(addStitch(g, type, row, {r == 1 ? prev[c] : prev[cols - 1 - c]}, loop));
        prev = cur;
    }
    resolveFacing(g);
    return g;
}

Real simulateDroop(const HingeSetup& setup, const Yarn& yarn, Real bendScale, Real restAngleDeg, bool* converged) {
    const StitchGraph g = makeHingeSwatch(setup.cols, setup.rowsBelow, setup.rowsAbove, setup.construction);
    Calibration cal = setup.fabric != nullptr ? *setup.fabric : Calibration();
    cal.hinge[setup.construction] = HingeCalibration{bendScale, restAngleDeg};
    ModelOptions mo;
    mo.hook_m = setup.hookMm * 1e-3;
    mo.tension = setup.tension;
    mo.calibration = &cal;
    CrochetModel cm = buildCrochetModel(g, yarn, mo);
    layoutModel(cm, g);
    // the sheet on a table: the rows run along +x (the layout runs them along -y), the right side up (+y), the width along -z
    for (Vec3& p : cm.mesh.x0) p = Vec3(-p.y, p.z, -p.x);
    RelaxOptions ro;
    ro.gravity = 9.81;
    ro.selfCollision = false;
    ro.seamRampFrames = 1;
    ro.keTarget = 0;  // never "converged" by kinetic energy: the damped sheet creeps with a tiny KE long after; the droop itself decides
    ro.maxFrames = setup.maxFrames;
    for (const Stitch& s : g.st)
        if (s.row <= setup.rowsBelow && cm.topo.stitchVertex[s.id] >= 0) ro.pin.push_back(cm.topo.stitchVertex[s.id]);
    const int lastRow = setup.rowsBelow + 1 + setup.rowsAbove;
    std::vector<int> hingeRow, tipRow;
    for (const Stitch& s : g.st) {
        const int v = cm.topo.stitchVertex[s.id];
        if (v < 0) continue;
        if (s.row == setup.rowsBelow) hingeRow.push_back(v);
        else if (s.row == lastRow) tipRow.push_back(v);
    }
    RelaxRun run(cm, ro);
    auto droop = [&]() {
        Vec3 h(0, 0, 0), t(0, 0, 0);
        for (const int v : hingeRow) h += run.positions()[v];
        for (const int v : tipRow) t += run.positions()[v];
        h = h * (1.0 / hingeRow.size());
        t = t * (1.0 / tipRow.size());
        return std::atan2(h.y - t.y, t.x - h.x) * 180.0 / 3.14159265358979323846;
    };
    // step in chunks until the droop stays within 0.005 deg over two chunks in a row
    const int chunk = 50;
    Real last = droop();
    int still = 0;
    bool done = false;
    for (int f = 0; f < ro.maxFrames && !done; f += chunk) {
        run.step(chunk);
        const Real d = droop();
        still = std::fabs(d - last) < 0.005 ? still + 1 : 0;
        last = d;
        done = still >= 2;
    }
    if (converged != nullptr) *converged = done;
    return last;
}

HingeFit fitHingeBendScale(const HingeSetup& setup, const Yarn& yarn, Real restAngleDeg, Real measuredDroopDeg, Real tolDeg, int maxRuns) {
    HingeFit out;
    auto run = [&](Real scale) {
        ++out.runs;
        return simulateDroop(setup, yarn, scale, restAngleDeg);
    };
    auto fmt = [](const char* f, double a, double b = 0, double c = 0) {
        char buf[300];
        std::snprintf(buf, sizeof buf, f, a, b, c);
        return std::string(buf);
    };
    Real lo = kMinBendScale, hi = kMaxBendScale;
    const Real dSoft = run(lo), dStiff = run(hi);  // the droop falls as the hinge stiffens
    if (measuredDroopDeg >= dSoft) {
        out.bendScale = lo;
        out.droopDeg = dSoft;
        out.notes.push_back(fmt("the measured droop %.1f deg is more than even a hinge as soft as bendScale %.2f gives (%.1f deg): check the rest angle", measuredDroopDeg, lo, dSoft));
        return out;
    }
    if (measuredDroopDeg <= dStiff) {
        out.bendScale = hi;
        out.droopDeg = dStiff;
        out.notes.push_back(fmt("the measured droop %.1f deg is less than even a hinge as stiff as bendScale %.1f gives (%.1f deg): check the rest angle", measuredDroopDeg, hi, dStiff));
        return out;
    }
    out.bracketed = true;
    Real mid = std::sqrt(lo * hi), d = 0;
    for (int i = 0; i < maxRuns; ++i) {
        mid = std::sqrt(lo * hi);
        d = run(mid);
        if (std::fabs(d - measuredDroopDeg) <= tolDeg) break;
        if (d > measuredDroopDeg) lo = mid;  // still droops too much: stiffer
        else hi = mid;
    }
    out.bendScale = mid;
    out.droopDeg = d;
    if (mid > 0.8) out.notes.push_back("a stiff hinge: the droop hardly changes with bendScale here, so this value is only roughly pinned");
    out.notes.push_back(fmt("bendScale %.3f gives a droop of %.2f deg", mid, d) + fmt(" (measured %.2f deg), %.0f simulator runs", measuredDroopDeg, out.runs));
    return out;
}

}  // namespace fl
