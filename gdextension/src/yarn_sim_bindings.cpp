#include "yarn_sim_bindings.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>

namespace godot {

namespace {

Vector3 v3(const fl::Vec3& p) { return Vector3((real_t)p.x, (real_t)p.y, (real_t)p.z); }

Dictionary toDict(const fl::YarnMesh& m) {
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
    for (size_t t = 0; t < m.tri.size(); ++t)
        for (int k = 0; k < 3; ++k) idx.set((int64_t)t * 3 + k, m.tri[t][k]);
    Dictionary d;
    d["positions"] = pos;
    d["normals"] = nrm;
    d["colors"] = col;
    d["indices"] = idx;
    return d;
}

void append(fl::YarnMesh& m, const fl::YarnMesh& o) {
    const int off = (int)m.pos.size();
    m.pos.insert(m.pos.end(), o.pos.begin(), o.pos.end());
    m.nrm.insert(m.nrm.end(), o.nrm.begin(), o.nrm.end());
    m.col.insert(m.col.end(), o.col.begin(), o.col.end());
    for (const auto& t : o.tri) m.tri.push_back({t[0] + off, t[1] + off, t[2] + off});
}

}  // namespace

int YarnSimSession::record(const String& procedure) {
    frames_.clear();
    fl::YarnSim sim;
    radius_ = sim.params.radius;
    if (procedure != String("slip_knot")) return 0;
    sim.build(fl::slipKnotLayout(sim.params.radius));
    sim.onFrame = [&] {
        Frame f;
        f.yarn = sim.rs.x;
        f.hook.assign(sim.rs.statics.begin(), sim.rs.statics.end());
        f.pose = sim.hook;
        f.phase = sim.phase;
        f.loops = sim.loopsOnHook(fl::Vec3{0, 0, 1});
        frames_.push_back(std::move(f));
    };
    fl::makeSlipKnot(sim);
    return (int)frames_.size();
}

Dictionary YarnSimSession::get_yarn_mesh(int i, int sides, int subdiv) const {
    if (i < 0 || i >= (int)frames_.size()) return Dictionary();
    fl::YarnMeshOptions o;
    o.sides = sides > 2 ? sides : 10;
    o.subdiv = subdiv > 0 ? subdiv : 2;
    o.colour = fl::Vec3{0.80, 0.30, 0.22};
    return toDict(fl::buildTubeMesh(frames_[i].yarn, radius_, o.colour, o));
}

Dictionary YarnSimSession::get_hook_mesh(int i) const {
    if (i < 0 || i >= (int)frames_.size()) return Dictionary();
    fl::YarnMesh m;
    fl::YarnMeshOptions o;
    o.plyContrast = 0.0;
    o.sides = 20;
    o.subdiv = 1;
    for (const fl::StaticCapsule& c : frames_[i].hook) {
        std::vector<fl::Vec3> pts;
        for (int k = 0; k <= 4; ++k) pts.push_back(c.a + (c.b - c.a) * (k / 4.0));
        append(m, fl::buildTubeMesh(pts, c.r, fl::Vec3{0.74, 0.76, 0.80}, o));
    }
    return toDict(m);
}

Dictionary YarnSimSession::get_frame_info(int i) const {
    Dictionary d;
    if (i < 0 || i >= (int)frames_.size()) return d;
    const Frame& f = frames_[i];
    d["phase"] = String(f.phase.c_str());
    d["tip"] = v3(f.pose.tip);
    d["axis"] = v3(f.pose.axis());
    d["throat"] = v3(f.pose.throat());
    d["loops_on_hook"] = f.loops;
    d["radius"] = radius_;
    return d;
}

void YarnSimSession::_bind_methods() {
    ClassDB::bind_method(D_METHOD("record", "procedure"), &YarnSimSession::record);
    ClassDB::bind_method(D_METHOD("get_frame_count"), &YarnSimSession::get_frame_count);
    ClassDB::bind_method(D_METHOD("get_yarn_mesh", "i", "sides", "subdiv"), &YarnSimSession::get_yarn_mesh);
    ClassDB::bind_method(D_METHOD("get_hook_mesh", "i"), &YarnSimSession::get_hook_mesh);
    ClassDB::bind_method(D_METHOD("get_frame_info", "i"), &YarnSimSession::get_frame_info);
}

}  // namespace godot
