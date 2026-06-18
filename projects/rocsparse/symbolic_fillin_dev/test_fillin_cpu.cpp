// ============================================================================
//  CPU validation harness for the symbolic fill-in algorithm.
//
//  Proves three things, with no GPU required:
//   (1) The Jacobi fixed-point fill (the logic ported to the GPU kernel)
//       reproduces the EXACT fill computed by a classic sequential symbolic
//       elimination (ground truth), for both symmetric and general matrices.
//   (2) The numeric scatter (copy A into the fill pattern with explicit zeros)
//       is correct.
//   (3) Running ILU(0)/IC(0)-style elimination restricted to the *complete*
//       fill pattern reconstructs A exactly (L*U == A), i.e. "incomplete on the
//       full pattern == exact complete factorization".
//
//  Build & run:
//     g++ -O2 -std=c++17 test_fillin_cpu.cpp -o test_fillin_cpu && ./test_fillin_cpu
// ============================================================================

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <random>
#include <set>
#include <vector>

#include "../library/src/precond/rocsparse_symbolic_fillin_host.hpp"

using std::size_t;

// ---------------------------------------------------------------------------
// Simple 0-based CSR (pattern + optional values).
// ---------------------------------------------------------------------------
struct Csr
{
    int                 n = 0;
    std::vector<int>    row_ptr; // size n+1
    std::vector<int>    col_ind; // size nnz
    std::vector<double> val;     // size nnz (optional)
    int                 nnz() const { return row_ptr.empty() ? 0 : row_ptr[n]; }
};

static Csr from_rows(const std::vector<std::set<int>>& rows)
{
    Csr A;
    A.n = (int)rows.size();
    A.row_ptr.assign(A.n + 1, 0);
    for(int i = 0; i < A.n; ++i)
        A.row_ptr[i + 1] = A.row_ptr[i] + (int)rows[i].size();
    A.col_ind.reserve(A.row_ptr[A.n]);
    for(int i = 0; i < A.n; ++i)
        for(int c : rows[i])
            A.col_ind.push_back(c);
    return A;
}

static std::vector<std::set<int>> to_rows(const Csr& A)
{
    std::vector<std::set<int>> rows(A.n);
    for(int i = 0; i < A.n; ++i)
        for(int p = A.row_ptr[i]; p < A.row_ptr[i + 1]; ++p)
            rows[i].insert(A.col_ind[p]);
    return rows;
}

// ===========================================================================
//  Ground truth: exact symbolic factorization fill, natural order, no pivot.
//  Classic elimination: for pivot k, every row i>k that has column k absorbs
//  the entries j>k of row k.  Produces the complete L+U pattern.
// ===========================================================================
static std::vector<std::set<int>> reference_fill(const Csr& A)
{
    auto F = to_rows(A);
    for(int i = 0; i < A.n; ++i)
        F[i].insert(i); // ensure diagonal
    for(int k = 0; k < A.n; ++k)
    {
        for(int i = k + 1; i < A.n; ++i)
        {
            if(F[i].count(k))
            {
                for(int j : F[k])
                    if(j > k)
                        F[i].insert(j);
            }
        }
    }
    return F;
}

// ===========================================================================
//  Jacobi fixed point -- THE logic that the GPU kernel implements.
//  Per row i: S_i = row(i) U  U_{k in row(i),k<i} { j in row(k) : j>k }.
//  Reads old F, writes new F, iterate until nnz stable.
// ===========================================================================
static std::vector<std::set<int>> jacobi_fill(const Csr& A, int* iters_out = nullptr)
{
    auto F = to_rows(A);
    for(int i = 0; i < A.n; ++i)
        F[i].insert(i);

    auto total_nnz = [](const std::vector<std::set<int>>& R) {
        size_t s = 0;
        for(auto& r : R)
            s += r.size();
        return s;
    };

    int    iters = 0;
    size_t prev  = total_nnz(F);
    while(true)
    {
        ++iters;
        auto next = F; // start from current (old F used for reads below)
        for(int i = 0; i < A.n; ++i)
        {
            for(int k : F[i])
            {
                if(k < i)
                {
                    for(int j : F[k])
                        if(j > k)
                            next[i].insert(j);
                }
            }
        }
        size_t now = total_nnz(next);
        F          = std::move(next);
        if(now == prev)
            break;
        prev = now;
    }
    if(iters_out)
        *iters_out = iters;
    return F;
}

// ===========================================================================
//  Numeric scatter: build A_F with the fill pattern F, A values where present,
//  explicit 0 elsewhere.  Returned as a dense n x n (simple & bulletproof for
//  the test); the real GPU code scatters into CSR.
// ===========================================================================
static std::vector<double>
    scatter_dense(const Csr& A, const std::vector<std::set<int>>& F, std::vector<char>& inF)
{
    const int           n = A.n;
    std::vector<double> M(n * (size_t)n, 0.0);
    inF.assign(n * (size_t)n, 0);
    for(int i = 0; i < n; ++i)
        for(int j : F[i])
            inF[i * (size_t)n + j] = 1;
    for(int i = 0; i < n; ++i)
        for(int p = A.row_ptr[i]; p < A.row_ptr[i + 1]; ++p)
            M[i * (size_t)n + A.col_ind[p]] = A.val[p];
    return M;
}

// ===========================================================================
//  Unpivoted LU restricted to the fill pattern (== ILU(0) on the full pattern).
//  If F is the complete fill, no required position is missing -> exact LU.
//  Returns L (unit lower) and U (upper) packed in M in place.
// ===========================================================================
static void ilu0_on_pattern(int n, std::vector<double>& M, const std::vector<char>& inF)
{
    auto at  = [&](int i, int j) -> double& { return M[i * (size_t)n + j]; };
    auto has = [&](int i, int j) { return inF[i * (size_t)n + j] != 0; };

    for(int k = 0; k < n; ++k)
    {
        const double piv = at(k, k);
        for(int i = k + 1; i < n; ++i)
        {
            if(!has(i, k))
                continue;
            const double lik = at(i, k) / piv;
            at(i, k)         = lik;
            for(int j = k + 1; j < n; ++j)
            {
                if(has(k, j) && has(i, j))
                    at(i, j) -= lik * at(k, j);
            }
        }
    }
}

// ===========================================================================
//  IC(0) on the complete pattern == exact Cholesky.  Left-looking, lower
//  triangle only; every needed k is present because F is complete.
//  Returns L (lower, incl diagonal) dense; 0 outside the pattern.
// ===========================================================================
static std::vector<double>
    ic0_on_pattern(int n, const std::vector<double>& M, const std::vector<char>& inF)
{
    auto a   = [&](int i, int j) { return M[i * (size_t)n + j]; };
    auto has = [&](int i, int j) { return inF[i * (size_t)n + j] != 0; };
    std::vector<double> L(n * (size_t)n, 0.0);
    auto                Lr = [&](int i, int j) -> double& { return L[i * (size_t)n + j]; };

    for(int j = 0; j < n; ++j)
    {
        double d = a(j, j);
        for(int k = 0; k < j; ++k)
            if(has(j, k))
                d -= Lr(j, k) * Lr(j, k);
        Lr(j, j) = std::sqrt(d);
        for(int i = j + 1; i < n; ++i)
        {
            if(!has(i, j))
                continue;
            double s = a(i, j);
            for(int k = 0; k < j; ++k)
                if(has(i, k) && has(j, k))
                    s -= Lr(i, k) * Lr(j, k);
            Lr(i, j) = s / Lr(j, j);
        }
    }
    return L;
}

// ===========================================================================
//  ILDLT(0) on the complete pattern == exact L*D*L^T.  Unit lower L + diag D.
// ===========================================================================
static std::vector<double> ildlt0_on_pattern(int                       n,
                                             const std::vector<double>& M,
                                             const std::vector<char>&   inF,
                                             std::vector<double>&       D)
{
    auto a   = [&](int i, int j) { return M[i * (size_t)n + j]; };
    auto has = [&](int i, int j) { return inF[i * (size_t)n + j] != 0; };
    std::vector<double> L(n * (size_t)n, 0.0);
    auto                Lr = [&](int i, int j) -> double& { return L[i * (size_t)n + j]; };
    D.assign(n, 0.0);

    for(int j = 0; j < n; ++j)
    {
        double d = a(j, j);
        for(int k = 0; k < j; ++k)
            if(has(j, k))
                d -= Lr(j, k) * Lr(j, k) * D[k];
        D[j]     = d;
        Lr(j, j) = 1.0;
        for(int i = j + 1; i < n; ++i)
        {
            if(!has(i, j))
                continue;
            double s = a(i, j);
            for(int k = 0; k < j; ++k)
                if(has(i, k) && has(j, k))
                    s -= Lr(i, k) * D[k] * Lr(j, k);
            Lr(i, j) = s / D[j];
        }
    }
    return L;
}

// max | (L*L^T)_{ij} - A_{ij} |
static double residual_LLt_minus_A(int n, const std::vector<double>& L, const Csr& A)
{
    std::vector<double> Ad(n * (size_t)n, 0.0);
    for(int i = 0; i < n; ++i)
        for(int p = A.row_ptr[i]; p < A.row_ptr[i + 1]; ++p)
            Ad[i * (size_t)n + A.col_ind[p]] = A.val[p];
    auto   l  = [&](int i, int j) { return L[i * (size_t)n + j]; };
    double mx = 0.0;
    for(int i = 0; i < n; ++i)
        for(int j = 0; j < n; ++j)
        {
            double s = 0.0;
            for(int k = 0; k <= std::min(i, j); ++k)
                s += l(i, k) * l(j, k);
            mx = std::max(mx, std::fabs(s - Ad[i * (size_t)n + j]));
        }
    return mx;
}

// max | (L*D*L^T)_{ij} - A_{ij} |
static double residual_LDLt_minus_A(int                        n,
                                    const std::vector<double>& L,
                                    const std::vector<double>& D,
                                    const Csr&                 A)
{
    std::vector<double> Ad(n * (size_t)n, 0.0);
    for(int i = 0; i < n; ++i)
        for(int p = A.row_ptr[i]; p < A.row_ptr[i + 1]; ++p)
            Ad[i * (size_t)n + A.col_ind[p]] = A.val[p];
    auto   l  = [&](int i, int j) { return L[i * (size_t)n + j]; };
    double mx = 0.0;
    for(int i = 0; i < n; ++i)
        for(int j = 0; j < n; ++j)
        {
            double s = 0.0;
            for(int k = 0; k <= std::min(i, j); ++k)
                s += l(i, k) * D[k] * l(j, k);
            mx = std::max(mx, std::fabs(s - Ad[i * (size_t)n + j]));
        }
    return mx;
}

// max | (L*U)_{ij} - A_{ij} |  over all i,j  (dense check)
static double residual_LU_minus_A(int n, const std::vector<double>& LU, const Csr& A)
{
    // dense A
    std::vector<double> Ad(n * (size_t)n, 0.0);
    for(int i = 0; i < n; ++i)
        for(int p = A.row_ptr[i]; p < A.row_ptr[i + 1]; ++p)
            Ad[i * (size_t)n + A.col_ind[p]] = A.val[p];

    auto lu = [&](int i, int j) { return LU[i * (size_t)n + j]; };
    double mx = 0.0;
    for(int i = 0; i < n; ++i)
        for(int j = 0; j < n; ++j)
        {
            double s = 0.0;
            for(int t = 0; t <= std::min(i, j); ++t)
            {
                const double lit = (t == i) ? 1.0 : lu(i, t); // unit diagonal of L
                s += lit * lu(t, j);
            }
            mx = std::max(mx, std::fabs(s - Ad[i * (size_t)n + j]));
        }
    return mx;
}

// ===========================================================================
//  Test matrix generators (0-based CSR, with values, diagonally dominant).
// ===========================================================================
static Csr gen_symmetric(int n, double density, unsigned seed)
{
    std::mt19937                           rng(seed);
    std::uniform_real_distribution<double> u(0.0, 1.0);
    std::vector<std::set<int>>             P(n);
    for(int i = 0; i < n; ++i)
        P[i].insert(i);
    for(int i = 0; i < n; ++i)
        for(int j = i + 1; j < n; ++j)
            if(u(rng) < density)
            {
                P[i].insert(j);
                P[j].insert(i);
            }
    Csr A = from_rows(P);
    A.val.assign(A.nnz(), 0.0);

    // Symmetric values: one weight per unordered pair, used for (i,j) AND (j,i).
    std::map<std::pair<int, int>, double> w;
    for(int i = 0; i < n; ++i)
        for(int p = A.row_ptr[i]; p < A.row_ptr[i + 1]; ++p)
        {
            int j = A.col_ind[p];
            if(i < j)
                w[{i, j}] = u(rng) - 0.5;
        }
    for(int i = 0; i < n; ++i)
    {
        double off = 0.0;
        for(int p = A.row_ptr[i]; p < A.row_ptr[i + 1]; ++p)
        {
            int j = A.col_ind[p];
            if(j != i)
            {
                double v = (i < j) ? w[{i, j}] : w[{j, i}];
                A.val[p] = v;
                off += std::fabs(v);
            }
        }
        for(int p = A.row_ptr[i]; p < A.row_ptr[i + 1]; ++p)
            if(A.col_ind[p] == i)
                A.val[p] = off + 1.0; // SPD: diagonally dominant + symmetric
    }
    return A;
}

static Csr gen_general(int n, double density, unsigned seed)
{
    std::mt19937                           rng(seed);
    std::uniform_real_distribution<double> u(0.0, 1.0);
    std::vector<std::set<int>>             P(n);
    for(int i = 0; i < n; ++i)
        P[i].insert(i);
    for(int i = 0; i < n; ++i)
        for(int j = 0; j < n; ++j)
            if(i != j && u(rng) < density)
                P[i].insert(j);
    Csr A   = from_rows(P);
    A.val.assign(A.nnz(), 0.0);
    for(int i = 0; i < n; ++i)
    {
        double off = 0.0;
        for(int p = A.row_ptr[i]; p < A.row_ptr[i + 1]; ++p)
            if(A.col_ind[p] != i)
            {
                double v = u(rng) - 0.5;
                A.val[p] = v;
                off += std::fabs(v);
            }
        for(int p = A.row_ptr[i]; p < A.row_ptr[i + 1]; ++p)
            if(A.col_ind[p] == i)
                A.val[p] = off + 1.0;
    }
    return A;
}

static bool same_pattern(const std::vector<std::set<int>>& X, const std::vector<std::set<int>>& Y)
{
    if(X.size() != Y.size())
        return false;
    for(size_t i = 0; i < X.size(); ++i)
        if(X[i] != Y[i])
            return false;
    return true;
}

static size_t count_nnz(const std::vector<std::set<int>>& R)
{
    size_t s = 0;
    for(auto& r : R)
        s += r.size();
    return s;
}

// ---------------------------------------------------------------------------
static int run_case(const char* name, const Csr& A, bool is_sym)
{
    int  iters = 0;
    auto ref   = reference_fill(A);
    auto jac   = jacobi_fill(A, &iters);

    // production CPU routine (etree+ereach for sym, up-looking for general)
    std::vector<int> hrp, hci;
    rocsparse::symbolic_fillin_host<int, int>(is_sym, A.n, A.row_ptr.data(), A.col_ind.data(), 0,
                                              hrp, hci);
    Csr H;
    H.n = A.n;
    H.row_ptr = hrp;
    H.col_ind = hci;
    auto host_rows = to_rows(H);

    const bool pat_ok  = same_pattern(ref, jac);
    const bool host_ok = same_pattern(ref, host_rows);

    // ---- ILU(0) on complete pattern (all matrices) ----
    std::vector<char> inF;
    auto              Mlu = scatter_dense(A, ref, inF);
    ilu0_on_pattern(A.n, Mlu, inF);
    const double res_lu = residual_LU_minus_A(A.n, Mlu, A);

    double     res_ic = 0.0, res_ld = 0.0;
    const bool lu_ok = res_lu < 1e-9;
    bool       ic_ok = true, ld_ok = true;

    if(is_sym)
    {
        // ---- IC(0): exact Cholesky on complete pattern ----
        auto Mic = scatter_dense(A, ref, inF);
        auto Lc  = ic0_on_pattern(A.n, Mic, inF);
        res_ic   = residual_LLt_minus_A(A.n, Lc, A);
        ic_ok    = res_ic < 1e-9;

        // ---- ILDLT(0): exact L*D*L^T on complete pattern ----
        auto                Mld = scatter_dense(A, ref, inF);
        std::vector<double> D;
        auto                Ld = ildlt0_on_pattern(A.n, Mld, inF, D);
        res_ld                 = residual_LDLt_minus_A(A.n, Ld, D, A);
        ld_ok                  = res_ld < 1e-9;
    }

    printf("[%-10s] n=%4d nnz(A)=%6d nnz(fill)=%6zu it=%2d  jac=%s host=%s  "
           "ILU:%.1e%s  IC:%s  ILDLT:%s\n",
           name, A.n, A.nnz(), count_nnz(ref), iters, pat_ok ? "OK" : "FAIL",
           host_ok ? "OK" : "FAIL", res_lu, lu_ok ? "" : "!",
           is_sym ? (ic_ok ? "OK" : "FAIL") : "-  ",
           is_sym ? (ld_ok ? "OK" : "FAIL") : "-  ");

    return (pat_ok && host_ok && lu_ok && ic_ok && ld_ok) ? 0 : 1;
}

int main()
{
    int fails = 0;

    // symmetric (tests ILU0 + IC0 + ILDLT0 on the complete pattern)
    fails += run_case("sym-small", gen_symmetric(8, 0.30, 1), true);
    fails += run_case("sym-med", gen_symmetric(40, 0.15, 2), true);
    fails += run_case("sym-dense", gen_symmetric(30, 0.45, 3), true);
    fails += run_case("sym-sparse", gen_symmetric(60, 0.05, 4), true);

    // general (ILU0 only) -- NO symmetrization
    fails += run_case("gen-small", gen_general(8, 0.30, 5), false);
    fails += run_case("gen-med", gen_general(40, 0.15, 6), false);
    fails += run_case("gen-dense", gen_general(30, 0.45, 7), false);
    fails += run_case("gen-sparse", gen_general(60, 0.05, 8), false);

    printf("\n%s  (%d failing case(s))\n", fails == 0 ? "ALL TESTS PASSED" : "TESTS FAILED", fails);
    return fails == 0 ? 0 : 1;
}
