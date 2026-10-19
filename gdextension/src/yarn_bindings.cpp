#include "yarn_bindings.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include "failoring/crochet/yarn_render.hpp"

using namespace godot;

namespace {
Vector3 v3(const fl::Vec3& v) { return Vector3((real_t)v.x, (real_t)v.y, (real_t)v.z); }
}  // namespace

YarnSession::YarnSession() { reset(); }

fl::Vec3 YarnSession::pivot() const {
    fl::Vec3 c{};
    if (work_.rod.x.empty()) return c;
    for (const fl::Vec3& p : work_.rod.x) c += p;
    return c / (double)work_.rod.x.size();
}

fl::Vec3 YarnSession::view(const fl::Vec3& p) const { return expand_ == 1.0 ? p : pivot() + (p - pivot()) * expand_; }

fl::Vec3 YarnSession::unview(const fl::Vec3& p) const { return expand_ == 1.0 ? p : pivot() + (p - pivot()) / expand_; }

void YarnSession::set_view(double expand, double thickness) {
    expand_ = expand < 0.2 ? 0.2 : expand;
    thickness_ = thickness < 0.1 ? 0.1 : thickness;
}

void YarnSession::reset() {
    grip_ = -1;
    work_ = fl::YarnWork();   // the tuned tight lattice, a soft shape memory, strands that give a little (YarnWork's defaults)
    work_.slipKnot(fl::Vec3{0, 0, 0}, fl::Vec3{0.80, 0.30, 0.22});
    started_ = true;
}

Dictionary YarnSession::action(const String& kind_in, int opening) {
    release();
    Dictionary r;
    r["ok"] = false;
    String kind = kind_in;
    if (work_.finished() && kind != "finish") {
        r["msg"] = String("the work is finished");
        return r;
    }
    fl::Loops which = fl::Loops::Both;
    if (kind.ends_with("_front")) { which = fl::Loops::Front; kind = kind.substr(0, kind.length() - 6); }
    else if (kind.ends_with("_back")) { which = fl::Loops::Back; kind = kind.substr(0, kind.length() - 5); }
    int loop = -1;
    const int loops0 = (int)work_.loops.size();
    if (kind == "chain") {
        loop = work_.stitchChain();
    } else if (kind == "sc" || kind == "hdc" || kind == "dc" || kind == "slst") {
        loop = work_.stitch(std::string(kind.utf8().get_data()), opening, which);
        if (loop < 0) r["msg"] = String("nothing to work into: turn the work first");
    } else if (kind == "finish") {
        loop = work_.finishOff();
        if (loop < 0) r["msg"] = String("nothing to fasten off");
    } else if (kind == "skip") {
        if (work_.stitchSkip()) {
            r["ok"] = true;
            loop = work_.hook.empty() ? -1 : work_.hook.back();
        } else {
            r["msg"] = String("nothing to skip");
        }
    } else if (kind == "turn") {
        // `opening` is the number of turning chains (-1: none after the foundation chain, else one)
        const int chains = opening >= 0 ? opening : (work_.row == 0 ? 0 : 1);
        work_.stitchTurn(chains);
        loop = work_.hook.empty() ? -1 : work_.hook.back();
        r["ok"] = true;
    } else {
        r["msg"] = String("unknown action");
    }
    if (loop >= 0) r["ok"] = true;
    r["loop"] = loop;
    Array made;
    if (kind != "skip")
        for (int i = loops0; i < (int)work_.loops.size(); ++i) made.push_back(i);
    r["loops"] = made;
    r["particles"] = work_.particleCount();
    r["row"] = work_.row;
    r["cursor"] = work_.cursor();
    return r;
}

void YarnSession::set_colour(const Color& c, const String& when) {
    work_.setNextColour(fl::Vec3{c.r, c.g, c.b}, when == "stitch" ? fl::ColourAt::Stitch : fl::ColourAt::PullThrough);
}

void YarnSession::toggle_extra(int loop) { work_.toggleExtraLoop(loop); }

TypedArray<Array> YarnSession::get_pattern_rows() const {
    TypedArray<Array> out;
    for (const auto& row : work_.patternRows()) {
        Array r;
        for (const fl::LogEntry& e : row) {
            Dictionary d;
            d["kind"] = String(e.kind.c_str());
            d["count"] = e.count;
            if (e.strand >= 0 && e.strand < (int)work_.strands.size()) {
                const fl::Vec3 c = work_.strands[e.strand].colour;
                d["colour"] = Color((float)c.x, (float)c.y, (float)c.z);
            } else {
                d["colour"] = Color(0.7f, 0.7f, 0.7f);
            }
            r.push_back(d);
        }
        out.push_back(r);
    }
    return out;
}

TypedArray<Color> YarnSession::get_colours() const {
    TypedArray<Color> out;
    for (const fl::Strand& s : work_.strands) out.push_back(Color((float)s.colour.x, (float)s.colour.y, (float)s.colour.z));
    return out;
}

void YarnSession::tick(int n) {
    for (int i = 0; i < n; ++i) work_.rod.step(1.0 / 120.0);
}

Dictionary YarnSession::mesh_to_dict(const fl::YarnMesh& m) {
    PackedVector3Array pos, nrm;
    PackedColorArray col;
    PackedInt32Array idx;
    pos.resize((int64_t)m.pos.size());
    nrm.resize((int64_t)m.pos.size());
    col.resize((int64_t)m.pos.size());
    for (size_t i = 0; i < m.pos.size(); ++i) {
        pos.set((int64_t)i, v3(m.pos[i]));
        nrm.set((int64_t)i, v3(m.nrm[i]));
        col.set((int64_t)i, Color((float)m.col[i].x, (float)m.col[i].y, (float)m.col[i].z));
    }
    idx.resize((int64_t)m.tri.size() * 3);
    for (size_t t = 0; t < m.tri.size(); ++t) {
        idx.set((int64_t)t * 3, m.tri[t][0]);
        idx.set((int64_t)t * 3 + 1, m.tri[t][1]);
        idx.set((int64_t)t * 3 + 2, m.tri[t][2]);
    }
    Dictionary d;
    d["positions"] = pos;
    d["normals"] = nrm;
    d["colors"] = col;
    d["indices"] = idx;
    return d;
}

Dictionary YarnSession::get_mesh(int up_to, int sides, int subdiv) {
    fl::YarnMeshOptions o;
    o.sides = sides > 2 ? sides : 8;
    o.subdiv = subdiv > 0 ? subdiv : 2;
    o.maxParticles = up_to;
    o.radiusScale = thickness_;
    o.segmentColour = work_.segmentColours();
    fl::YarnMesh m = fl::buildYarnMesh(work_.rod, o);
    if (expand_ != 1.0) {
        const fl::Vec3 pv = pivot();
        for (fl::Vec3& p : m.pos) p = pv + (p - pv) * expand_;
    }
    return mesh_to_dict(m);
}

Dictionary YarnSession::anim_frame(int loop, double s, double wrap, const PackedVector3Array& hang, double phi, int sides, int subdiv) {
    Dictionary out;
    if (loop < 0 || loop >= (int)work_.loops.size()) return out;
    fl::YarnWork::PullFrame f = work_.pullFrame(loop, s, wrap);
    if (hang.size() >= 2 && phi < 1.0) {
        std::vector<fl::Vec3> curve;
        for (int64_t i = 0; i < hang.size(); ++i) curve.push_back(unview(fl::Vec3{hang[i].x, hang[i].y, hang[i].z}));
        f.applyHang(curve, phi);
    }
    const fl::YarnLoop& L = work_.loops[loop];
    std::vector<fl::Vec3> saved;
    for (size_t i = 0; i < f.pts.size(); ++i) {
        saved.push_back(work_.rod.x[f.first + i]);
        work_.rod.x[f.first + i] = f.pts[i];
    }
    fl::YarnMeshOptions o;
    o.sides = sides > 2 ? sides : 8;
    o.subdiv = subdiv > 0 ? subdiv : 2;
    o.maxParticles = std::min(work_.rod.particleCount(), work_.strands[L.strand].first + L.p1 + 2);
    o.radiusScale = thickness_;
    o.segmentColour = work_.segmentColours();
    fl::YarnMesh m = fl::buildYarnMesh(work_.rod, o);
    for (size_t i = 0; i < f.pts.size(); ++i) work_.rod.x[f.first + i] = saved[i];
    if (expand_ != 1.0) {
        const fl::Vec3 pv = pivot();
        for (fl::Vec3& p : m.pos) p = pv + (p - pv) * expand_;
    }
    out = mesh_to_dict(m);
    out["tip"] = v3(view(f.tip));
    out["axis"] = v3(f.axis);
    out["eye"] = v3(view(f.eye));
    out["run_end"] = v3(view(f.pts.back()));
    out["run_dir"] = v3(view(f.pts.back()) - view(f.pts[f.pts.size() - 2]));
    out["path_length"] = f.pathLength;
    out["nat_length"] = f.natLength;
    out["strand"] = L.strand;
    out["through"] = f.through;
    return out;
}

Dictionary YarnSession::tube(const PackedVector3Array& points, const Color& colour, double radius, int sides, int subdiv) const {
    std::vector<fl::Vec3> pts;
    for (int64_t i = 0; i < points.size(); ++i) pts.push_back(fl::Vec3{points[i].x, points[i].y, points[i].z});
    fl::YarnMeshOptions o;
    o.sides = sides > 2 ? sides : 8;
    o.subdiv = subdiv > 0 ? subdiv : 2;
    return mesh_to_dict(fl::buildTubeMesh(pts, radius, fl::Vec3{colour.r, colour.g, colour.b}, o));
}

Dictionary YarnSession::get_loop_mesh(int loop) {
    if (loop < 0 || loop >= (int)work_.loops.size()) return Dictionary();
    const fl::YarnLoop& l = work_.loops[loop];
    std::vector<fl::Vec3> pts;
    for (int i = l.p0; i <= l.p1; ++i) pts.push_back(view(work_.point(l.strand, i)));
    fl::YarnMeshOptions o;
    o.sides = 8;
    o.subdiv = 2;
    o.plyContrast = 0.0;
    return mesh_to_dict(fl::buildTubeMesh(pts, work_.rodParams.radius * 1.35 * thickness_, fl::Vec3{1.0, 0.9, 0.3}, o));
}

Dictionary YarnSession::pick(const Vector3& origin, const Vector3& dir) {
    Dictionary r;
    r["ok"] = false;
    const fl::Vec3 o{origin.x, origin.y, origin.z};
    fl::Vec3 d{dir.x, dir.y, dir.z};
    const double dl = fl::length(d);
    if (dl < 1e-12) return r;
    d = d / dl;
    // owner loop of every node
    std::vector<std::vector<int>> owner(work_.strands.size());
    for (size_t s = 0; s < work_.strands.size(); ++s) owner[s].assign(work_.strands[s].pts.size(), -1);
    for (size_t li = 0; li < work_.loops.size(); ++li)
        for (int i = work_.loops[li].p0; i <= work_.loops[li].p1; ++i) owner[work_.loops[li].strand][i] = (int)li;
    // 1. the yarn nearest to the ray
    const double reach = 3.5 * work_.rodParams.radius * expand_ * thickness_ + 1.0e-4;
    double best = reach;
    int bestLoop = -1, bestStrand = -1;
    fl::Vec3 bestPoint{};
    for (size_t s = 0; s < work_.strands.size(); ++s) {
        const auto& P = work_.strands[s].pts;
        for (size_t i = 0; i + 1 < P.size(); ++i) {
            if (owner[s][i] < 0 && owner[s][i + 1] < 0) continue;   // travel and tails belong to no loop
            const fl::SegSegResult sr = fl::closestSegmentSegment(o, o + d * 5.0, view(P[i]), view(P[i + 1]));
            const double dist = std::sqrt(sr.dist2);
            if (dist < best) {
                best = dist;
                bestLoop = owner[s][i] >= 0 ? owner[s][i] : owner[s][i + 1];
                bestStrand = (int)s;
                bestPoint = sr.pb;
            }
        }
    }
    // 2. failing that, a ring the ray goes through (the hole of a V)
    if (bestLoop < 0) {
        double bestT = 1e300;
        for (size_t li = 0; li < work_.loops.size(); ++li) {
            const fl::YarnLoop& l = work_.loops[li];
            if (l.r1 - l.r0 + 1 < 3) continue;
            std::vector<fl::Vec3> ring;
            fl::Vec3 c{};
            for (int i = l.r0; i <= l.r1; ++i) {
                ring.push_back(view(work_.point(l.strand, i)));
                c += ring.back();
            }
            c = c / (double)ring.size();
            for (size_t i = 0; i < ring.size(); ++i) {
                const fl::Vec3 &t0 = ring[i], &t1 = ring[(i + 1) % ring.size()];
                const fl::Vec3 e1 = t0 - c, e2 = t1 - c, pv = fl::cross(d, e2);
                const double det = fl::dot(e1, pv);
                if (std::fabs(det) < 1e-30) continue;
                const fl::Vec3 tv = o - c;
                const double u = fl::dot(tv, pv) / det;
                if (u < 0 || u > 1) continue;
                const fl::Vec3 qv = fl::cross(tv, e1);
                const double v = fl::dot(d, qv) / det;
                if (v < 0 || u + v > 1) continue;
                const double t = fl::dot(e2, qv) / det;
                if (t > 0 && t < bestT) {
                    bestT = t;
                    bestLoop = (int)li;
                    bestStrand = l.strand;
                    bestPoint = o + d * t;
                }
            }
        }
    }
    if (bestLoop < 0) return r;
    r["ok"] = true;
    r["loop"] = bestLoop;
    r["top"] = work_.loops[bestLoop].top;
    r["strand"] = bestStrand;
    r["point"] = v3(bestPoint);
    return r;
}

int YarnSession::get_particle_count() const { return work_.particleCount(); }

TypedArray<Dictionary> YarnSession::get_loops() const {
    TypedArray<Dictionary> out;
    for (size_t i = 0; i < work_.loops.size(); ++i) {
        const fl::EyeFrame e = work_.eye((int)i);
        Dictionary d;
        d["id"] = (int)i;
        d["row"] = work_.loops[i].row;
        d["live"] = work_.loops[i].live;
        d["top"] = work_.loops[i].top;
        d["probe"] = v3(view(work_.point(work_.loops[i].strand, work_.loops[i].r0 + (work_.loops[i].r1 - work_.loops[i].r0) / 2)));   // a point on its yarn
        d["centre"] = v3(view(e.centre));
        d["normal"] = v3(e.normal);
        d["radius"] = e.radius;
        out.push_back(d);
    }
    return out;
}

Dictionary YarnSession::get_state() const {
    Dictionary d;
    d["row"] = work_.row;
    d["cursor"] = work_.cursor();
    d["next"] = work_.nextOpening();
    PackedInt32Array targets, live;
    for (int t : work_.targetRow()) targets.push_back(t);
    for (int l : work_.hook) live.push_back(l);
    d["targets"] = targets;
    d["live"] = live;
    d["made"] = work_.rowStitches();
    const auto hp = work_.hookPose();
    if (hp.valid) {
        d["hook_tip"] = v3(view(hp.tip));
        d["hook_axis"] = v3(hp.axis);
    }
    d["text"] = String(work_.patternText().c_str());
    d["progress"] = v3(work_.progress);
    PackedInt32Array extra;
    for (int e : work_.extraLoops()) extra.push_back(e);
    d["extra"] = extra;
    Array ends;
    for (size_t si = 0; si < work_.strands.size(); ++si) {
        const fl::Strand& st = work_.strands[si];
        if (st.pts.size() < 2 || st.cut) continue;   // a yarn that is cut no longer runs to its ball
        const fl::Vec3 e = st.pts.back();
        const fl::Vec3 back = st.pts[st.pts.size() - 2];
        Dictionary en;
        en["strand"] = (int)si;
        en["end"] = v3(view(e));
        en["dir"] = v3(fl::normalized(view(e) - view(back)));
        en["colour"] = Color((float)st.colour.x, (float)st.colour.y, (float)st.colour.z);
        en["active"] = (int)si == work_.active;
        double len = 0.0;
        for (size_t k = 1; k < st.pts.size(); ++k) len += fl::length(st.pts[k] - st.pts[k - 1]);
        en["length"] = len;
        ends.push_back(en);
    }
    d["ends"] = ends;
    d["strands"] = (int)work_.strands.size();
    d["finished"] = work_.finished();
    d["pending_colour"] = work_.colourPending();
    const fl::Vec3 ac = work_.activeColour();
    d["colour"] = Color((float)ac.x, (float)ac.y, (float)ac.z);
    return d;
}

String YarnSession::get_pattern_text() const { return String(work_.patternText().c_str()); }

void YarnSession::set_param(const String& name, double value) {
    if (name == "shape_stiffness") { work_.rodParams.shapeStiffness = value; work_.rod.params.shapeStiffness = value; }
    else if (name == "contact_scale") { work_.rodParams.radiusScale = value; work_.rod.params.radiusScale = value; }
    else if (name == "damping") { work_.rodParams.damping = value; work_.rod.params.damping = value; }
}

Dictionary YarnSession::grab(const Vector3& origin, const Vector3& dir) {
    Dictionary r;
    r["ok"] = false;
    if (grip_ >= 0) release();
    const fl::Vec3 o{origin.x, origin.y, origin.z};
    fl::Vec3 d{dir.x, dir.y, dir.z};
    const double dl = fl::length(d);
    if (dl < 1e-12) return r;
    d = d / dl;
    int best = -1;
    double bestD = 3.0e-3 * expand_;   // within 3 mm of the ray
    for (size_t i = 0; i < work_.rod.x.size(); ++i) {
        const fl::Vec3 p = view(work_.rod.x[i]);
        const double t = fl::dot(p - o, d);
        if (t < 0) continue;
        const double dist = fl::length(p - (o + d * t));
        if (dist < bestD) { bestD = dist; best = (int)i; }
    }
    if (best < 0) return r;
    grip_ = best;
    work_.rod.freezeOutside(work_.rod.x[best], 0.022);
    work_.rod.pin(best);
    r["ok"] = true;
    r["particle"] = best;
    r["point"] = v3(view(work_.rod.x[best]));
    return r;
}

void YarnSession::drag_to(const Vector3& point) {
    if (grip_ < 0) return;
    fl::Vec3 target = unview(fl::Vec3{point.x, point.y, point.z});
    const fl::Vec3 home = work_.rod.anchor[grip_];
    const fl::Vec3 off = target - home;
    const double dl = fl::length(off);
    if (dl > 0.04) target = home + off * (0.04 / dl);   // no further than 4 cm from where it belongs
    work_.rod.movePin(grip_, target);
}

void YarnSession::release() {
    if (grip_ < 0) return;
    work_.rod.thaw();
    work_.rod.unpin(grip_);
    grip_ = -1;
}

void YarnSession::_bind_methods() {
    ClassDB::bind_method(D_METHOD("reset"), &YarnSession::reset);
    ClassDB::bind_method(D_METHOD("action", "kind", "opening"), &YarnSession::action);
    ClassDB::bind_method(D_METHOD("anim_frame", "loop", "s", "wrap", "hang", "phi", "sides", "subdiv"), &YarnSession::anim_frame);
    ClassDB::bind_method(D_METHOD("tick", "n"), &YarnSession::tick);
    ClassDB::bind_method(D_METHOD("get_mesh", "up_to", "sides", "subdiv"), &YarnSession::get_mesh);
    ClassDB::bind_method(D_METHOD("get_particle_count"), &YarnSession::get_particle_count);
    ClassDB::bind_method(D_METHOD("get_loops"), &YarnSession::get_loops);
    ClassDB::bind_method(D_METHOD("get_state"), &YarnSession::get_state);
    ClassDB::bind_method(D_METHOD("get_pattern_text"), &YarnSession::get_pattern_text);
    ClassDB::bind_method(D_METHOD("set_param", "name", "value"), &YarnSession::set_param);
    ClassDB::bind_method(D_METHOD("set_colour", "colour", "when"), &YarnSession::set_colour);
    ClassDB::bind_method(D_METHOD("get_colours"), &YarnSession::get_colours);
    ClassDB::bind_method(D_METHOD("toggle_extra", "loop"), &YarnSession::toggle_extra);
    ClassDB::bind_method(D_METHOD("get_pattern_rows"), &YarnSession::get_pattern_rows);
    ClassDB::bind_method(D_METHOD("set_view", "expand", "thickness"), &YarnSession::set_view);
    ClassDB::bind_method(D_METHOD("grab", "origin", "dir"), &YarnSession::grab);
    ClassDB::bind_method(D_METHOD("pick", "origin", "dir"), &YarnSession::pick);
    ClassDB::bind_method(D_METHOD("get_loop_mesh", "loop"), &YarnSession::get_loop_mesh);
    ClassDB::bind_method(D_METHOD("tube", "points", "colour", "radius", "sides", "subdiv"), &YarnSession::tube);
    ClassDB::bind_method(D_METHOD("drag_to", "point"), &YarnSession::drag_to);
    ClassDB::bind_method(D_METHOD("release"), &YarnSession::release);
    ClassDB::bind_method(D_METHOD("is_grabbed"), &YarnSession::is_grabbed);
}
