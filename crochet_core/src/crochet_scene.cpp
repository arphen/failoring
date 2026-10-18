// Scenes for a relaxed crochet model (docs/CROCHET_PLAN.md section 7.2). See crochet/scene.hpp.
#include "failoring/crochet/scene.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <cstdio>
#include <string>

#include "failoring/collider.hpp"
#include "failoring/error.hpp"

namespace fl {

namespace {

// Rotation taking unit vector a to unit vector b (Rodrigues; pi about a perpendicular axis when they are opposite).
Mat3 rotationFromTo(const Vec3& a, const Vec3& b) {
    Mat3 r;
    r.m[0] = r.m[4] = r.m[8] = 1;
    const Vec3 v = cross(a, b);
    const Real c = dot(a, b), s = length(v);
    if (s < 1e-12) {
        if (c > 0) return r;
        Vec3 u = normalized(cross(a, Vec3(1, 0, 0)));
        if (length(u) < 0.5) u = normalized(cross(a, Vec3(0, 0, 1)));
        r.m[0] = 2 * u.x * u.x - 1; r.m[1] = 2 * u.x * u.y;     r.m[2] = 2 * u.x * u.z;
        r.m[3] = 2 * u.y * u.x;     r.m[4] = 2 * u.y * u.y - 1; r.m[5] = 2 * u.y * u.z;
        r.m[6] = 2 * u.z * u.x;     r.m[7] = 2 * u.z * u.y;     r.m[8] = 2 * u.z * u.z - 1;
        return r;
    }
    const Vec3 k = v / s;
    const Real K[9] = {0, -k.z, k.y, k.z, 0, -k.x, -k.y, k.x, 0};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            Real kk = 0;
            for (int l = 0; l < 3; ++l) kk += K[3 * i + l] * K[3 * l + j];
            r.m[3 * i + j] += s * K[3 * i + j] + (1 - c) * kk;
        }
    return r;
}

}  // namespace

CrochetScene::CrochetScene(const CrochetModel& model, const StitchGraph& g, const LayoutReport& layout, const SceneOptions& opt) : opt_(opt) {
    const CrochetMesh& t = model.topo;
    // the strap: the Strip region with the most rows
    const LayoutRegion* strap = nullptr;
    for (const LayoutRegion& r : layout.regions)
        if (r.type == LayoutRegion::Strip && (strap == nullptr || r.lastRow - r.firstRow > strap->lastRow - strap->firstRow)) strap = &r;
    if (strap == nullptr) FL_THROW(std::runtime_error, "the scene hangs the model by its strap, but the pattern has no strap (no turned rows worked off a rim)");
    const int nv = static_cast<int>(t.vertexStitch.size());
    std::vector<char> isStrap(nv, 0);
    for (int v = 0; v < nv; ++v) {
        const int row = g.st[t.vertexStitch[v]].row;
        isStrap[v] = row >= strap->firstRow && row <= strap->lastRow;
    }
    // the middle two rows of the strap: kept vertices of rows mid and mid + 1
    const int mid = (strap->firstRow + strap->lastRow) / 2;
    Vec3 hang(0, 0, 0), body(0, 0, 0);
    int nh = 0, nb = 0;
    std::vector<int> middle;
    for (int v = 0; v < nv; ++v) {
        const int row = g.st[t.vertexStitch[v]].row;
        if (isStrap[v] && (row == mid || row == mid + 1)) {
            middle.push_back(v);
            hang += model.mesh.x0[v];
            ++nh;
        } else if (!isStrap[v]) {
            body += model.mesh.x0[v];
            ++nb;
        }
    }
    if (nh == 0 || nb == 0) FL_THROW(std::runtime_error, "the scene needs both a body and a strap with kept stitches");
    hang = hang / nh;
    body = body / nb;
    // orient: up = body centroid -> middle of the strap; the middle of the strap over the origin (x, z)
    const Mat3 R = rotationFromTo(normalized(hang - body), Vec3(0, 1, 0));
    std::vector<Vec3> pos(nv);
    const Vec3 hangRot = R * hang;
    for (int v = 0; v < nv; ++v) {
        Vec3 p = R * model.mesh.x0[v];
        p.x -= hangRot.x;
        p.z -= hangRot.z;
        pos[v] = p;
    }
    const Real hangY = hangRot.y;

    for (int v = 0; v < nv; ++v)
        if (!isStrap[v]) body_.push_back(v);
    for (const int row : layout.openingRows)
        for (int v = 0; v < nv; ++v)
            if (g.st[t.vertexStitch[v]].row == row) mouth_.push_back(v);

    // solver parameters
    Real thickness = 0, w = 1e300, h = 1e300;
    for (const FabricMaterial& m : model.materials) thickness = std::max(thickness, m.thickness);
    for (const StitchDims& d : model.dims) {
        w = std::min(w, d.w);
        h = std::min(h, d.h);
    }
    SolverParams sp;
    sp.gravity = Vec3(0, -9.81, 0);
    sp.newtonIterations = opt.newtonIterations;
    sp.contactDiagnostics = true;
    sp.selfCollision = true;
    sp.selfThickness = std::min(0.8 * thickness, 0.4 * std::min(w, h));
    solver_ = makeSolver(model, sp, &pos);
    if (opt.kind == SceneKind::HangPin) {
        for (const int v : middle) solver_->pinVertex(v);
        pinned_ = middle;
    } else {
        solver_->params().bodyFrictionMode = 2;
        // the peg inside the loop, pegGap below the inner apex of the strap (the middle rows' height minus half a fabric thickness)
        const Real inner = hangY - 0.5 * thickness;
        const Real yc = inner - opt.pegGap - opt.pegRadius;
        pegY_ = yc;
        peg_ = std::make_shared<CapsuleCollider>(Vec3(-0.5 * opt.pegLength, yc, 0), Vec3(0.5 * opt.pegLength, yc, 0), opt.pegRadius);
        solver_->setCollider(peg_);
    }
}

bool CrochetScene::advance(int frames) {
    for (int f = 0; f < frames && !stats_.settled; ++f) {
        solver_->step();
        ++stats_.frames;
        stats_.kineticEnergy = solver_->kineticEnergy();
        calmFrames_ = stats_.kineticEnergy < opt_.keTarget ? calmFrames_ + 1 : 0;
        if (calmFrames_ >= opt_.settleFrames) stats_.settled = true;
    }
    stats_.substeps = solver_->contactDiag().substeps;
    stats_.newtonIterations = solver_->contactDiag().newtonIters;
    stats_.iterCapHits = solver_->contactDiag().iterCapHits;
    return stats_.settled;
}

SceneStats CrochetScene::settle() {
    advance(opt_.maxFrames);
    stats_.selfIntersections = solver_->selfIntersections();
    stats_.minClearance = solver_->collider() ? solver_->minBodyClearance() : std::numeric_limits<Real>::infinity();
    return stats_;
}

std::vector<Vec3> CrochetScene::pinReactions() {
    std::vector<Vec3> out;
    const Real h = 1e-6;
    for (const int v : pinned_) {
        const Vec3 x0 = solver_->positions()[v];
        Vec3 g;
        for (int k = 0; k < 3; ++k) {
            Vec3 d(0, 0, 0);
            (k == 0 ? d.x : k == 1 ? d.y : d.z) = h;
            solver_->setVertexPosition(v, x0 + d);
            const Real ep = solver_->potentialEnergy(1.0);
            solver_->setVertexPosition(v, x0 - d);
            const Real em = solver_->potentialEnergy(1.0);
            (k == 0 ? g.x : k == 1 ? g.y : g.z) = (ep - em) / (2 * h);
        }
        solver_->setVertexPosition(v, x0);
        out.push_back(g);
    }
    return out;
}

// ---- the rigid load -----------------------------------------------------------------------------------------------

LoadSpec loadPreset(const std::string& name) {
    LoadSpec s;
    s.name = name;
    auto corner = [](Real thickness) { return std::min(0.010, 0.9 * 0.5 * thickness); };
    if (name == "mba13") {
        s.width = 0.3041; s.height = 0.2150; s.thickness = 0.0113; s.mass = 1.24;
    } else if (name == "mbp14") {
        s.width = 0.3126; s.height = 0.2212; s.thickness = 0.0155; s.mass = 1.55;
    } else if (name == "mbp16") {
        s.width = 0.3557; s.height = 0.2481; s.thickness = 0.0168; s.mass = 2.14;
    } else if (name.rfind("box:", 0) == 0) {
        double l = 0, w = 0, h = 0, kg = 0;
        if (std::sscanf(name.c_str(), "box:%lfx%lfx%lf_mm:%lf", &l, &w, &h, &kg) != 4 || !(l > 0 && w > 0 && h > 0 && kg > 0))
            FL_THROW(std::invalid_argument, "load '" + name + "' is not box:LxWxH_mm:kg with positive numbers");
        s.width = l * 1e-3; s.height = w * 1e-3; s.thickness = h * 1e-3; s.mass = kg;
    } else {
        FL_THROW(std::invalid_argument, "unknown load '" + name + "' (mba13, mbp14, mbp16 or box:LxWxH_mm:kg)");
    }
    s.cornerRadius = corner(s.thickness);
    return s;
}

FitResult fitCheck(const CrochetModel& model, const StitchGraph& g, const LayoutReport& layout, const LoadSpec& load) {
    FitResult r;
    if (layout.openingRows.empty() || layout.depth <= 0) {
        r.message = "the pattern has no mouth to measure (no round bag and no folded piece)";
        return r;
    }
    // the rest circumference of the mouth: the stitch widths of the last round; two edge rows of a folded piece are the
    // two sides of the mouth, so its circumference is twice the width of one
    Real circumference = 0;
    for (const int row : layout.openingRows)
        for (const Stitch& s : g.st)
            if (s.row == row && model.topo.stitchVertex[s.id] >= 0) circumference += model.dims[s.id].w;
    if (layout.openingRows.size() == 1) {
        // a round: the whole ring is the circumference
    } else {
        circumference = 2 * (circumference / layout.openingRows.size());
    }
    r.known = true;
    r.innerWidth = 0.5 * circumference;
    r.innerDepth = layout.depth;
    r.widthRatio = load.width / r.innerWidth;
    r.heightRatio = load.height / r.innerDepth;
    const Real worst = std::max(r.widthRatio, r.heightRatio);
    r.fits = worst <= 1.05;
    r.tight = worst > 0.95 && worst <= 1.05;
    r.shortfall = std::max({0.0, load.width - r.innerWidth, load.height - r.innerDepth});
    char buf[256];
    if (!r.fits)
        std::snprintf(buf, sizeof buf, "the load is %.1f cm %s than the bag (%.1f x %.1f cm load, %.1f x %.1f cm inside)", r.shortfall * 100,
                      load.width - r.innerWidth >= load.height - r.innerDepth ? "wider" : "taller", load.width * 100, load.height * 100, r.innerWidth * 100, r.innerDepth * 100);
    else if (r.tight)
        std::snprintf(buf, sizeof buf, "tight fit: the load is %.0f %% of the bag's inside", 100 * worst);
    else
        std::snprintf(buf, sizeof buf, "fits: the load is %.0f %% of the bag's inside", 100 * worst);
    r.message = buf;
    return r;
}

RigidLoad::RigidLoad(CrochetScene& scene, const LoadSpec& load) : scene_(scene), spec_(load) {
    if (scene.mouth().empty()) FL_THROW(std::runtime_error, "RigidLoad: the model has no mouth (layout.openingRows is empty)");
    const std::vector<Vec3>& x = scene.solver().positions();
    Vec3 centre(0, 0, 0);
    Real top = -1e300;
    for (const int v : scene.mouth()) {
        centre += x[v];
        top = std::max(top, x[v].y);
    }
    centre = centre / static_cast<Real>(scene.mouth().size());
    // the bottom face 1 cm above the mouth, centred on it
    c_ = Vec3(centre.x, top + 0.01 + 0.5 * spec_.height, centre.z);
    Mat3 rot;
    rot.m[0] = rot.m[4] = rot.m[8] = 1;
    box_ = std::make_shared<RoundedBoxCollider>(c_, rot, Vec3(0.5 * spec_.width, 0.5 * spec_.height, 0.5 * spec_.thickness), spec_.cornerRadius);
    if (scene.peg() != nullptr) {
        auto both = std::make_shared<UnionCollider>();
        both->add(scene.peg());
        both->add(box_);
        scene.solver().setCollider(both);
    } else {
        scene.solver().setCollider(box_);
    }
    scene.solver().params().bodyFrictionMode = 2;
    scene.solver().params().newtonIterations = std::max(scene.solver().params().newtonIterations, 60);  // contact needs more than the 30 of a free hang
    dt_ = scene.solver().params().dt;
    stats_.center = c_;
}

Vec3 RigidLoad::forceFromCloth() {
    ClothSolver& s = scene_.solver();
    const std::vector<Real>& f = s.rigidContactForces();
    const std::vector<Vec3>& n = s.contactNormals();
    const std::vector<Vec3>& x = s.positions();
    Vec3 total(0, 0, 0);
    if (f.size() != x.size()) return total;
    for (size_t i = 0; i < x.size(); ++i) {
        if (!(f[i] > 0)) continue;
        Vec3 nb;
        const Real db = box_->eval(x[i], nb);
        if (scene_.peg() != nullptr) {
            Vec3 np;
            if (scene_.peg()->eval(x[i], np) < db) continue;  // the force belongs to the peg, not the box
        }
        total -= n[i] * f[i];  // the cloth is pushed along +n; the box gets the opposite
    }
    return total;
}

void RigidLoad::frame(const Vec3& dc) {
    ClothSolver& s = scene_.solver();
    c_ += dc;
    Mat3 rot;
    rot.m[0] = rot.m[4] = rot.m[8] = 1;
    box_->setPose(c_, rot);
    // the box surface moves by dc over the frame: tell the friction, for the vertices that belong to the box
    const std::vector<Vec3>& x = s.positions();
    std::vector<Vec3> motion(x.size(), Vec3(0, 0, 0));
    for (size_t i = 0; i < x.size(); ++i) {
        Vec3 nb;
        const Real db = box_->eval(x[i], nb);
        if (db > 0.01) continue;
        if (scene_.peg() != nullptr) {
            Vec3 np;
            if (scene_.peg()->eval(x[i], np) < db) continue;
        }
        motion[i] = dc;
    }
    s.setColliderMotion(motion);
    s.step();
    contact_ = forceFromCloth();
}

bool RigidLoad::insertSteps(int frames) {
    const Real speed = 0.05;  // m/s, plan 7.3
    const Real weight = spec_.mass * 9.81;
    if (insertDone_) return true;
    for (int f = 0; f < frames; ++f) {
        // the lowest cloth point of the body: the stop height
        Real lowest = 1e300;
        for (const int v : scene_.body()) lowest = std::min(lowest, scene_.solver().positions()[v].y);
        const Real bottom = c_.y - 0.5 * spec_.height;
        if (bottom <= lowest + 0.01) {
            stats_.stopReason = "bottom face 1 cm above the lowest cloth point";
            stats_.inserted = insertDone_ = true;
            break;
        }
        if (stats_.insertFrames > 0 && contact_.y > weight) {
            stats_.stopReason = "vertical contact force above the box weight";
            stats_.inserted = insertDone_ = true;
            break;
        }
        frame(Vec3(0, -speed * dt_, 0));
        ++stats_.insertFrames;
    }
    stats_.center = c_;
    return insertDone_;
}

bool RigidLoad::insert(int maxFrames) {
    stats_.stopReason = "frame cap";
    insertSteps(maxFrames);
    return stats_.inserted;
}

bool RigidLoad::releaseSteps(int frames) {
    const Real weight = spec_.mass * 9.81;
    for (int f = 0; f < frames && !stats_.released; ++f) {
        const Vec3 force = Vec3(0, -weight, 0) + contact_;
        v_ += force * (dt_ / spec_.mass);
        v_ *= std::exp(-damping_ * dt_);
        Vec3 dc = v_ * dt_;
        const Real len = length(dc);
        if (len > 2e-3) dc = dc * (2e-3 / len);
        frame(dc);
        ++stats_.releaseFrames;
        const Vec3 net = Vec3(0, -weight, 0) + contact_;
        stats_.netForceFraction = length(net) / weight;
        stats_.speed = length(v_);
        calm_ = (stats_.netForceFraction < 0.02 && stats_.speed < 1e-3) ? calm_ + 1 : 0;
        if (calm_ >= 30) stats_.released = true;
    }
    stats_.center = c_;
    return stats_.released;
}

bool RigidLoad::release(int maxFrames) { return releaseSteps(maxFrames); }

}  // namespace fl
