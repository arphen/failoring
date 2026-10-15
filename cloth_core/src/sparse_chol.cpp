#include "failoring/sparse_chol.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace fl {

void SparseCholesky::analyze(int n, const std::vector<int>& rowStart, const std::vector<int>& col, const std::vector<int>& perm) {
    n_ = n;
    perm_ = perm;
    iperm_.assign(n, 0);
    for (int i = 0; i < n; ++i) iperm_[perm_[i]] = i;

    // permuted upper-triangular CSC pattern (entry (r,c) -> column iperm[c], row iperm[r], kept if row <= column)
    std::vector<int> cnt(n + 1, 0);
    for (int r = 0; r < n; ++r)
        for (int e = rowStart[r]; e < rowStart[r + 1]; ++e) {
            const int pr = iperm_[r], pc = iperm_[col[e]];
            if (pr <= pc) cnt[pc + 1]++;
        }
    Ap_.assign(n + 1, 0);
    for (int k = 0; k < n; ++k) Ap_[k + 1] = Ap_[k] + cnt[k + 1];
    Ai_.assign(Ap_[n], 0);
    slot_.assign(col.size(), -1);
    std::vector<int> fill(Ap_.begin(), Ap_.end() - 1);
    for (int r = 0; r < n; ++r)
        for (int e = rowStart[r]; e < rowStart[r + 1]; ++e) {
            const int pr = iperm_[r], pc = iperm_[col[e]];
            if (pr <= pc) {
                const int p = fill[pc]++;
                Ai_[p] = pr;
                slot_[e] = p;
            }
        }
    Ax_.assign(Ap_[n], 0.0);

    // elimination tree
    parent_.assign(n, -1);
    std::vector<int> ancestor(n, -1);
    for (int k = 0; k < n; ++k)
        for (int p = Ap_[k]; p < Ap_[k + 1]; ++p) {
            int i = Ai_[p];
            while (i != -1 && i < k) {
                const int inext = ancestor[i];
                ancestor[i] = k;
                if (inext == -1) parent_[i] = k;
                i = inext;
            }
        }

    // symbolic: column counts from the row patterns
    mark_.assign(n, 0);
    stamp_ = 0;
    std::vector<int> s(n), colCount(n, 0);
    rowPatStart_.assign(n + 1, 0);
    rowPat_.clear();
    stack_.reserve(n);
    for (int k = 0; k < n; ++k) {
        const int top = ereach(k, s);
        for (int t = top; t < n; ++t) {
            colCount[s[t]]++;
            rowPat_.push_back(s[t]);
        }
        colCount[k]++;
        rowPatStart_[k + 1] = static_cast<int>(rowPat_.size());
    }
    Lp_.assign(n + 1, 0);
    for (int k = 0; k < n; ++k) Lp_[k + 1] = Lp_[k] + colCount[k];
    Li_.assign(Lp_[n], 0);
    Lx_.assign(Lp_[n], 0.0);
    {   // full pattern of L, rows ascending in every column (diagonal first)
        std::vector<int> pos(Lp_.begin(), Lp_.end() - 1);
        for (int k = 0; k < n; ++k) {
            for (int t = rowPatStart_[k]; t < rowPatStart_[k + 1]; ++t) Li_[pos[rowPat_[t]]++] = k;
            Li_[pos[k]++] = k;
        }
        // NOTE: rows of column i arrive in increasing k, but the diagonal of column i is inserted at step k=i,
        // before any row k>i, so it is first. Rows of earlier steps never precede it (rowPat has i<k only).
    }
    {   // fundamental supernodes: column j joins j-1 if parent(j-1)==j and structures nest exactly
        sFirst_.clear();
        snodeOf_.assign(n, 0);
        for (int j = 0; j < n; ++j) {
            const bool join = j > 0 && parent_[j - 1] == j && colCount[j - 1] == colCount[j] + 1 && (j - sFirst_.back()) < 96;
            if (!join) sFirst_.push_back(j);
            snodeOf_[j] = static_cast<int>(sFirst_.size()) - 1;
        }
        nsuper_ = static_cast<int>(sFirst_.size());
        sFirst_.push_back(n);
        sOff_.assign(nsuper_ + 1, 0);
        for (int s = 0; s < nsuper_; ++s) {
            const int f = sFirst_[s], w = sFirst_[s + 1] - f, nr = Lp_[f + 1] - Lp_[f];
            sOff_[s + 1] = sOff_[s] + static_cast<long long>(nr) * w;
        }
        Sx_.assign(static_cast<size_t>(sOff_[nsuper_]), 0.0);
        head_.assign(nsuper_, -1);
        link_.assign(nsuper_, -1);
        nextPos_.assign(nsuper_, 0);
        relmap_.assign(n, 0);
        // lower CSC of A (entry (r,k) with r<=k in the upper CSC is lower entry (k,r) of column r)
        std::vector<int> cc(n + 1, 0);
        for (int k = 0; k < n; ++k)
            for (int p = Ap_[k]; p < Ap_[k + 1]; ++p) cc[Ai_[p] + 1]++;
        LCp_.assign(n + 1, 0);
        for (int j = 0; j < n; ++j) LCp_[j + 1] = LCp_[j] + cc[j + 1];
        LCi_.assign(LCp_[n], 0);
        LCsrc_.assign(LCp_[n], 0);
        std::vector<int> fl(LCp_.begin(), LCp_.end() - 1);
        for (int k = 0; k < n; ++k)
            for (int p = Ap_[k]; p < Ap_[k + 1]; ++p) {
                const int q = fl[Ai_[p]]++;
                LCi_[q] = k;
                LCsrc_[q] = p;
            }
        size_t maxC = 1;
        for (int s = 0; s < nsuper_; ++s) {
            const size_t nr = Lp_[sFirst_[s] + 1] - Lp_[sFirst_[s]], w = sFirst_[s + 1] - sFirst_[s];
            maxC = std::max(maxC, nr * w);
        }
        cbuf_.assign(maxC, 0.0);
    }
    work_.assign(n, 0.0);
    y_.assign(n, 0.0);
    next_.assign(n, 0);
}

// Pattern of row k of L (columns i < k), in topological order, stored in s[top..n-1].
int SparseCholesky::ereach(int k, std::vector<int>& s) const {
    int top = n_;
    ++stamp_;
    mark_[k] = stamp_;
    std::vector<int>& stack = stack_;
    for (int p = Ap_[k]; p < Ap_[k + 1]; ++p) {
        int i = Ai_[p];
        if (i > k) continue;
        int len = 0;
        stack.clear();
        for (; mark_[i] != stamp_; i = parent_[i]) {
            stack.push_back(i);
            ++len;
            mark_[i] = stamp_;
        }
        while (len > 0) s[--top] = stack[--len];
    }
    return top;
}

// B[:, R[jb]-f] -= L(ia,:) . L(jb,:) for jb < m1, ia >= jb (rows of the descendant panel L, leading dimension ld,
// width wd): the supernodal update, in 4x4 register tiles accumulated over the panel width and scattered straight
// into the target panel through relmap_ (no intermediate buffer).
void SparseCholesky::updateBlock(const double* L, int ld, int wd, int m1, int m2, const int* R, double* B, int nr, int f) {
    const int* rm = relmap_.data();
    for (int jb = 0; jb < m1; jb += 4) {
        const int nb = std::min(4, m1 - jb);
        double* col[4];
        for (int jj = 0; jj < 4; ++jj) col[jj] = B + static_cast<size_t>(R[jb + std::min(jj, nb - 1)] - f) * nr;
        for (int ia = jb; ia < m2; ia += 4) {
            const int na = std::min(4, m2 - ia);
            double acc[4][4] = {{0}};
            if (na == 4 && nb == 4) {
                for (int k = 0; k < wd; ++k) {
                    const double* a = L + static_cast<size_t>(k) * ld;
                    const double a0 = a[ia], a1 = a[ia + 1], a2 = a[ia + 2], a3 = a[ia + 3];
                    const double b0 = a[jb], b1 = a[jb + 1], b2 = a[jb + 2], b3 = a[jb + 3];
                    acc[0][0] += a0 * b0; acc[0][1] += a1 * b0; acc[0][2] += a2 * b0; acc[0][3] += a3 * b0;
                    acc[1][0] += a0 * b1; acc[1][1] += a1 * b1; acc[1][2] += a2 * b1; acc[1][3] += a3 * b1;
                    acc[2][0] += a0 * b2; acc[2][1] += a1 * b2; acc[2][2] += a2 * b2; acc[2][3] += a3 * b2;
                    acc[3][0] += a0 * b3; acc[3][1] += a1 * b3; acc[3][2] += a2 * b3; acc[3][3] += a3 * b3;
                }
            } else if (na == 4) {  // a short column block: the 4 rows still vectorise
                for (int k = 0; k < wd; ++k) {
                    const double* a = L + static_cast<size_t>(k) * ld + ia;
                    for (int jj = 0; jj < nb; ++jj) {
                        const double bj = L[static_cast<size_t>(k) * ld + jb + jj];
                        acc[jj][0] += a[0] * bj; acc[jj][1] += a[1] * bj; acc[jj][2] += a[2] * bj; acc[jj][3] += a[3] * bj;
                    }
                }
            } else {
                for (int k = 0; k < wd; ++k) {
                    const double* a = L + static_cast<size_t>(k) * ld;
                    for (int jj = 0; jj < nb; ++jj)
                        for (int ii = 0; ii < na; ++ii) acc[jj][ii] += a[ia + ii] * a[jb + jj];
                }
            }
            for (int jj = 0; jj < nb; ++jj)
                for (int ii = 0; ii < na; ++ii)
                    if (ia + ii >= jb + jj) col[jj][rm[R[ia + ii]]] -= acc[jj][ii];
        }
    }
}

bool SparseCholesky::factor(const std::vector<double>& val) {
    for (size_t e = 0; e < slot_.size(); ++e)
        if (slot_[e] >= 0) Ax_[slot_[e]] = val[e];
    std::fill(Sx_.begin(), Sx_.end(), 0.0);
    std::fill(head_.begin(), head_.end(), -1);
    for (int s = 0; s < nsuper_; ++s) {
        const int f = sFirst_[s], w = sFirst_[s + 1] - f;
        const int* R = &Li_[Lp_[f]];
        const int nr = Lp_[f + 1] - Lp_[f];
        double* B = &Sx_[sOff_[s]];
        for (int i = 0; i < nr; ++i) relmap_[R[i]] = i;
        for (int j = f; j < f + w; ++j)
            for (int q = LCp_[j]; q < LCp_[j + 1]; ++q) B[relmap_[LCi_[q]] + static_cast<size_t>(j - f) * nr] += Ax_[LCsrc_[q]];
        // updates from descendant supernodes whose next unprocessed row lies in this supernode
        int d = head_[s];
        while (d != -1) {
            const int dnext = link_[d];
            const int fd = sFirst_[d], wd = sFirst_[d + 1] - fd;
            const int* Rd = &Li_[Lp_[fd]];
            const int nrd = Lp_[fd + 1] - Lp_[fd];
            const double* Ld = &Sx_[sOff_[d]];
            const int p0 = nextPos_[d];
            int p1 = p0;
            while (p1 < nrd && Rd[p1] < f + w) ++p1;
            const int m1 = p1 - p0, m2 = nrd - p0;
            updateBlock(Ld + p0, nrd, wd, m1, m2, Rd + p0, B, nr, f);
            nextPos_[d] = p1;
            if (p1 < nrd) {
                const int t = snodeOf_[Rd[p1]];
                link_[d] = head_[t];
                head_[t] = d;
            }
            d = dnext;
        }
        // dense factorisation of the panel (right-looking inside the supernode)
        for (int c = 0; c < w; ++c) {
            double* colc = B + static_cast<size_t>(c) * nr;
            const double dg = colc[c];
            if (!(dg > 0)) return false;
            const double sq = std::sqrt(dg), inv = 1.0 / sq;
            colc[c] = sq;
            for (int i = c + 1; i < nr; ++i) colc[i] *= inv;
            for (int c2 = c + 1; c2 < w; ++c2) {
                double* col2 = B + static_cast<size_t>(c2) * nr;
                const double l = colc[c2];
                for (int i = c2; i < nr; ++i) col2[i] -= colc[i] * l;
            }
        }
        // copy into the simplicial layout used by solve()
        for (int c = 0; c < w; ++c) {
            const double* colc = B + static_cast<size_t>(c) * nr;
            double* dst = &Lx_[Lp_[f + c]];
            for (int t = 0; t < nr - c; ++t) dst[t] = colc[c + t];
        }
        nextPos_[s] = w;
        if (w < nr) {
            const int t = snodeOf_[R[w]];
            link_[s] = head_[t];
            head_[t] = s;
        }
    }
    return true;
}

bool SparseCholesky::factorReference(const std::vector<double>& val) {
    for (size_t e = 0; e < slot_.size(); ++e)
        if (slot_[e] >= 0) Ax_[slot_[e]] = val[e];
    for (int k = 0; k < n_; ++k) next_[k] = Lp_[k];
    std::vector<double>& x = work_;
    std::fill(x.begin(), x.end(), 0.0);
    for (int k = 0; k < n_; ++k) {
        for (int p = Ap_[k]; p < Ap_[k + 1]; ++p) x[Ai_[p]] = Ax_[p];
        double d = x[k];
        x[k] = 0;
        for (int t = rowPatStart_[k]; t < rowPatStart_[k + 1]; ++t) {
            const int i = rowPat_[t];
            const double lki = x[i] / Lx_[Lp_[i]];
            x[i] = 0;
            for (int p = Lp_[i] + 1; p < next_[i]; ++p) x[Li_[p]] -= Lx_[p] * lki;
            d -= lki * lki;
            const int p = next_[i]++;
            Li_[p] = k;
            Lx_[p] = lki;
        }
        if (!(d > 0)) return false;
        const int p = next_[k]++;
        Li_[p] = k;
        Lx_[p] = std::sqrt(d);
    }
    return true;
}

void SparseCholesky::solve(const std::vector<double>& b, std::vector<double>& x) const {
    std::vector<double>& y = y_;
    for (int i = 0; i < n_; ++i) y[i] = b[perm_[i]];
    for (int j = 0; j < n_; ++j) {
        y[j] /= Lx_[Lp_[j]];
        for (int p = Lp_[j] + 1; p < Lp_[j + 1]; ++p) y[Li_[p]] -= Lx_[p] * y[j];
    }
    for (int j = n_ - 1; j >= 0; --j) {
        for (int p = Lp_[j] + 1; p < Lp_[j + 1]; ++p) y[j] -= Lx_[p] * y[Li_[p]];
        y[j] /= Lx_[Lp_[j]];
    }
    x.assign(n_, 0.0);
    for (int i = 0; i < n_; ++i) x[perm_[i]] = y[i];
}

// ---------------------------------------------------------------------------
namespace {

struct NdCtx {
    const std::vector<int>& rs;
    const std::vector<int>& col;
    const std::vector<double>& xyz;
    std::vector<int> side;  // 0 = not in current set, 1 = left, 2 = right
    std::vector<int> order;
};

void ndRecurse(NdCtx& c, std::vector<int> verts) {
    if (verts.size() <= 24) {
        std::sort(verts.begin(), verts.end());
        for (int v : verts) c.order.push_back(v);
        return;
    }
    double lo[3] = {1e300, 1e300, 1e300}, hi[3] = {-1e300, -1e300, -1e300};
    for (int v : verts)
        for (int a = 0; a < 3; ++a) {
            lo[a] = std::min(lo[a], c.xyz[3 * v + a]);
            hi[a] = std::max(hi[a], c.xyz[3 * v + a]);
        }
    int axis = 0;
    for (int a = 1; a < 3; ++a)
        if (hi[a] - lo[a] > hi[axis] - lo[axis]) axis = a;
    std::sort(verts.begin(), verts.end(), [&](int p, int q) { return c.xyz[3 * p + axis] < c.xyz[3 * q + axis]; });
    const size_t mid = verts.size() / 2;
    for (size_t i = 0; i < verts.size(); ++i) c.side[verts[i]] = i < mid ? 1 : 2;
    std::vector<int> left, right, sep;
    for (size_t i = 0; i < mid; ++i) {  // separator: left vertices touching the right half
        const int v = verts[i];
        bool touches = false;
        for (int e = c.rs[v]; e < c.rs[v + 1] && !touches; ++e)
            if (c.col[e] != v && c.side[c.col[e]] == 2) touches = true;
        (touches ? sep : left).push_back(v);
    }
    for (size_t i = mid; i < verts.size(); ++i) right.push_back(verts[i]);
    for (int v : verts) c.side[v] = 0;
    ndRecurse(c, std::move(left));
    ndRecurse(c, std::move(right));
    for (int v : sep) c.order.push_back(v);
}

}  // namespace

std::vector<int> nestedDissectionOrder(int n, const std::vector<int>& rowStart, const std::vector<int>& col, const std::vector<double>& xyz) {
    NdCtx c{rowStart, col, xyz, std::vector<int>(n, 0), {}};
    c.order.reserve(n);
    std::vector<int> all(n);
    std::iota(all.begin(), all.end(), 0);
    ndRecurse(c, std::move(all));
    return c.order;
}

}  // namespace fl
