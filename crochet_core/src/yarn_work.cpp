// Yarn mode Y2: the work made one hook action at a time. See include/failoring/crochet/yarn_work.hpp.
#include "failoring/crochet/yarn_work.hpp"

#include <algorithm>
#include <cmath>
#include <map>

namespace fl {

namespace {

// the points of a polyline resampled so that no step is longer than `step`
std::vector<Vec3> resample(const Vec3& from, const std::vector<Vec3>& way, Real step) {
    std::vector<Vec3> out;
    Vec3 prev = from;
    for (const Vec3& w : way) {
        const Real d = length(w - prev);
        const int n = std::max(1, (int)std::ceil(d / step - 1e-9));
        for (int i = 1; i <= n; ++i) out.push_back(prev + (w - prev) * ((Real)i / n));
        prev = w;
    }
    return out;
}

}  // namespace

YarnWork::YarnWork() {
    rod.params.gravity = Vec3{};   // the work floats: the shape of the loops is what is being looked at
    rod.params.damping = 25.0;
    rod.params.substeps = 4;
    rod.params.iterations = 2;
    rod.params.EA = 30.0;
    rod.params.EI = 4.0e-6;
    rod.params.GJ = 4.0e-6;
}

Vec3 YarnWork::endPosition() const { return rod.x.back(); }

int YarnWork::addLoop(int p0, int p1, bool live) {
    YarnLoop l;
    l.p0 = l.r0 = p0;
    l.p1 = l.r1 = p1;
    l.live = live;
    l.row = row;
    loops.push_back(l);
    const int id = (int)loops.size() - 1;
    if (live) hook.push_back(id);
    return id;
}

EyeFrame YarnWork::eye(int loop) const {
    const YarnLoop& l = loops[loop];
    EyeFrame e;
    const int n = l.r1 - l.r0 + 1;
    Vec3 c{};
    for (int i = l.r0; i <= l.r1; ++i) c += rod.x[i];
    c = c / (Real)n;
    Vec3 area{};
    for (int i = l.r0; i <= l.r1; ++i) {
        const Vec3& a = rod.x[i];
        const Vec3& b = rod.x[i == l.r1 ? l.r0 : i + 1];
        area += cross(a - c, b - c);
    }
    e.centre = c;
    e.normal = length2(area) > 1e-30 ? normalized(area) : Vec3{0, 0, 1};
    e.radius = std::sqrt(0.5 * length(area) / kPi);
    return e;
}

YarnWork::HookPose YarnWork::hookPose() const {
    HookPose h;
    if (hook.empty()) return h;
    const EyeFrame e = eye(hook.back());
    h.tip = e.centre;
    h.axis = e.normal;
    h.valid = true;
    return h;
}

void YarnWork::appendPath(const std::vector<Vec3>& way) {
    const std::vector<Vec3> pts = resample(rod.x.back(), way, wp.segmentLength);
    const int first = rod.segmentCount();
    rod.extendChain(pts);
    for (int s = first; s < rod.segmentCount(); ++s) rod.restLen[s] *= wp.tighten * slack_;
}

// A finger through the eyes of `eyesLoops` in the order given. The hook's axis goes through each eye along its normal, from the side the
// yarn is on. Both legs pass each eye beside the shaft, side by side (leg a on the side of the yarn's end); beyond the last eye they flare
// out and the yarn goes once round the shaft (the ring) and comes back as leg b. The yarn then runs on a little toward the ball.
int YarnWork::finger(const std::vector<int>& eyesLoops, bool consume, const Vec3* axisHint) {
    const Real r = rod.params.radius;
    const Real off = wp.hookRadius + r + 0.0004;   // the legs' distance from the shaft's axis
    struct Pass { Vec3 c, a, w, u; };               // eye centre, axis direction, toward the legs (in the eye's plane), across the legs
    std::vector<Pass> passes;
    Vec3 cursor = endPosition();
    for (int li : eyesLoops) {
        const EyeFrame e = eye(li);
        Vec3 a = e.normal;
        if (axisHint != nullptr && length2(*axisHint) > 0.5) a = normalized(*axisHint);
        // through the eye away from where the yarn is
        if (dot(cursor - e.centre, a) > 0) a = -a;
        Vec3 w = (cursor - e.centre) - a * dot(cursor - e.centre, a);
        if (length2(w) < 1e-10) w = upDir - a * dot(upDir, a);
        if (length2(w) < 1e-10) w = Vec3{1, 0, 0} - a * dot(Vec3{1, 0, 0}, a);
        w = normalized(w);
        const Vec3 u = cross(a, w);
        passes.push_back({e.centre, a, w, u});
        cursor = e.centre + a * wp.approach + w * off;
    }
    if (passes.empty()) return -1;
    const Real eps = std::asin(std::min(0.9, wp.legGap / off));   // the legs sit at +-eps round the shaft
    std::vector<Vec3> way, travel;
    Real travelStraight = 0;
    {   // the working yarn goes from its end round the outside of the work to the first eye (never through the eyes of the loops on the way)
        const Pass& f = passes.front();
        const Vec3 entry = f.c + f.w * off + f.u * wp.legGap - f.a * wp.approach;
        Vec3 lift = upDir - f.a * dot(upDir, f.a);
        if (length2(lift) < 1e-8) lift = f.w;
        lift = normalized(lift) * wp.travelLift;
        travel.push_back(endPosition() + lift);
        travel.push_back(entry + lift);
        travel.push_back(entry);
        travelStraight = length(entry - endPosition());
    }
    for (const Pass& p : passes) {
        const Vec3 leg = p.c + p.w * off + p.u * wp.legGap;
        way.push_back(leg - p.a * wp.approach);
        way.push_back(leg);
        way.push_back(leg + p.a * wp.approach);
    }
    const Pass& L = passes.back();
    auto at = [&](Real z, Real rho, Real phi) { return L.c + L.a * z + (L.w * std::cos(phi) + L.u * std::sin(phi)) * rho; };
    const Real zf = wp.approach + wp.fingerLength, zr = zf + wp.flare;
    const Real pitch = 2.4 * r;   // the ring is a helix of a strand's width so that it does not cut across its own legs
    way.push_back(at(zf, off, eps));
    for (int k = 1; k <= 3; ++k) {
        const Real f = k / 3.0, sm = f * f * (3.0 - 2.0 * f);
        way.push_back(at(zf + wp.flare * f, off + (wp.ringRadius - off) * sm, eps));
    }
    const int ringSteps = 22;
    const Real sweep = 2.0 * kPi - 2.0 * eps;
    const int ringFirstWay = (int)way.size();   // the ring starts after this many waypoints
    for (int k = 1; k <= ringSteps; ++k) way.push_back(at(zr + pitch * k / ringSteps, wp.ringRadius, eps + sweep * k / ringSteps));
    const int ringLastWay = (int)way.size() - 1;
    for (int k = 1; k <= 3; ++k) {
        const Real f = k / 3.0, sm = f * f * (3.0 - 2.0 * f);
        way.push_back(at(zr + pitch - (zr + pitch - zf) * f, wp.ringRadius + (off - wp.ringRadius) * sm, -eps));
    }
    for (int k = (int)passes.size() - 1; k >= 0; --k) {
        const Pass& p = passes[k];
        const Vec3 leg = p.c + p.w * off - p.u * wp.legGap;
        way.push_back(leg + p.a * wp.approach);
        way.push_back(leg);
        way.push_back(leg - p.a * wp.approach);
    }
    rod.statics.clear();  // the hook leaves the old loop before the new one is pulled through
    const int e0 = rod.particleCount() - 1;
    {   // the working yarn to the first eye: laid round the outside, but pulled taut (it is the yarn running to the hook)
        const int t0 = rod.segmentCount();
        appendPath(travel);
        Real made = 0;
        for (int sIdx = t0; sIdx < rod.segmentCount(); ++sIdx) made += rod.restLen[sIdx];
        const Real want = std::max(travelStraight * wp.travelSlack, 1e-4);
        if (made > want)
            for (int sIdx = t0; sIdx < rod.segmentCount(); ++sIdx) rod.restLen[sIdx] *= want / made;
    }
    // where the ring starts and ends among the particles: appendPath resamples, so read the counts off after each part
    std::vector<Vec3> head(way.begin(), way.begin() + ringFirstWay);
    std::vector<Vec3> ring(way.begin() + ringFirstWay, way.begin() + ringLastWay + 1);
    std::vector<Vec3> tail(way.begin() + ringLastWay + 1, way.end());
    appendPath(head);
    const int r0 = rod.particleCount();
    appendPath(ring);
    const int r1 = rod.particleCount() - 1;
    appendPath(tail);
    const int p1 = rod.particleCount() - 1;
    rod.statics.push_back({L.c - L.a * (0.5 * wp.approach), L.c + L.a * (zr + 0.6 * wp.ringRadius), wp.hookRadius});
    const Pass& first = passes.front();
    appendPath({first.c + first.w * off - first.u * wp.legGap - first.a * (wp.approach + wp.workingRun)});
    for (int li : eyesLoops) {   // the loops the finger went through are pulled in onto it
        YarnLoop& L = loops[li];
        const Real target = std::max(wp.closeFloor, L.ringScale * wp.closeFactor);
        const Real f = target / L.ringScale;
        for (int sIdx = L.r0; sIdx < L.r1; ++sIdx) rod.restLen[sIdx] *= f;   // segment sIdx joins particles sIdx and sIdx + 1 (one chain)
        L.ringScale = target;
    }
    if (consume)
        for (int li : eyesLoops) {
            loops[li].live = false;
            hook.erase(std::remove(hook.begin(), hook.end(), li), hook.end());
        }
    const int id = addLoop(e0 + 1, p1, true);
    loops[id].r0 = r0;
    loops[id].r1 = r1;
    loops[id].through = eyesLoops;
    return id;
}

// A loop with its ring put at `ringC` (plane square to `ringN`) and its legs through the eyes of `eyesLoops`, in order.
int YarnWork::loopAt(const Vec3& ringC, const Vec3& ringN0, const std::vector<int>& eyesLoops, bool consume) {
    const Real r = rod.params.radius;
    const Real off = wp.hookRadius + r + 0.0004;
    const Vec3 ringN = normalized(ringN0);
    struct Pass { Vec3 c, a, u; };
    std::vector<Pass> passes;
    Vec3 cursor = endPosition();
    for (size_t k = 0; k < eyesLoops.size(); ++k) {
        const EyeFrame e = eye(eyesLoops[k]);
        const Vec3 target = (k + 1 < eyesLoops.size()) ? eye(eyesLoops[k + 1]).centre : ringC;
        Vec3 toward = normalized(target - e.centre);
        Vec3 n = e.normal;
        if (dot(toward, n) < 0) n = -n;
        // legs pass towards the target; a very slanting line is bent towards the eye's normal so that it really goes through
        Vec3 a = normalized(toward * 0.5 + n * 0.5);
        if (dot(toward, n) > 0.75) a = toward;
        Vec3 u = cross(a, ringN);
        if (length2(u) < 1e-8) u = cross(a, upDir);
        u = normalized(u);
        const Real sg = dot(cursor - e.centre, u) >= 0 ? 1.0 : -1.0;
        passes.push_back({e.centre, a, u * sg});
        cursor = e.centre + a * wp.approach;
    }
    if (passes.empty()) return -1;
    std::vector<Vec3> way, travel;
    Real travelStraight = 0;
    {
        const Pass& f = passes.front();
        const Vec3 entry = f.c + f.u * wp.legGap - f.a * wp.approach;
        travel.push_back(entry);
        travelStraight = length(entry - endPosition());
    }
    for (const Pass& p : passes) {
        const Vec3 leg = p.c + p.u * wp.legGap;
        way.push_back(leg - p.a * wp.approach);
        way.push_back(leg);
        way.push_back(leg + p.a * wp.approach);
    }
    // the ring: legs flare out to it from the side of the last eye
    const Pass& L = passes.back();
    Vec3 w0 = (L.c - ringC) - ringN * dot(L.c - ringC, ringN);
    if (length2(w0) < 1e-10) w0 = L.u;
    w0 = normalized(w0);
    const Vec3 v0 = cross(ringN, w0);   // (w0, v0, ringN) right handed
    const Real eps = std::asin(std::min(0.9, wp.legGap / std::max(off, 1e-6)));
    auto onRing = [&](Real phi, Real rho) { return ringC + (w0 * std::cos(phi) + v0 * std::sin(phi)) * rho; };
    const Real pitch = 2.4 * r;
    way.push_back(L.c + L.a * wp.approach + L.u * wp.legGap);
    const int ringSteps = 22;
    const Real sweep = 2.0 * kPi - 2.0 * eps;
    // from the eye to the ring's attachment: a short run, spreading from the leg gap to the ring
    for (int k = 1; k <= 2; ++k) {
        const Real f = k / 3.0;
        const Vec3 from = L.c + L.a * wp.approach + L.u * wp.legGap;
        const Vec3 to = onRing(eps, wp.ringRadius) + ringN * 0.0;
        way.push_back(from + (to - from) * f);
    }
    const int ringFirstWay = (int)way.size();
    for (int k = 0; k <= ringSteps; ++k) way.push_back(onRing(eps + sweep * k / ringSteps, wp.ringRadius) + ringN * (pitch * k / ringSteps));
    const int ringLastWay = (int)way.size() - 1;
    for (int k = 2; k >= 1; --k) {
        const Real f = k / 3.0;
        const Vec3 from = L.c + L.a * wp.approach - L.u * wp.legGap;
        const Vec3 to = onRing(-eps, wp.ringRadius) + ringN * pitch;
        way.push_back(from + (to - from) * f);
    }
    for (int k = (int)passes.size() - 1; k >= 0; --k) {
        const Pass& p = passes[k];
        const Vec3 leg = p.c - p.u * wp.legGap;
        way.push_back(leg + p.a * wp.approach);
        way.push_back(leg);
        way.push_back(leg - p.a * wp.approach);
    }
    rod.statics.clear();
    {   // the working yarn to the first eye: pulled taut
        const int t0 = rod.segmentCount();
        appendPath(travel);
        Real made = 0;
        for (int sIdx = t0; sIdx < rod.segmentCount(); ++sIdx) made += rod.restLen[sIdx];
        const Real want = std::max(travelStraight * wp.travelSlack, 1e-4);
        if (made > want)
            for (int sIdx = t0; sIdx < rod.segmentCount(); ++sIdx) rod.restLen[sIdx] *= want / made;
    }
    const int e0 = rod.particleCount() - 1;
    std::vector<Vec3> head(way.begin(), way.begin() + ringFirstWay);
    std::vector<Vec3> ring(way.begin() + ringFirstWay, way.begin() + ringLastWay + 1);
    std::vector<Vec3> tail(way.begin() + ringLastWay + 1, way.end());
    appendPath(head);
    const int r0 = rod.particleCount();
    appendPath(ring);
    const int r1 = rod.particleCount() - 1;
    appendPath(tail);
    const int p1 = rod.particleCount() - 1;
    if (wp.hookRadius > 1e-3) rod.statics.push_back({ringC - ringN * (0.6 * wp.ringRadius), ringC + ringN * (pitch + 0.6 * wp.ringRadius), wp.hookRadius});
    const Pass& first = passes.front();
    appendPath({first.c - first.u * wp.legGap - first.a * (wp.approach + wp.workingRun)});
    for (int li : eyesLoops) {   // the loops the finger went through are pulled in onto it
        YarnLoop& Lp = loops[li];
        const Real target = std::max(wp.closeFloor, Lp.ringScale * wp.closeFactor);
        const Real f = target / Lp.ringScale;
        for (int sIdx = Lp.r0; sIdx < Lp.r1; ++sIdx) rod.restLen[sIdx] *= f;
        Lp.ringScale = target;
    }
    if (consume)
        for (int li : eyesLoops) {
            loops[li].live = false;
            hook.erase(std::remove(hook.begin(), hook.end(), li), hook.end());
        }
    const int id = addLoop(e0 + 1, p1, true);
    loops[id].r0 = r0;
    loops[id].r1 = r1;
    loops[id].through = eyesLoops;
    return id;
}

int YarnWork::slipKnot(const Vec3& at) {
    // the tail runs in along the work direction, makes one turn round that axis (a helix a strand and a bit long, so it does not cut itself:
    // the ring of the slip knot), then the yarn leaves round the outside and a finger of it goes through the ring
    const Real R = 0.0055, r = rod.params.radius;
    const Vec3 ex = progress, ey = upDir, ez = cross(progress, upDir);
    std::vector<Vec3> pts;
    pts.push_back(at - ex * 0.030 + ey * R);
    pts.push_back(at + ey * R);
    const int n = 16;
    for (int i = 1; i <= n; ++i) {
        const Real a = 2.0 * kPi * i / n;
        pts.push_back(at + ex * (2.4 * r * i / n) + ey * (R * std::cos(a)) + ez * (R * std::sin(a)));
    }
    const std::vector<Vec3> nodes = resample(pts.front(), std::vector<Vec3>(pts.begin() + 1, pts.end()), wp.segmentLength);
    std::vector<Vec3> all;
    all.push_back(pts.front());
    all.insert(all.end(), nodes.begin(), nodes.end());
    rod.addChain(all, false, true);   // the ring keeps the bend it is made with
    slack_ = 1.0;
    for (int s = 0; s < rod.segmentCount(); ++s) rod.restLen[s] *= wp.tighten;
    rod.pin(0);  // the tail end stays where it is
    const int ring = addLoop(0, rod.particleCount() - 1, false);
    // find where the ring starts: the first particle past the straight tail
    loops[ring].r0 = std::min(rod.particleCount() - 2, (int)std::ceil(0.030 / wp.segmentLength));
    // out round the ring and back to the side the finger will come from
    appendPath({at + ex * (2.4 * r) + ey * (R * 1.7), at - ex * (4.0 * r) + ey * (R * 1.7), at - ex * (4.0 * r) + ey * (R * 0.3)});
    origin_ = at;
    chainCount_ = 0;
    target_.clear();
    made_.clear();
    cursor_ = 0;
    log_.clear();
    const int id = loopAt(at + progress * wp.stitchWidth + zAxis() * wp.layerGap, zAxis(), {ring}, false);
    settle();
    return id;
}

int YarnWork::pullThrough(int k) {
    k = std::min<int>(k, (int)hook.size());
    std::vector<int> eyes;
    for (int i = 0; i < k; ++i) eyes.push_back(hook[hook.size() - 1 - i]);  // the tip's loop first
    const int id = finger(eyes, true);
    settle();
    return id;
}

int YarnWork::chain() {
    // the next ring: one stitch on along the row, alternating between the two layers
    ++chainCount_;
    const Vec3 c = origin_ + progress * (wp.stitchWidth * (chainCount_ + 1)) + zAxis() * ((chainCount_ % 2 == 0 ? 1.0 : -1.0) * wp.layerGap);
    const int id = loopAt(c, zAxis(), {hook.back()}, true);
    settle();
    return id;
}

int YarnWork::pullUp(int opening) {
    const int id = finger({opening}, false);
    settle();
    return id;
}

int YarnWork::sc(int opening) {
    // the opening is the eye of a loop in the row below. The loop pulled up through it stands half a row above, on the worker's side of the
    // work; the loop pulled through that one and the loop on the hook stands a whole row up, half a stitch along, on the other side. The
    // position comes from the opening's place along the row; the layer from the row (the rows alternate sides as the work is turned over).
    const EyeFrame b = eye(opening);
    const Vec3 z = zAxis();
    const Real layer = (row % 2 == 1 ? 1.0 : -1.0) * wp.layerGap;
    const Vec3 base = b.centre - z * dot(b.centre, z);   // the opening's place in the plane of the work
    const Vec3 rowBase = base + z * (origin_.x * 0.0);
    const int b1 = loopAt(rowBase + upDir * (0.55 * wp.stitchHeight) + z * layer, z, {opening}, false);
    settle();
    const int top = loopAt(rowBase + upDir * wp.stitchHeight + progress * (0.5 * wp.stitchWidth) - z * (0.5 * layer), z,
                           {b1, hook.front() == b1 ? hook.back() : hook.front()}, true);
    settle();
    return top;
}

void YarnWork::turn() {
    progress = -progress;
    ++row;
}

int YarnWork::nextOpening() const { return cursor_ < (int)target_.size() ? target_[cursor_] : -1; }

int YarnWork::stitchChain() {
    if (hook.empty()) return -1;
    const int id = chain();
    made_.push_back(id);
    if (!log_.empty() && log_.back().first == "ch") ++log_.back().second;
    else log_.push_back({"ch", 1});
    return id;
}

int YarnWork::stitchSc(int opening) {
    if (opening < 0) opening = nextOpening();
    if (opening < 0 || hook.empty()) return -1;
    if (opening == nextOpening()) ++cursor_;
    const int id = sc(opening);
    made_.push_back(id);
    if (!log_.empty() && log_.back().first == "sc") ++log_.back().second;
    else log_.push_back({"sc", 1});
    return id;
}

void YarnWork::stitchTurn(int turningChain) {
    const bool foundation = row == 0;
    for (int i = 0; i < turningChain; ++i) {
        const int id = chain();
        (void)id;
        if (!log_.empty() && log_.back().first == "ch") ++log_.back().second;
        else log_.push_back({"ch", 1});
    }
    // the next row goes back along the stitches just made, the last made first
    target_.assign(made_.rbegin(), made_.rend());
    if (foundation && !target_.empty()) target_.erase(target_.begin());   // sc in the 2nd chain from the hook
    made_.clear();
    cursor_ = 0;
    turn();
    log_.push_back({"turn", 1});
}

std::string YarnWork::patternText() const {
    std::string out, line;
    auto flush = [&]() {
        if (!line.empty()) out += line + "\n";
        line.clear();
    };
    for (const auto& a : log_) {
        if (a.first == "turn") { flush(); continue; }
        if (!line.empty()) line += ", ";
        line += a.first + " " + std::to_string(a.second);
    }
    flush();
    return out;
}

void YarnWork::settle(int steps) {
    const int n = steps > 0 ? steps : wp.settleSteps;
    const int last = rod.particleCount() - 1;
    if (last >= 4) {
        const Vec3 dir = normalized(rod.x[last] - rod.x[last - 4]);
        for (Vec3& f : rod.extForce) f = Vec3{};
        rod.extForce[last] = dir * wp.workTension;
    }
    for (int i = 0; i < n; ++i) rod.step(1.0 / 120.0);
}

void YarnWork::setSlack(Real factor, int steps) {
    const Real ratio = factor / slack_;
    rod.scaleRestLengths(ratio);
    slack_ = factor;
    settle(steps);
}

// ---------------------------------------------------------------------------------------------------------------- piercings

std::vector<std::array<int, 3>> loopPiercings(const YarnWork& w) {
    std::vector<std::array<int, 3>> out;
    const RodSystem& rs = w.rod;
    std::vector<int> owner(rs.particleCount(), -1);
    for (size_t li = 0; li < w.loops.size(); ++li)
        for (int i = w.loops[li].p0; i <= w.loops[li].p1; ++i) owner[i] = (int)li;
    for (size_t li = 0; li < w.loops.size(); ++li) {
        const YarnLoop& L = w.loops[li];
        const int n = L.p1 - L.p0 + 1;
        if (n < 3) continue;
        Vec3 c{};
        for (int i = L.p0; i <= L.p1; ++i) c += rs.x[i];
        c = c / (Real)n;
        std::map<int, int> byOwner;
        for (int s = 0; s < rs.segmentCount(); ++s) {
            const int a = rs.segA[s], b = rs.segB[s];
            if (a >= L.p0 - 1 && b <= L.p1 + 1) continue;  // the ring's own segments and the two that join it
            const Vec3 d = rs.x[b] - rs.x[a];
            for (int i = L.p0; i <= L.p1; ++i) {
                const Vec3 &t0 = rs.x[i], &t1 = rs.x[i == L.p1 ? L.p0 : i + 1];
                // segment a + t d against the triangle (c, t0, t1): Moller-Trumbore
                const Vec3 e1 = t0 - c, e2 = t1 - c, pv = cross(d, e2);
                const Real det = dot(e1, pv);
                if (std::fabs(det) < 1e-30) continue;
                const Vec3 tv = rs.x[a] - c;
                const Real uu = dot(tv, pv) / det;
                if (uu < 0 || uu > 1) continue;
                const Vec3 qv = cross(tv, e1);
                const Real vv = dot(d, qv) / det;
                if (vv < 0 || uu + vv > 1) continue;
                const Real tt = dot(e2, qv) / det;
                if (tt < 0 || tt >= 1) continue;
                const Vec3 nrm = cross(e1, e2);
                byOwner[owner[a]] += dot(d, nrm) > 0 ? 1 : -1;
            }
        }
        for (const auto& kv : byOwner)
            if (kv.second != 0) out.push_back({(int)li, kv.first, kv.second});
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::array<int, 4>> threadingReport(const YarnWork& w) {
    std::vector<std::array<int, 4>> out;
    const RodSystem& rs = w.rod;
    for (size_t li = 0; li < w.loops.size(); ++li) {
        const YarnLoop& M = w.loops[li];
        for (int eyeLoop : M.through) {
            const YarnLoop& L = w.loops[eyeLoop];
            const int n = L.r1 - L.r0 + 1;
            if (n < 3) continue;
            Vec3 c{};
            for (int i = L.r0; i <= L.r1; ++i) c += rs.x[i];
            c = c / (Real)n;
            int plus = 0, minus = 0;
            for (int s = std::max(0, M.p0 - 1); s < M.p1; ++s) {   // from the segment that brings the yarn to the first leg
                if (s >= M.r0 - 1 && s <= M.r1) continue;   // the ring itself is not a leg
                const Vec3 a = rs.x[s], d = rs.x[s + 1] - rs.x[s];
                for (int i = L.r0; i <= L.r1; ++i) {
                    const Vec3 &t0 = rs.x[i], &t1 = rs.x[i == L.r1 ? L.r0 : i + 1];
                    const Vec3 e1 = t0 - c, e2 = t1 - c, pv = cross(d, e2);
                    const Real det = dot(e1, pv);
                    if (std::fabs(det) < 1e-30) continue;
                    const Vec3 tv = a - c;
                    const Real uu = dot(tv, pv) / det;
                    if (uu < 0 || uu > 1) continue;
                    const Vec3 qv = cross(tv, e1);
                    const Real vv = dot(d, qv) / det;
                    if (vv < 0 || uu + vv > 1) continue;
                    const Real tt = dot(e2, qv) / det;
                    if (tt < 0 || tt >= 1) continue;
                    if (dot(d, cross(e1, e2)) > 0) ++plus; else ++minus;
                }
            }
            out.push_back({(int)li, eyeLoop, plus, minus});
        }
    }
    return out;
}

}  // namespace fl
