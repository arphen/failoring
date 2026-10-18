// Scenes for a relaxed crochet model (docs/CROCHET_PLAN.md section 7.2). See crochet/scene.hpp.
#include "failoring/crochet/scene.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
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
        solver_->setCollider(std::make_shared<CapsuleCollider>(Vec3(-0.5 * opt.pegLength, yc, 0), Vec3(0.5 * opt.pegLength, yc, 0), opt.pegRadius));
    }
}

SceneStats CrochetScene::settle() {
    for (int f = 0; f < opt_.maxFrames; ++f) {
        solver_->step();
        ++stats_.frames;
        stats_.kineticEnergy = solver_->kineticEnergy();
        calmFrames_ = stats_.kineticEnergy < opt_.keTarget ? calmFrames_ + 1 : 0;
        if (calmFrames_ >= opt_.settleFrames) {
            stats_.settled = true;
            break;
        }
    }
    stats_.substeps = solver_->contactDiag().substeps;
    stats_.newtonIterations = solver_->contactDiag().newtonIters;
    stats_.iterCapHits = solver_->contactDiag().iterCapHits;
    stats_.selfIntersections = solver_->selfIntersections();
    stats_.minClearance = solver_->collider() ? solver_->minBodyClearance() : std::numeric_limits<Real>::infinity();
    return stats_;
}

}  // namespace fl
