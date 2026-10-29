// The solver as flat arrays for the GPU stepper (GpuModel in cloth.hpp; gdextension/src/gpu_step.cpp runs it).
//
// The GPU runs the fast policy of stepImplicit (docs/GPU_SOLVER_NOTES.md): per substep one linearised solve with
// block-Jacobi CG, the line search on the variational energy, strain limiting, body push-out and self collision.
// Everything the CPU path has beyond that (barrier contact, contact multipliers, the soft skin, signed hinges, the
// post-solve friction of mode 0) is reported in GpuModel::unsupported and the caller keeps the CPU.
#include <algorithm>
#include <cstdint>

#include "failoring/cloth.hpp"

namespace fl {

GpuModel ClothSolver::gpuModel() {
    if (!finalized_) finalize();
    if (!patternValid_) buildBlockPattern();
    GpuModel m;
    auto no = [&](const char* why) { if (m.unsupported.empty()) m.unsupported = why; };
    if (p_.integrator != Integrator::Implicit) no("integrator is not implicit");
    if (p_.barrierContact) no("barrier contact");
    if (p_.contactAL) no("contact multipliers (contactAL)");
    if (skinOn()) no("compliant skin");
    if (p_.bodyFrictionMode == 0) no("body friction mode 0 (after the solve)");
    if (!extraCouple_.empty()) no("contact couplings in the Hessian pattern");
    for (const BendC& b : bendC_)
        if (b.rest != 0) { no("hinges with a rest angle"); break; }

    const int N = vertexCount();
    m.n = N;
    m.nTri = static_cast<int>(triC_.size());
    m.nBend = static_cast<int>(bendC_.size());
    m.nSeam = static_cast<int>(seamC_.size());
    m.nnz = K_.rowStart[N];
    m.mass.resize(N); m.w.resize(N); m.damp.resize(N); m.muK.resize(N); m.radius.resize(N);
    for (int i = 0; i < N; ++i) {
        m.mass[i] = static_cast<float>(mass_[i]);
        m.w[i] = static_cast<float>(w_[i]);
        m.damp[i] = static_cast<float>(damp_[i]);
        m.muK[i] = static_cast<float>(muK_[i]);
        m.radius[i] = static_cast<float>(radius_[i]);
    }
    for (const TriC& c : triC_) {
        m.triIdx.insert(m.triIdx.end(), {c.i0, c.i1, c.i2});
        for (Real v : {c.a, c.b, c.c, c.d, c.alphaU, c.alphaV, c.alphaS, c.comp, c.lockU, c.lockV, c.ratio}) m.triPar.push_back(static_cast<float>(v));
    }
    for (const BendC& b : bendC_) {
        m.bendIdx.insert(m.bendIdx.end(), {b.i[0], b.i[1], b.i[2], b.i[3]});
        m.bendAlpha.push_back(static_cast<float>(b.alpha));
    }
    for (const SeamC& s : seamC_) {
        m.seamIdx.insert(m.seamIdx.end(), {s.a, s.b0, s.b1});
        m.seamPar.push_back(static_cast<float>(s.t));
        m.seamPar.push_back(static_cast<float>(s.point ? -s.alpha : s.alpha));   // < 0: a point seam (PF[24] scales it)
    }
    m.rowStart = K_.rowStart;
    m.col = K_.col;
    m.diag = K_.diag;

    // gather lists (counting sort of the contributions by their target block / vertex)
    const int T = m.nTri, B = m.nBend;
    std::vector<std::pair<int, int>> hk, gv;  // (block or vertex, contribution)
    for (int t = 0; t < T; ++t) {
        const int idx[3] = {triC_[t].i0, triC_[t].i1, triC_[t].i2};
        for (int q = 0; q < 9; ++q) hk.push_back({triC_[t].slot[q], 9 * t + q});
        for (int a = 0; a < 3; ++a) gv.push_back({idx[a], 3 * t + a});
    }
    for (int h = 0; h < B; ++h) {
        for (int q = 0; q < 16; ++q) hk.push_back({bendC_[h].slot[q], 9 * T + 16 * h + q});
        for (int a = 0; a < 4; ++a) gv.push_back({bendC_[h].i[a], 3 * T + 4 * h + a});
    }
    for (int s = 0; s < m.nSeam; ++s) {
        const int idx[3] = {seamC_[s].a, seamC_[s].b0, seamC_[s].b1};
        for (int q = 0; q < 9; ++q) hk.push_back({seamC_[s].slot[q], 9 * T + 16 * B + 9 * s + q});
        for (int a = 0; a < 3; ++a) gv.push_back({idx[a], 3 * T + 4 * B + 3 * s + a});
    }
    auto csr = [](std::vector<std::pair<int, int>>& v, int rows, std::vector<int>& start, std::vector<int>& item) {
        std::stable_sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        start.assign(rows + 1, 0);
        for (const auto& p : v) ++start[p.first + 1];
        for (int r = 0; r < rows; ++r) start[r + 1] += start[r];
        item.resize(v.size());
        for (size_t k = 0; k < v.size(); ++k) item[k] = v[k].second;
    };
    csr(hk, m.nnz, m.blkStart, m.blkItem);
    csr(gv, N, m.gStart, m.gItem);

    // strain-limit edges, in the colour order the CPU projects them (buildStrainEdges)
    buildStrainEdges();
    for (const SlEdge& e : slEdges_) {
        m.edgeA.push_back(e.a);
        m.edgeB.push_back(e.b);
        m.edgeL0.push_back(static_cast<float>(e.l0));
    }
    m.colStart = slColStart_;

    // self collision exclusions, by vertex
    std::vector<std::pair<int, int>> nc;
    nc.reserve(noCollide_.size());
    for (std::uint64_t k : noCollide_) nc.push_back({static_cast<int>(k >> 32), static_cast<int>(k & 0xffffffffu)});
    std::sort(nc.begin(), nc.end());
    m.ncStart.assign(N + 1, 0);
    for (const auto& p : nc) ++m.ncStart[p.first + 1];
    for (int i = 0; i < N; ++i) m.ncStart[i + 1] += m.ncStart[i];
    m.ncTri.resize(nc.size());
    for (size_t k = 0; k < nc.size(); ++k) m.ncTri[k] = nc[k].second;
    m.hashCell = static_cast<float>(hash_.cell);
    m.hashTable = static_cast<int>(hash_.table);
    return m;
}

void ClothSolver::gpuState(std::vector<Vec3>& x, std::vector<Vec3>& v, std::vector<Real>& force, std::vector<Real>& phi,
                           std::vector<Vec3>& normal) const {
    const size_t N = x_.size();
    x = x_;
    v = v_;
    force = rigidForce_.size() == N ? rigidForce_ : std::vector<Real>(N, 0.0);
    phi = contactPhi_.size() == N ? contactPhi_ : std::vector<Real>(N, 1e30);
    normal = contactN_.size() == N ? contactN_ : std::vector<Vec3>(N, Vec3(0, 1, 0));
}

void ClothSolver::gpuEndFrame(const std::vector<Vec3>& x, const std::vector<Vec3>& v, const std::vector<Real>& force,
                              const std::vector<Real>& phi, const std::vector<Vec3>& normal) {
    x_ = x;
    v_ = v;
    xPrev_ = x;
    rigidForce_ = force;
    contactPhi_ = phi;
    contactN_ = normal;
    const int S = std::max(1, p_.substeps);
    substepsDone_ += S;
    newtonIterations_ += S;
    ++frame_;
}

Real ClothSolver::pointSeamScaleNow() const {
    return p_.seamRampFrames > 0 ? std::min(Real(1), std::max(Real(0.02), (frame_ + 1.0) / p_.seamRampFrames)) : Real(1);
}

Real ClothSolver::seamScaleNow() const {
    return pointSeamScaleNow() * std::max(Real(0), p_.seamScale);
}

}  // namespace fl
