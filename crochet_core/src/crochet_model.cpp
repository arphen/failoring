// Mesh topology -> IntrinsicMesh + materials + point seams (docs/CROCHET_PLAN.md
// sections 6.5-6.7). The decisions are listed in crochet/model.hpp.
#include "failoring/crochet/model.hpp"

#include <cmath>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <tuple>
#include <unordered_map>

#include "failoring/error.hpp"

namespace fl {

namespace {

// Plan 4.4 hinge table (UNVERIFIED starters); rest angles RS-relative, degrees, positive = toward the WS.
struct HingeRule {
    Real bendScale = 1;
    Real restDeg = 0;
};
constexpr HingeRule kBlo{0.30, +25};
constexpr HingeRule kFlo{0.35, -25};
constexpr HingeRule kSlst{1.50, 0};
constexpr HingeRule kSurface{0.70, 0};
// Surface sl st stiffening of the faces next to the edge it is worked into (plan 4.4).
constexpr Real kSurfaceCourseK = 3.0;
constexpr Real kSurfaceLockCourse = 0.5;

StitchType stitchTypeOf(const std::string& t, int id) {
    if (t == "ch") return StitchType::Ch;
    if (t == "slst") return StitchType::SlSt;
    if (t == "sc") return StitchType::Sc;
    if (t == "hdc") return StitchType::Hdc;
    if (t == "dc") return StitchType::Dc;
    if (t == "tr") return StitchType::Tr;
    FL_THROW(std::runtime_error, "stitch " + std::to_string(id) + " has unknown type '" + t + "'");
}

Insertion insertionOf(const std::string& loop) {
    if (loop == "blo") return Insertion::BLO;
    if (loop == "flo") return Insertion::FLO;
    if (loop == "space") return Insertion::Space;
    return Insertion::Both;  // both, fpost, bpost
}

std::uint64_t edgeKey(int a, int b) {
    if (a > b) std::swap(a, b);
    return (static_cast<std::uint64_t>(a) << 32) | static_cast<std::uint32_t>(b);
}

HingeRule ruleFor(const Calibration* cal, const char* name, const HingeRule& dflt) {
    if (cal == nullptr) return dflt;
    const auto it = cal->hinge.find(name);
    if (it == cal->hinge.end()) return dflt;
    return {it->second.bendScale, it->second.restAngleDeg};
}

// Accumulate hinge modifiers per edge: bendScale multiplies; a non-zero rest angle is kept (the first one wins).
class HingeBook {
public:
    void add(int a, int b, Real scale, Real restRad) {
        if (a == b || a < 0 || b < 0) return;
        const std::uint64_t k = edgeKey(a, b);
        auto it = index_.find(k);
        if (it == index_.end()) {
            HingeMod h;
            h.a = std::min(a, b);
            h.b = std::max(a, b);
            h.bendScale = scale;
            h.restAngle = restRad;
            index_[k] = mods_.size();
            mods_.push_back(h);
            return;
        }
        HingeMod& h = mods_[it->second];
        h.bendScale *= scale;
        if (h.restAngle == 0) h.restAngle = restRad;
    }
    std::vector<HingeMod> take() { return std::move(mods_); }

private:
    std::unordered_map<std::uint64_t, size_t> index_;
    std::vector<HingeMod> mods_;
};

}  // namespace

CrochetModel buildCrochetModel(const StitchGraph& g, const Yarn& yarn, const ModelOptions& opt) {
    CrochetModel out;
    const std::vector<StitchDims>& dims = out.dims = graphDims(g, yarn, opt.hook_m, opt.tension, opt.calibration);
    out.topo = buildMeshTopology(g, dims);
    const CrochetMesh& t = out.topo;
    const int nv = static_cast<int>(t.vertexStitch.size());
    const int nt = static_cast<int>(t.tris.size());
    const Real lambda = yarn.lambda() * 1e-3;  // kg per metre of yarn

    // --- yarn mass per stitch and rest area per source stitch
    std::vector<Real> yarnMass(g.st.size(), 0), ownedArea(g.st.size(), 0);
    for (const Stitch& s : g.st) {
        yarnMass[s.id] = dims[s.id].yarnLen * lambda;
        out.yarnMass += yarnMass[s.id];
    }
    std::vector<Real> triArea(nt, 0);
    for (int i = 0; i < nt; ++i) {
        const auto& r = t.triRest[i];
        triArea[i] = 0.5 * std::fabs(cross(r[1] - r[0], r[2] - r[0]));
        ownedArea[t.triSource[i]] += triArea[i];
    }

    // --- faces next to a surface sl st edge are stiffened (plan 4.4)
    std::vector<char> surfaceStiff(nt, 0);
    if (!t.surface.empty()) {
        std::unordered_map<std::uint64_t, std::vector<int>> edgeTris;
        for (int i = 0; i < nt; ++i)
            for (int k = 0; k < 3; ++k) edgeTris[edgeKey(t.tris[i][k], t.tris[i][(k + 1) % 3])].push_back(i);
        for (const MeshSurfacePair& sp : t.surface)
            for (size_t j = 0; j + 1 < sp.base.size(); ++j) {
                const auto it = edgeTris.find(edgeKey(sp.base[j], sp.base[j + 1]));
                if (it == edgeTris.end()) continue;
                for (const int tri : it->second) surfaceStiff[tri] = 1;
            }
    }

    // --- materials: one per (type, insertion, density, surface stiffening)
    std::map<std::pair<int, int>, FabricMaterial> baseFabric;
    auto fabricFor = [&](const Stitch& s) -> const FabricMaterial& {
        const StitchType type = stitchTypeOf(s.type, s.id);
        const Insertion ins = insertionOf(s.loopRs);
        const auto key = std::make_pair(static_cast<int>(type), static_cast<int>(ins));
        auto it = baseFabric.find(key);
        if (it == baseFabric.end()) {
            const StitchMech mech = stitchMech(yarn, opt.hook_m, opt.tension, type, ins, opt.calibration);
            FabricMaterial f = toFabric(mech, s.type + "/" + s.loopRs);
            f.lockStrainWarp = mech.lockWale;     // u = warp = wale (plan 2.3)
            f.lockStrainWeft = mech.lockCourse;
            f.lockStiffnessRatio = mech.lockRatio;
            it = baseFabric.emplace(key, f).first;
        }
        return it->second;
    };
    std::map<std::tuple<int, int, long long, int>, int> matIndex;
    out.mesh.triMaterial.assign(nt, 0);
    for (int i = 0; i < nt; ++i) {
        const Stitch& s = g.st[t.triSource[i]];
        const Real density = yarnMass[s.id] / ownedArea[s.id];
        const auto key = std::make_tuple(static_cast<int>(stitchTypeOf(s.type, s.id)), static_cast<int>(insertionOf(s.loopRs)),
                                         std::llround(density * 1e6), static_cast<int>(surfaceStiff[i]));
        auto it = matIndex.find(key);
        if (it == matIndex.end()) {
            FabricMaterial f = fabricFor(s);
            f.density = density;
            std::string name = s.type + "/" + s.loopRs;
            if (surfaceStiff[i]) {
                f.tensileWeft *= kSurfaceCourseK;
                f.lockStrainWeft *= kSurfaceLockCourse;
                name += "+surface";
            }
            f.id = f.name = name + "#" + std::to_string(out.materials.size());
            it = matIndex.emplace(key, static_cast<int>(out.materials.size())).first;
            out.materialNames.push_back(f.id);
            out.materials.push_back(std::move(f));
        }
        out.mesh.triMaterial[i] = it->second;
    }

    // --- IntrinsicMesh geometry + extra mass (stitches that own no triangle)
    out.mesh.tris = t.tris;
    out.mesh.triRest = t.triRest;
    out.mesh.extraMass.assign(nv, 0);
    for (const Stitch& s : g.st) {
        if (ownedArea[s.id] > 0) continue;
        const int v = t.stitchVertex[s.id] >= 0 ? t.stitchVertex[s.id] : t.dropTo[s.id];
        if (v >= 0) out.mesh.extraMass[v] += yarnMass[s.id];
    }

    // --- hinge modifiers (plan 4.4, 6.6)
    const HingeRule blo = ruleFor(opt.calibration, "blo", kBlo), flo = ruleFor(opt.calibration, "flo", kFlo);
    const HingeRule slst = ruleFor(opt.calibration, "slst", kSlst), surface = ruleFor(opt.calibration, "surface", kSurface);
    HingeBook hinges;
    auto solverAngle = [](const HingeRule& r) { return kWsFoldSign * r.restDeg * kPi / 180.0; };
    for (const MeshPolygon& pl : t.polygons) {
        const Stitch& s = g.st[pl.source];
        const HingeRule* loopRule = s.loopRs == "blo" ? &blo : s.loopRs == "flo" ? &flo : nullptr;
        for (size_t j = 0; j + 1 < pl.base.size(); ++j) {
            if (loopRule != nullptr) hinges.add(pl.base[j], pl.base[j + 1], loopRule->bendScale, solverAngle(*loopRule));
            if (s.type == "slst") hinges.add(pl.base[j], pl.base[j + 1], slst.bendScale, solverAngle(slst));
        }
        if (s.type == "slst")
            for (size_t j = 0; j + 1 < pl.top.size(); ++j) hinges.add(pl.top[j], pl.top[j + 1], slst.bendScale, solverAngle(slst));
    }
    for (const MeshSurfacePair& sp : t.surface)
        for (size_t j = 0; j + 1 < sp.base.size(); ++j) hinges.add(sp.base[j], sp.base[j + 1], surface.bendScale, solverAngle(surface));
    out.mesh.hingeMods = hinges.take();

    // --- point seams (plan 6.7)
    for (const PointSeam& ps : pointSeams(g)) {
        const int a = t.stitchVertex[ps.stitch] >= 0 ? t.stitchVertex[ps.stitch] : t.dropTo[ps.stitch];
        const int b = t.stitchVertex[ps.partner] >= 0 ? t.stitchVertex[ps.partner] : t.dropTo[ps.partner];
        if (a < 0 || b < 0 || a == b) {
            out.warnings.push_back("point seam between stitch " + std::to_string(ps.stitch) + " and " + std::to_string(ps.partner) +
                                   " has no two distinct mesh vertices");
            continue;
        }
        out.pointSeams.push_back({a, b, dims[ps.stitch].w});
    }

    out.warnings.insert(out.warnings.begin(), t.warnings.begin(), t.warnings.end());
    return out;
}

}  // namespace fl
