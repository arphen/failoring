// Yarn mode: the work made one hook action at a time, from one or more yarns. See include/failoring/crochet/yarn_work.hpp.
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

constexpr Real kMassScale = 1.0e4;   // see RodSystem::setMassScale: the springs of a piece worked by hand are soft

}  // namespace

YarnWork::YarnWork() {
    rodParams.gravity = Vec3{};   // the work floats: the shape of the loops is what is being looked at
    rodParams.EA = 30.0;
    rodParams.EI = 4.0e-6;
    rodParams.GJ = 4.0e-6;
    rodParams.damping = 60.0;
    rodParams.substeps = 2;       // the soft settling is for feel: cheap
    rodParams.iterations = 1;
    rodParams.radiusScale = 0.55; // worsted is soft: strands may be squeezed
    rodParams.shapeStiffness = 40.0;
    rod.params = rodParams;
}

// ---------------------------------------------------------------------------------------------------------------- the rod from the design

void YarnWork::materialize() {
    RodSystem fresh;
    fresh.params = rodParams;
    fresh.setMassScale(kMassScale);
    fresh.setRotationalInertiaScale(kMassScale);
    for (Strand& s : strands) {
        if (s.pts.size() < 2) continue;
        const int ci = fresh.addChain(s.pts, false, true);   // the yarn keeps the bend it was made with
        const RodChain& ch = fresh.chains[ci];
        s.first = ch.firstParticle;
        for (int i = 0; i < ch.segments && i < (int)s.rest.size(); ++i) fresh.restLen[ch.firstSegment + i] = s.rest[i];
        fresh.pin(ch.firstParticle);   // the tail end is held
    }
    rod = std::move(fresh);
}

void YarnWork::settle(int steps) {
    const int n = steps > 0 ? steps : wp.settleSteps;
    for (int i = 0; i < n; ++i) rod.step(1.0 / 120.0);
}

std::vector<Vec3> YarnWork::segmentColours() const {
    std::vector<Vec3> out(rod.segmentCount(), Vec3{0.8, 0.3, 0.22});
    for (int s = 0; s < rod.segmentCount(); ++s) {
        const int c = rod.segChain[s];
        if (c >= 0 && c < (int)strands.size()) out[s] = strands[c].colour;
    }
    return out;
}

// ---------------------------------------------------------------------------------------------------------------- loops

int YarnWork::addLoop(int strand, int p0, int p1, bool live) {
    YarnLoop l;
    l.strand = strand;
    l.p0 = l.r0 = p0;
    l.p1 = l.r1 = p1;
    l.live = live;
    l.row = row;
    loops.push_back(l);
    const int id = (int)loops.size() - 1;
    loops[id].top = id;
    if (live) hook.push_back(id);
    return id;
}

EyeFrame YarnWork::eye(int loop) const {
    const YarnLoop& l = loops[loop];
    const std::vector<Vec3>& P = strands[l.strand].pts;
    EyeFrame e;
    const int n = l.r1 - l.r0 + 1;
    Vec3 c{};
    for (int i = l.r0; i <= l.r1; ++i) c += P[i];
    c = c / (Real)n;
    Vec3 area{};
    for (int i = l.r0; i <= l.r1; ++i) {
        const Vec3& a = P[i];
        const Vec3& b = P[i == l.r1 ? l.r0 : i + 1];
        area += cross(a - c, b - c);
    }
    e.centre = c + l.eyeShift;
    e.normal = length2(area) > 1e-30 ? normalized(area) : Vec3{0, 0, 1};
    e.radius = std::sqrt(0.5 * length(area) / kPi);
    return e;
}

YarnWork::PullFrame YarnWork::pullFrame(int loopId, Real s, Real wrap) const {
    PullFrame f = pullFrameBody(loopId, s, wrap);
    f.pathLength = 0.0;
    for (size_t i = 1; i < f.pts.size(); ++i) f.pathLength += length(f.pts[i] - f.pts[i - 1]);
    if (f.pts.size() >= 2) f.runDir = normalized(f.pts.back() - f.pts[f.pts.size() - 2]);
    {
        Vec3 prev = f.base;
        for (const Vec3& q : f.natural) {
            f.natLength += length(q - prev);
            prev = q;
        }
    }
    return f;
}

void YarnWork::PullFrame::applyHang(const std::vector<Vec3>& curve, Real phi) {
    if (curve.size() < 2 || phi >= 1.0) return;
    phi = std::max(0.0, phi);
    std::vector<Real> arc(curve.size(), 0.0);
    for (size_t i = 1; i < curve.size(); ++i) arc[i] = arc[i - 1] + length(curve[i] - curve[i - 1]);
    Real a = 0.0;
    Vec3 prev = base;
    size_t k = 0;
    for (size_t i = 0; i < pts.size(); ++i) {
        a += length(natural[i] - prev);
        prev = natural[i];
        while (k + 2 < curve.size() && arc[k + 1] < a) ++k;
        const Real seg = std::max(arc[k + 1] - arc[k], 1e-12);
        const Real u = std::min(1.0, std::max(0.0, (a - arc[k]) / seg));
        const Vec3 h = curve[k] + (curve[k + 1] - curve[k]) * u;
        pts[i] = h + (pts[i] - h) * phi;
    }
}

YarnWork::PullFrame YarnWork::pullFrameBody(int loopId, Real s, Real wrap) const {
    PullFrame f;
    const YarnLoop& L = loops[loopId];
    const Strand& S = strands[L.strand];
    s = std::min(1.0, std::max(0.0, s));
    const Real e = s * s * (3.0 - 2.0 * s);
    auto cur = [&](int i) { const int g = S.first + i; return g < (int)rod.x.size() ? rod.x[g] : S.pts[i]; };
    const int start = (L.t0 >= 0 && L.t0 + 1 < L.p0) ? L.t0 + 1 : L.p0;   // the working yarn to the first eye comes with it
    f.first = S.first + start;
    const int last = std::min(L.p1 + 1, (int)S.pts.size() - 1);   // with the node after it: where the run to the ball starts
    for (int i = start; i <= last; ++i) f.pts.push_back(cur(i));
    f.natural = f.pts;
    f.base = cur(std::max(0, start - 1));
    auto at = [&](int i) -> Vec3& { return f.pts[i - start]; };
    Vec3 rc{};
    for (int i = L.r0; i <= L.r1; ++i) rc += cur(i);
    rc = rc / (Real)(L.r1 - L.r0 + 1);
    f.tip = rc;
    f.eye = rc;
    const EyeFrame own = eye(loopId);
    f.axis = own.normal;
    if (L.through.empty()) {   // a yarn over: the yarn closes round the hook into its ring
        const Real k = 0.25 + 0.75 * e;
        for (int i = L.p0; i <= L.p1; ++i) at(i) = rc + (at(i) - rc) * k;
        return f;
    }
    f.through = true;
    // the direction the loop goes through the eye, as the loop was laid
    const EyeFrame ef = eye(L.through[0]);
    const Vec3 target = L.through.size() > 1 ? eye(L.through[1]).centre : rc;
    const Vec3 toward = normalized(target - ef.centre);
    Vec3 n = ef.normal;
    if (dot(toward, n) < 0) n = -n;
    Vec3 a = normalized(toward * 0.5 + n * 0.5);
    if (dot(toward, n) > 0.75) a = toward;
    const Vec3 E = ef.centre;
    auto xi = [&](const Vec3& q) { return dot(q - E, a); };
    const Real behind = 0.0075;                       // how far behind the eye the hook's throat starts
    const Real xiR = xi(rc);
    const Real xiH = -behind + (xiR + behind) * e;   // the throat's position along the way through
    const Vec3 latRC = (rc - E) - a * xiR;
    const Real k = 0.10 + 0.90 * e * e;               // how much of the loop beyond the throat is still folded onto the hook
    const Real w = 0.30 + 0.70 * e;                   // and how closely its two sides lie together
    for (int i = L.p0; i <= L.p1; ++i) {
        Vec3& q = at(i);
        const Real x = xi(q);
        if (x <= xiH) continue;
        const Vec3 lat = (q - E) - a * x;
        const bool ring = i >= L.r0 && i <= L.r1;
        const Vec3 lat2 = ring ? latRC * e + (lat - latRC) * w : lat;
        q = E + a * (xiH + (x - xiH) * k) + lat2;
    }
    f.tip = E + a * xiH + latRC * e;
    f.axis = a;
    f.eye = E;
    if (wrap > 0.0 && start < L.p0) {
        // the yarn over: the working yarn from where it was left comes from behind the work, over the shaft a little behind the throat, to its front
        const Vec3 back = -zAxis();
        Vec3 bperp = back - a * dot(back, a);
        if (length2(bperp) < 1e-8) bperp = cross(a, upDir);
        bperp = normalized(bperp);
        Vec3 top = upDir - a * dot(upDir, a) - bperp * dot(upDir, bperp);
        if (length2(top) < 1e-8) top = cross(a, bperp);
        top = normalized(top);
        const Real Rw = 0.0033, lam = 0.003;
        const Vec3 C = f.tip + a * lam;
        const int m = L.p0 - start;
        for (int j = 0; j < m; ++j) {
            const Real ph = kPi * (m > 1 ? j / (m - 1.0) : 0.5);
            const Vec3 on = C + (bperp * std::cos(ph) + top * std::sin(ph)) * Rw;
            Vec3& q = at(start + j);
            q = q + (on - q) * std::min(1.0, wrap);
        }
    }
    return f;
}

namespace {
Vec3 rotateAbout(const Vec3& p, const Vec3& origin, const Vec3& axis, Real ang) {   // Rodrigues
    const Vec3 v = p - origin;
    const Real c = std::cos(ang), s = std::sin(ang);
    return origin + v * c + cross(axis, v) * s + axis * (dot(axis, v) * (1.0 - c));
}
}  // namespace

namespace {
// The ring `ring` hinged about the chord between its ends until its normal lies along `tipDir`, the way that lifts its middle up the rows; returns the ring
// hinged by `k` of the way and its middle.
std::vector<Vec3> hingeRing(std::vector<Vec3> ring, const Vec3& tipDir, const Vec3& upDir, Real k, Vec3* middle) {
    Vec3 c{};
    for (const Vec3& q : ring) c += q;
    c = c / (Real)std::max<size_t>(1, ring.size());
    if (middle) *middle = c;
    if (ring.size() < 3 || k <= 0.0) return ring;
    const Vec3 A = ring.front(), B = ring.back();
    Vec3 area{};
    for (size_t i = 0; i < ring.size(); ++i) area += cross(ring[i] - c, ring[(i + 1) % ring.size()] - c);
    if (length2(area) < 1e-30) return ring;
    const Vec3 n = normalized(area);
    Vec3 h = B - A;
    if (length2(h) < 1e-10) h = cross(n, tipDir);
    if (length2(h) < 1e-10) return ring;
    h = normalized(h);
    Vec3 np = n - h * dot(h, n), dp = tipDir - h * dot(h, tipDir);
    if (length2(np) < 1e-12 || length2(dp) < 1e-12) return ring;
    np = normalized(np);
    dp = normalized(dp);
    auto angleTo = [&](const Vec3& d) { return std::atan2(dot(h, cross(np, d)), dot(np, d)); };
    const Real th1 = angleTo(dp), th2 = angleTo(-dp);
    const Vec3 mid = (A + B) * 0.5;
    const Real up1 = dot(rotateAbout(c, mid, h, th1) - mid, upDir), up2 = dot(rotateAbout(c, mid, h, th2) - mid, upDir);
    const Real th = (up1 >= up2 ? th1 : th2) * k;
    for (Vec3& q : ring) q = rotateAbout(q, mid, h, th);
    if (middle) *middle = rotateAbout(c, mid, h, th);
    return ring;
}
}  // namespace

std::vector<Vec3> YarnWork::standRing(int loopId, const Vec3& tipDir, const Vec3& tipPos, Real k) const {
    const YarnLoop& L = loops[loopId];
    const Strand& S = strands[L.strand];
    std::vector<Vec3> ring;
    for (int i = L.r0; i <= L.r1; ++i) {
        const int g = S.first + i;
        ring.push_back(g < (int)rod.x.size() ? rod.x[g] : S.pts[i]);
    }
    Vec3 c2{};
    ring = hingeRing(ring, tipDir, upDir, k, &c2);
    if (k <= 0.0) return ring;
    const Vec3 move = (tipPos - tipDir * kStandBack - c2) * k;   // the middle of the ring is carried onto the hook, a throat's length behind its tip
    for (Vec3& q : ring) q += move;
    return ring;
}

Vec3 YarnWork::standTip(int loopId, const Vec3& tipDir) const {
    const YarnLoop& L = loops[loopId];
    const Strand& S = strands[L.strand];
    std::vector<Vec3> ring;
    for (int i = L.r0; i <= L.r1; ++i) {
        const int g = S.first + i;
        ring.push_back(g < (int)rod.x.size() ? rod.x[g] : S.pts[i]);
    }
    Vec3 c2{};
    hingeRing(ring, tipDir, upDir, 1.0, &c2);
    return c2 + tipDir * kStandBack;
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
    Strand& s = strands[active];
    const std::vector<Vec3> pts = resample(s.pts.back(), way, wp.segmentLength);
    for (const Vec3& p : pts) {
        const Real d = length(p - s.pts.back());
        if (d < 1e-9) continue;   // a node on top of the last one would be a segment of no length
        s.rest.push_back(d * wp.tighten);
        s.pts.push_back(p);
    }
}

// A loop with its ring put at `ringC` (plane square to `ringN`) and its legs through the eyes of `eyesLoops`, in order.
int YarnWork::loopAt(const Vec3& ringC, const Vec3& ringN0, const std::vector<int>& eyesLoops, const Vec3* major, Real semiA, Real semiB, const std::vector<int>* modes,
                     const Vec3* front) {
    const Real r = rodParams.radius;
    const Vec3 ringN = normalized(ringN0);
    const std::vector<int>& eyesV = eyesLoops;   // the eyes are visited in the order given: the tip's loop first
    struct Pass { Vec3 c, a, u; };
    std::vector<Pass> passes;
    Vec3 cursor = endPosition();
    for (size_t k = 0; k < eyesV.size(); ++k) {
        const EyeFrame e = eye(eyesV[k]);
        const Vec3 target = (k + 1 < eyesV.size()) ? eye(eyesV[k + 1]).centre : ringC;
        Vec3 toward = normalized(target - e.centre);
        Vec3 n = e.normal;
        if (dot(toward, n) < 0) n = -n;
        // legs pass towards the target; a very slanting line is bent towards the eye's normal so that it really goes through
        Vec3 a = normalized(toward * 0.5 + n * 0.5);
        if (dot(toward, n) > 0.75) a = toward;
        Vec3 u = cross(a, ringN);
        if (length2(u) < 1e-8) u = cross(a, upDir);
        u = normalized(u);
        Vec3 centre = e.centre;
        Real gap = wp.legGap;
        const int mode = (modes != nullptr && k < modes->size()) ? (*modes)[k] : 0;
        if (mode != 0 && front != nullptr) {
            // round one strand of the ring only: the ring's node farthest toward (mode +1) or away from (mode -1) the worker is on that strand; the
            // legs pass either side of it, across it (one between the two strands, one outside)
            const YarnLoop& Lp = loops[eyesV[k]];
            const std::vector<Vec3>& P = strands[Lp.strand].pts;
            const Vec3 F = normalized(*front - e.normal * dot(*front, e.normal));
            int best = Lp.r0;
            Real bestV = -1e300;
            for (int pi = Lp.r0; pi <= Lp.r1; ++pi) {
                const Real v = mode * dot(F, P[pi] - e.centre);
                if (v > bestV) { bestV = v; best = pi; }
            }
            centre = P[best];
            u = F;
            gap = std::max(wp.legGap, 1.6 * r);
        }
        const Real sg = dot(cursor - centre, u) >= 0 ? 1.0 : -1.0;
        passes.push_back({centre, a, u * sg * (gap / std::max(wp.legGap, 1e-9))});
        cursor = centre + a * wp.approach;
    }
    const Real ra = semiA > 0 ? semiA : wp.ringRadius, rb = semiB > 0 ? semiB : wp.ringRadius;
    const Real pitch = 2.4 * r;
    std::vector<Vec3> way, travel;
    Real travelStraight = 0;
    int ringFirstWay = 0, ringLastWay = 0;
    if (passes.empty()) {
        // a yarn over: a ring round the hook, on the side the yarn is on; the yarn goes round once and on
        Vec3 U = major != nullptr ? (*major) - ringN * dot(*major, ringN) : Vec3{1, 0, 0} - ringN * dot(Vec3{1, 0, 0}, ringN);
        U = normalized(U);
        const Vec3 V = cross(ringN, U);
        const Vec3 toEnd = (endPosition() - ringC) - ringN * dot(endPosition() - ringC, ringN);
        const Real th0 = length2(toEnd) > 1e-12 ? std::atan2(dot(toEnd, V) / rb, dot(toEnd, U) / ra) : 0.0;
        auto onRing = [&](Real phi) { const Real t = th0 + phi; return ringC + U * (ra * std::cos(t)) + V * (rb * std::sin(t) * (1.0 - taper_ * std::cos(t))); };
        travel.push_back(onRing(0.0));
        travelStraight = length(onRing(0.0) - endPosition());
        ringFirstWay = 0;
        const int steps = 22;
        for (int k = 1; k <= steps; ++k) way.push_back(onRing(2.0 * kPi * k / steps) + ringN * (pitch * k / steps));
        ringLastWay = (int)way.size() - 1;
    } else {
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
        Vec3 U = major != nullptr ? (*major) - ringN * dot(*major, ringN) : Vec3{};
        const Vec3 toEye = (L.c - ringC) - ringN * dot(L.c - ringC, ringN);
        if (length2(U) < 1e-10) {
            U = toEye;
            if (length2(U) < 1e-10) U = L.u;
        }
        U = normalized(U);
        const Vec3 V = cross(ringN, U);   // (U, V, ringN) right handed
        const Real th0 = std::atan2(dot(toEye, V) / rb, dot(toEye, U) / ra);   // the ring starts on the side that faces the eye
        const Real eps = std::asin(std::min(0.9, wp.legGap / std::max(std::min(ra, rb), 1e-6)));
        auto onRing = [&](Real phi) { const Real t = th0 + phi; return ringC + U * (ra * std::cos(t)) + V * (rb * std::sin(t) * (1.0 - taper_ * std::cos(t))); };
        way.push_back(L.c + L.a * wp.approach + L.u * wp.legGap);
        const int ringSteps = 22;
        const Real sweep = 2.0 * kPi - 2.0 * eps;
        for (int k = 1; k <= 2; ++k) {
            const Real f = k / 3.0;
            const Vec3 from = L.c + L.a * wp.approach + L.u * wp.legGap;
            way.push_back(from + (onRing(eps) - from) * f);
        }
        ringFirstWay = (int)way.size();
        for (int k = 0; k <= ringSteps; ++k) way.push_back(onRing(eps + sweep * k / ringSteps) + ringN * (pitch * k / ringSteps));
        ringLastWay = (int)way.size() - 1;
        for (int k = 2; k >= 1; --k) {
            const Real f = k / 3.0;
            const Vec3 from = L.c + L.a * wp.approach - L.u * wp.legGap;
            way.push_back(from + (onRing(-eps) + ringN * pitch - from) * f);
        }
        for (int k = (int)passes.size() - 1; k >= 0; --k) {
            const Pass& p = passes[k];
            const Vec3 leg = p.c - p.u * wp.legGap;
            way.push_back(leg + p.a * wp.approach);
            way.push_back(leg);
            way.push_back(leg - p.a * wp.approach);
        }
    }
    Strand& S = strands[active];
    const int oldEnd = (int)S.pts.size() - 1;
    {   // the working yarn to the first eye: pulled taut
        const int t0 = (int)S.rest.size();
        appendPath(travel);
        Real made = 0;
        for (int i = t0; i < (int)S.rest.size(); ++i) made += S.rest[i];
        const Real want = std::max(travelStraight * wp.travelSlack, 1e-4);
        if (made > want)
            for (int i = t0; i < (int)S.rest.size(); ++i) S.rest[i] *= want / made;
    }
    const int e0 = (int)S.pts.size() - 1;
    std::vector<Vec3> head(way.begin(), way.begin() + ringFirstWay);
    std::vector<Vec3> ring(way.begin() + ringFirstWay, way.begin() + ringLastWay + 1);
    std::vector<Vec3> tail(way.begin() + ringLastWay + 1, way.end());
    if (!head.empty()) appendPath(head);
    const int r0 = (int)S.pts.size();
    appendPath(ring);
    const int r1 = (int)S.pts.size() - 1;
    if (!tail.empty()) appendPath(tail);
    const int p1 = (int)S.pts.size() - 1;
    if (!passes.empty()) {
        const Pass& first = passes.front();
        appendPath({first.c - first.u * wp.legGap - first.a * (wp.approach + wp.workingRun)});
    }
    for (int li : eyesLoops) {   // the loops the finger went through are pulled in onto it
        YarnLoop& Lp = loops[li];
        const Real target = std::max(wp.closeFloor, Lp.ringScale * wp.closeFactor);
        const Real f = target / Lp.ringScale;
        std::vector<Real>& rest = strands[Lp.strand].rest;
        for (int i = Lp.r0; i < Lp.r1 && i < (int)rest.size(); ++i) rest[i] *= f;
        Lp.ringScale = target;
    }
    for (int li : eyesLoops)   // the live loops among the eyes leave the hook
        if (loops[li].live) {
            loops[li].live = false;
            hook.erase(std::remove(hook.begin(), hook.end(), li), hook.end());
            loops[li].leftHookBy = -2;   // set to the new loop below
        }
    const int id = addLoop(active, e0 + 1, p1, true);
    for (YarnLoop& Lp : loops)
        if (Lp.leftHookBy == -2) Lp.leftHookBy = id;
    loops[id].r0 = r0;
    loops[id].r1 = r1;
    loops[id].t0 = oldEnd;
    loops[id].through = eyesLoops;
    return id;
}

// ---------------------------------------------------------------------------------------------------------------- yarns and colours

int YarnWork::startYarn(const Vec3& colour) {
    // a tail behind the work, by the hook: it is held there while the yarn is used
    const Vec3 z = zAxis();
    const HookPose hp = hookPose();
    const Vec3 at = hp.valid ? hp.tip : endPosition();
    Strand s;
    s.colour = colour;
    s.pts.push_back(at - progress * wp.tailLength - z * 0.0045 + upDir * 0.001);
    strands.push_back(s);
    active = (int)strands.size() - 1;
    appendPath({at - z * 0.0045 + upDir * 0.001});
    return active;
}

void YarnWork::setNextColour(const Vec3& colour, ColourAt when) {
    if (length(colour - strands[active].colour) < 1e-6) {   // already this colour
        pending_ = false;
        return;
    }
    pending_ = true;
    pendingColour_ = colour;
    pendingAt_ = when;
}

// Switches to the pending colour if it is due now (`now` is the moment this is called from): picks up a yarn of that colour that was dropped, or
// starts a new one.
void YarnWork::applyPendingColour(ColourAt now) {
    if (!pending_ || pendingAt_ != now) return;
    pending_ = false;
    for (int i = (int)strands.size() - 1; i >= 0; --i)
        if (length(strands[i].colour - pendingColour_) < 1e-6) {
            active = i;
            return;
        }
    startYarn(pendingColour_);
}

// ---------------------------------------------------------------------------------------------------------------- the first loop and the stitches

int YarnWork::slipKnot(const Vec3& at, const Vec3& colour) {
    // the tail runs in along the work direction, makes one turn round that axis (a helix a strand and a bit long, so it does not cut itself: the
    // ring of the slip knot), then the yarn leaves round the outside and a finger of it goes through the ring
    strands.clear();
    loops.clear();
    hook.clear();
    active = 0;
    pending_ = false;
    const Real R = 0.0055, r = rodParams.radius;
    const Vec3 ex = progress, ey = upDir, ez = cross(progress, upDir);
    Strand s;
    s.colour = colour;
    std::vector<Vec3> way;
    way.push_back(at + ey * R);
    const int n = 16;
    for (int i = 1; i <= n; ++i) {
        const Real a = 2.0 * kPi * i / n;
        way.push_back(at + ex * (2.4 * r * i / n) + ey * (R * std::cos(a)) + ez * (R * std::sin(a)));
    }
    s.pts.push_back(at - ex * wp.tailLength + ey * R);
    strands.push_back(s);
    appendPath(way);
    const int ring = addLoop(0, 0, (int)strands[0].pts.size() - 1, false);
    loops[ring].r0 = std::min((int)strands[0].pts.size() - 2, (int)std::ceil(wp.tailLength / wp.segmentLength));
    // out round the ring and back to the side the finger will come from
    appendPath({at + ex * (2.4 * r) + ey * (R * 1.7), at - ex * (4.0 * r) + ey * (R * 1.7), at - ex * (4.0 * r) + ey * (R * 0.3)});
    origin_ = at;
    chainCount_ = 0;
    turnChains_ = 0;
    target_.clear();
    made_.clear();
    cursor_ = 0;
    log_.clear();
    row = 0;
    const int id = loopAt(at + progress * wp.stitchWidth + zAxis() * wp.layerGap, zAxis(), {ring});
    materialize();
    return id;
}

int YarnWork::chain() {
    applyPendingColour(ColourAt::Stitch);
    applyPendingColour(ColourAt::PullThrough);   // a chain is a single pull-through
    const Vec3 z = zAxis();
    Vec3 c;
    int id;
    if (row == 0) {
        // the foundation chain: the next ring one stitch on along the row, alternating between the two layers
        ++chainCount_;
        c = origin_ + progress * (wp.chainPitch * (chainCount_ + 1)) + z * ((chainCount_ % 2 == 0 ? 1.0 : -1.0) * wp.chainLayer);
        const Vec3 along = progress;
        taper_ = wp.chainTaper;
        id = loopAt(c, z, {hook.back()}, &along, wp.chainA, wp.chainB);
        taper_ = 0.0;
        loops[id].eyeShift = along * (wp.chainEye * wp.chainA);
    } else {
        // a turning chain: rings standing above the stitch on the hook, at the end of the row
        const EyeFrame e = eye(hook.back());
        const Vec3 base = e.centre - z * dot(e.centre, z);
        const Vec3 lean = normalized(upDir * std::cos(wp.barTilt) + z * std::sin(wp.barTilt));
        const Vec3 along = progress;
        const Real rise = turnChains_ == 0 ? 1.0 : 0.8;
        c = base + upDir * (wp.stitchHeight * rise) - along * (0.5 * wp.stitchWidth) + z * (0.3 * wp.layerGap);
        id = loopAt(c, lean, {hook.back()}, &along, wp.barA, wp.barB);
        ++turnChains_;
    }
    materialize();
    return id;
}

Real YarnWork::heightOf(const std::string& kind) const {
    if (kind == "hdc") return wp.hdcScale;
    if (kind == "dc") return wp.dcScale;
    if (kind == "slst") return wp.slstScale;
    return 1.0;
}

// The stitches. The opening is the eye of a loop in the row below. The loop pulled up through it is a post: it stands up to the height of the stitch
// and is a bar (a flat elongated ring leaning back toward the worker, which from the front is a pair of strands side by side along the row). The loop
// pulled through that and the loops on the hook is the stitch's top: another bar, half a stitch along. hdc and dc have yarn overs (rings on the hook
// that lie behind the post) and dc pulls through twice. Position comes from the opening's place along the row; the lean from the worker's side.
int YarnWork::tallStitchBody(const std::string& kind, int opening, Loops which) {
    const EyeFrame b = eye(opening);
    const Vec3 z = zAxis();
    const Vec3 along = progress;
    const Vec3 lean = normalized(upDir * std::cos(wp.barTilt) + z * std::sin(wp.barTilt));
    const Vec3 base = b.centre - z * dot(b.centre, z);
    const Real h = heightOf(kind) * wp.stitchHeight;
    const std::vector<int> modes{which == Loops::Both ? 0 : (which == Loops::Front ? 1 : -1)};
    applyPendingColour(ColourAt::Stitch);
    int y1 = -1;
    if (kind == "hdc" || kind == "dc") {   // yarn over before the hook goes into the stitch
        const Real yh = kind == "dc" ? wp.yoDc : wp.yoHdc;
        y1 = loopAt(base + upDir * (yh * h) - z * (wp.yoLayer * wp.layerGap), lean, {}, &along, wp.barA, wp.barB);
    }
    const Real rise = kind == "dc" ? wp.riseDc : wp.postRise;
    const int b1 = loopAt(base + upDir * (rise * h) + z * (0.5 * wp.layerGap), lean, {opening}, &along, wp.barA, wp.barB, &modes, &z);
    if (kind == "dc") {
        // pull through two: the loop just pulled up and the yarn over; then through that and the loop that was on the hook
        const int m = loopAt(base + upDir * (wp.midDc * h) + z * (0.2 * wp.layerGap), lean, {b1, y1}, &along, wp.barA, wp.barB);
        applyPendingColour(ColourAt::PullThrough);
        std::vector<int> eyes{m, hook[hook.size() - 2]};
        eyes.insert(eyes.end(), extra_.begin(), extra_.end());
        return loopAt(base + upDir * h + along * (0.5 * wp.stitchWidth) - z * (0.3 * wp.layerGap), lean, eyes, &along, wp.barA, wp.barB);
    }
    if (kind == "hdc") {
        applyPendingColour(ColourAt::PullThrough);
        // the hook has [.., L, yarn over, pulled-up loop]
        std::vector<int> eyes{b1, y1, hook[hook.size() - 3]};
        eyes.insert(eyes.end(), extra_.begin(), extra_.end());
        return loopAt(base + upDir * h + along * (0.5 * wp.stitchWidth) - z * (0.3 * wp.layerGap), lean, eyes, &along, wp.barA, wp.barB);
    }
    applyPendingColour(ColourAt::PullThrough);
    std::vector<int> eyes{b1, hook[hook.size() - 2]};
    eyes.insert(eyes.end(), extra_.begin(), extra_.end());
    return loopAt(base + upDir * h + along * (0.5 * wp.stitchWidth) - z * (0.3 * wp.layerGap), lean, eyes, &along, wp.barA, wp.barB);
}

int YarnWork::tallStitch(const std::string& kind, int opening, Loops which) {
    const size_t firstNew = loops.size();
    const int top = tallStitchBody(kind, opening, which);
    for (size_t i = firstNew; i < loops.size(); ++i) loops[i].top = top;   // its post, yarn overs and middle loop belong to the stitch's top
    return top;
}

int YarnWork::sc(int opening, Loops which) {
    const int id = tallStitch("sc", opening, which);
    materialize();
    return id;
}

int YarnWork::hdc(int opening, Loops which) {
    const int id = tallStitch("hdc", opening, which);
    materialize();
    return id;
}

int YarnWork::dc(int opening, Loops which) {
    const int id = tallStitch("dc", opening, which);
    materialize();
    return id;
}

int YarnWork::slst(int opening, Loops which) {
    const EyeFrame b = eye(opening);
    const Vec3 z = zAxis();
    const Vec3 along = progress;
    const Vec3 lean = normalized(upDir * std::cos(wp.barTilt) + z * std::sin(wp.barTilt));
    const Vec3 base = b.centre - z * dot(b.centre, z);
    const std::vector<int> modes{which == Loops::Both ? 0 : (which == Loops::Front ? 1 : -1), 0};
    applyPendingColour(ColourAt::Stitch);
    applyPendingColour(ColourAt::PullThrough);
    const Real h = heightOf("slst") * wp.stitchHeight;
    // through the opening and then the loop on the hook, in one go: a flat loop half a stitch along
    const int id = loopAt(base + upDir * h + along * (0.5 * wp.stitchWidth) - z * (0.3 * wp.layerGap), lean, {opening, hook.back()}, &along, wp.barA, wp.barB, &modes, &z);
    materialize();
    return id;
}

void YarnWork::turn() {
    progress = -progress;
    ++row;
}

int YarnWork::nextOpening() const { return cursor_ < (int)target_.size() ? target_[cursor_] : -1; }

int YarnWork::finishOff() {
    if (hook.empty() || finished_) return -1;
    const int live = hook.back();
    const EyeFrame e = eye(live);
    Vec3 n = e.normal;
    if (dot(n, zAxis()) < 0) n = -n;   // toward the front of the work: the cut end is drawn through the loop from behind
    Strand& S = strands[active];
    const int oldEnd = (int)S.pts.size() - 1;
    Vec3 side = cross(n, upDir);
    side = length2(side) > 1e-8 ? normalized(side) : Vec3{1, 0, 0};
    Vec3 down = normalized(-upDir + n * dot(upDir, n));   // hangs along the loop's plane, not back through it
    const Vec3 c = e.centre + side * 0.0008;               // through the eye, a little off its middle
    appendPath({c - n * 0.006});
    const int e0 = (int)S.pts.size() - 1;
    appendPath({c - n * 0.002, c});
    appendPath({c + n * 0.003, c + n * 0.006 + down * 0.003, c + n * 0.008 + down * 0.012, c + n * 0.008 + down * 0.022});
    const int p1 = (int)S.pts.size() - 1;
    int r0 = p1;   // what lies beyond the eye is the "ring" of the tail (its far end is where the hook draws it to)
    for (int i = e0 + 1; i <= p1; ++i)
        if (length(S.pts[i] - c) > 0.006) { r0 = i; break; }
    {   // the loop is pulled tight on the yarn through it
        YarnLoop& Lp = loops[live];
        const Real target = std::max(wp.closeFloor, Lp.ringScale * wp.closeFactor);
        const Real f = target / Lp.ringScale;
        std::vector<Real>& rest = strands[Lp.strand].rest;
        for (int i = Lp.r0; i < Lp.r1 && i < (int)rest.size(); ++i) rest[i] *= f;
        Lp.ringScale = target;
    }
    const int id = addLoop(active, e0 + 1, p1, false);
    loops[id].r0 = r0;
    loops[id].r1 = p1;
    loops[id].t0 = oldEnd;
    loops[id].through = {live};
    S.cut = true;
    finished_ = true;
    pushLog("fo", active);
    materialize();
    return id;
}

int YarnWork::stitchChain() {
    if (hook.empty() || finished_) return -1;
    const int id = chain();
    made_.push_back(id);
    pushLog("ch", loops[id].strand);
    return id;
}

int YarnWork::stitch(const std::string& kind, int opening, Loops which) {
    if (finished_) return -1;
    if (opening < 0) opening = nextOpening();
    if (opening < 0 || hook.empty()) return -1;
    if (opening == nextOpening()) ++cursor_;
    int id = -1;
    if (kind == "sc") id = sc(opening, which);
    else if (kind == "hdc") id = hdc(opening, which);
    else if (kind == "dc") id = dc(opening, which);
    else if (kind == "slst") id = slst(opening, which);
    else return -1;
    made_.push_back(id);
    const std::string name = kind == "slst" ? "sl st" : kind;
    const std::string logged = which == Loops::Both ? name : name + (which == Loops::Front ? " flo" : " blo");
    pushLog(logged, loops[id].strand);
    extra_.clear();
    return id;
}

bool YarnWork::stitchSkip() {
    if (finished_ || nextOpening() < 0) return false;
    ++cursor_;
    pushLog("sk", -1);
    return true;
}

void YarnWork::stitchTurn(int turningChain) {
    if (finished_) return;
    const bool foundation = row == 0;
    // the next row goes back along the stitches just made, the last made first; its turning chain is the first thing in it
    target_.assign(made_.rbegin(), made_.rend());
    if (foundation && !target_.empty()) target_.erase(target_.begin());   // sc in the 2nd chain from the hook
    made_.clear();
    cursor_ = 0;
    log_.push_back({"turn", 1, -1});
    turn();   // the work is turned over before its turning chain is made: the chain goes the new way
    turnChains_ = 0;
    for (int i = 0; i < turningChain; ++i) {
        const int id = chain();
        pushLog("ch", loops[id].strand);
    }
}

void YarnWork::pushLog(const std::string& kind, int strand) {
    if (!log_.empty() && log_.back().kind == kind && log_.back().strand == strand) ++log_.back().count;
    else log_.push_back({kind, 1, strand});
}

std::string YarnWork::patternText() const {
    std::string out, line;
    auto flush = [&]() {
        if (!line.empty()) out += line + "\n";
        line.clear();
    };
    // runs of the same stitch in different yarns are one run in the text (the colours are in patternRows)
    std::string lastKind;
    int run = 0;
    auto emit = [&]() {
        if (run == 0) return;
        if (!line.empty()) line += ", ";
        line += lastKind == "fo" ? std::string("fasten off") : lastKind + " " + std::to_string(run);
        run = 0;
    };
    for (const LogEntry& a : log_) {
        if (a.kind == "turn") { emit(); flush(); continue; }
        if (a.kind != lastKind) emit();
        lastKind = a.kind;
        run += a.count;
    }
    emit();
    flush();
    return out;
}

std::vector<std::vector<LogEntry>> YarnWork::patternRows() const {
    std::vector<std::vector<LogEntry>> rows(1);
    for (const LogEntry& a : log_) {
        if (a.kind == "turn") { rows.emplace_back(); continue; }
        rows.back().push_back(a);
    }
    if (rows.back().empty() && rows.size() > 1) rows.pop_back();
    return rows;
}

void YarnWork::toggleExtraLoop(int loop) {
    if (loop < 0 || loop >= (int)loops.size()) return;
    const auto it = std::find(extra_.begin(), extra_.end(), loop);
    if (it != extra_.end()) extra_.erase(it);
    else extra_.push_back(loop);
}

// ---------------------------------------------------------------------------------------------------------------- checks on the topology

namespace {

// does the segment a -> a + d pierce the fan of triangles (c, t[i], t[i+1]) over the polygon `ring`? returns +1 / -1 per crossing, summed
int pierce(const Vec3& a, const Vec3& d, const Vec3& c, const std::vector<Vec3>& ring) {
    int total = 0;
    const size_t n = ring.size();
    for (size_t i = 0; i < n; ++i) {
        const Vec3 &t0 = ring[i], &t1 = ring[(i + 1) % n];
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
        total += dot(d, cross(e1, e2)) > 0 ? 1 : -1;
    }
    return total;
}

}  // namespace

std::vector<std::array<int, 3>> loopPiercings(const YarnWork& w) {
    std::vector<std::array<int, 3>> out;
    std::vector<std::vector<int>> owner(w.strands.size());
    for (size_t s = 0; s < w.strands.size(); ++s) owner[s].assign(w.strands[s].pts.size(), -1);
    for (size_t li = 0; li < w.loops.size(); ++li)
        for (int i = w.loops[li].p0; i <= w.loops[li].p1; ++i) owner[w.loops[li].strand][i] = (int)li;
    for (size_t li = 0; li < w.loops.size(); ++li) {
        const YarnLoop& L = w.loops[li];
        if (L.r1 - L.r0 + 1 < 3) continue;
        const std::vector<Vec3>& P = w.strands[L.strand].pts;
        std::vector<Vec3> ring(P.begin() + L.r0, P.begin() + L.r1 + 1);
        Vec3 c{};
        for (const Vec3& p : ring) c += p;
        c = c / (Real)ring.size();
        std::map<int, int> byOwner;
        for (size_t s = 0; s < w.strands.size(); ++s) {
            const std::vector<Vec3>& Q = w.strands[s].pts;
            for (int i = 0; i + 1 < (int)Q.size(); ++i) {
                if ((int)s == L.strand && i >= L.p0 - 1 && i + 1 <= L.p1 + 1) continue;   // the ring's own segments and the two that join it
                const int k = pierce(Q[i], Q[i + 1] - Q[i], c, ring);
                if (k != 0) byOwner[owner[s][i]] += k;
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
    for (size_t li = 0; li < w.loops.size(); ++li) {
        const YarnLoop& M = w.loops[li];
        const std::vector<Vec3>& Q = w.strands[M.strand].pts;
        for (int eyeLoop : M.through) {
            const YarnLoop& L = w.loops[eyeLoop];
            if (L.r1 - L.r0 + 1 < 3) continue;
            const std::vector<Vec3>& P = w.strands[L.strand].pts;
            std::vector<Vec3> ring(P.begin() + L.r0, P.begin() + L.r1 + 1);
            Vec3 c{};
            for (const Vec3& p : ring) c += p;
            c = c / (Real)ring.size();
            int plus = 0, minus = 0;
            for (int s = std::max(0, M.p0 - 1); s < M.p1; ++s) {   // from the segment that brings the yarn to the first leg
                if (s >= M.r0 - 1 && s <= M.r1) continue;           // the ring itself is not a leg
                const int k = pierce(Q[s], Q[s + 1] - Q[s], c, ring);
                if (k > 0) plus += k;
                else if (k < 0) minus += -k;
            }
            out.push_back({(int)li, eyeLoop, plus, minus});
        }
    }
    return out;
}

}  // namespace fl
