#pragma once
// Small sparse Cholesky (A = L L^T) for symmetric positive definite systems with a fixed
// sparsity pattern: analyse once, refactor numerically whenever values change.
// Numeric factorisation is left-looking supernodal (dense blocks for columns with identical structure);
// the original up-looking algorithm (T. Davis, CSparse) is kept as factorReference() for cross-checking.
#include <vector>

namespace fl {

class SparseCholesky {
public:
    // Full symmetric pattern in CSR (both triangles), `perm` maps new index -> original index
    // (a fill-reducing ordering). Values are later supplied in the same CSR entry order.
    void analyze(int n, const std::vector<int>& rowStart, const std::vector<int>& col, const std::vector<int>& perm);
    // Numeric factorisation. Returns false if the matrix is not positive definite.
    bool factor(const std::vector<double>& valFull);
    bool factorReference(const std::vector<double>& valFull);  // simple up-looking version (slow)
    int supernodeCount() const { return nsuper_; }
    // Solve A x = b (original ordering).
    void solve(const std::vector<double>& b, std::vector<double>& x) const;
    long long nnzL() const { return static_cast<long long>(Li_.size()); }
    // Floating-point operations of one numeric factorisation: sum over columns of c^2 for column count c (approx.)
    double flops() const {
        double f = 0;
        for (int j = 0; j < n_; ++j) { const double c = Lp_[j + 1] - Lp_[j]; f += c * c; }
        return f;
    }
    int size() const { return n_; }

private:
    int ereach(int k, std::vector<int>& s) const;
    void updateBlock(const double* L, int ld, int wd, int m1, int m2, const int* R, double* B, int nr, int f);
    std::vector<int> rowPatStart_, rowPat_;  // cached nonzero pattern of every row of L (topological order)
    int n_ = 0;
    std::vector<int> perm_, iperm_;
    std::vector<int> Ap_, Ai_, slot_, parent_, Lp_, Li_;
    std::vector<double> Ax_, Lx_;
    mutable std::vector<int> mark_, stack_;
    mutable int stamp_ = 0;
    mutable std::vector<double> work_, y_;
    std::vector<int> next_;
    // supernodal data
    int nsuper_ = 0;
    std::vector<int> sFirst_, snodeOf_, head_, link_, nextPos_, relmap_;
    std::vector<long long> sOff_;
    std::vector<double> Sx_, cbuf_;
    std::vector<int> LCp_, LCi_, LCsrc_;  // lower-triangular CSC of A: column -> rows, index into Ax_
};

// Geometric nested-dissection ordering of a graph (block-level). `rowStart/col` is the adjacency
// (self entries allowed), `xyz` are vertex coordinates used to pick separators. Returns new->old.
std::vector<int> nestedDissectionOrder(int n, const std::vector<int>& rowStart, const std::vector<int>& col,
                                       const std::vector<double>& xyz);

}  // namespace fl
