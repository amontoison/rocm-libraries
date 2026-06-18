// ============================================================================
//  Cross-validate the pure-AMD host symbolic fill (symmetric / IC,ILDLT path:
//  elimination-tree + ereach) against SuiteSparse CXSparse.
//
//  Oracle: cs_schol(order=0, A)  (natural order, no fill-reducing perm)
//          + cs_chol(A, S)       (exact symbolic+numeric Cholesky)
//  -> N->L is the complete L factor in natural order; we compare its pattern
//     (lower triangle) to symbolic_fillin_host(symmetric=true).
//
//  (General/ILU is NOT comparable here: cs_lu does partial pivoting, our fill
//   is no-pivot natural order.  The symmetric case is the apples-to-apples one
//   and is exactly our "smart" SuiteSparse-style algorithm.)
//
//  Build:
//    g++ -O2 -std=c++17 validate_suitesparse.cpp -o validate_suitesparse \
//      -I/tmp/ss/root/usr/include/suitesparse \
//      -L/tmp/ss/root/usr/lib/x86_64-linux-gnu -lcxsparse \
//      -Wl,-rpath,/tmp/ss/root/usr/lib/x86_64-linux-gnu
// ============================================================================

#include <cmath>
#include <cstdio>
#include <map>
#include <random>
#include <set>
#include <vector>

#include "cs.h" // CXSparse (has its own extern "C" guards)

#include "../library/src/precond/rocsparse_symbolic_fillin_host.hpp"

// symmetric SPD matrix in CSR (== CSC since symmetric), 0-based
struct Csr
{
    int                 n = 0;
    std::vector<int>    row_ptr, col_ind;
    std::vector<double> val;
    int                 nnz() const { return row_ptr[n]; }
};

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
    Csr A;
    A.n = n;
    A.row_ptr.assign(n + 1, 0);
    for(int i = 0; i < n; ++i)
        A.row_ptr[i + 1] = A.row_ptr[i] + (int)P[i].size();
    for(int i = 0; i < n; ++i)
        for(int c : P[i])
            A.col_ind.push_back(c);
    A.val.assign(A.nnz(), 0.0);
    std::map<std::pair<int, int>, double> w;
    for(int i = 0; i < n; ++i)
        for(int p = A.row_ptr[i]; p < A.row_ptr[i + 1]; ++p)
            if(i < A.col_ind[p])
                w[{i, A.col_ind[p]}] = u(rng) - 0.5;
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
                A.val[p] = off + 1.0;
    }
    return A;
}

// lower-triangle pattern (i>=j) from a CXSparse CSC factor L
static std::set<std::pair<int, int>> lower_pairs_cs(const cs* L)
{
    std::set<std::pair<int, int>> s;
    for(int j = 0; j < L->n; ++j)
        for(int p = L->p[j]; p < L->p[j + 1]; ++p)
            s.insert({L->i[p], j}); // CSC col j, row i>=j
    return s;
}

// lower-triangle pattern from our full symmetric CSR F
static std::set<std::pair<int, int>> lower_pairs_host(int                     n,
                                                      const std::vector<int>& rp,
                                                      const std::vector<int>& ci)
{
    std::set<std::pair<int, int>> s;
    for(int i = 0; i < n; ++i)
        for(int p = rp[i]; p < rp[i + 1]; ++p)
            if(ci[p] <= i)
                s.insert({i, ci[p]});
    return s;
}

static int run_case(const char* name, const Csr& A)
{
    const int n = A.n;

    // ---- build CXSparse matrix (triplet -> compressed CSC) ----
    cs* T = cs_spalloc(n, n, A.nnz(), 1, 1);
    for(int i = 0; i < n; ++i)
        for(int p = A.row_ptr[i]; p < A.row_ptr[i + 1]; ++p)
            cs_entry(T, i, A.col_ind[p], A.val[p]);
    cs* M = cs_compress(T);
    cs_spfree(T);

    // ---- SuiteSparse: natural-order symbolic + Cholesky ----
    css* S = cs_schol(0, M); // order 0 = natural, no fill-reducing perm
    csn* N = cs_chol(M, S);
    if(!N)
    {
        printf("[%-10s] cs_chol FAILED (not SPD?)\n", name);
        return 1;
    }
    auto ss_pairs = lower_pairs_cs(N->L);

    // ---- pure-AMD host (symmetric path: etree + ereach) ----
    std::vector<int> rp, ci;
    rocsparse::symbolic_fillin_host<int, int>(true, n, A.row_ptr.data(), A.col_ind.data(), 0, rp,
                                              ci);
    auto host_pairs = lower_pairs_host(n, rp, ci);

    const bool match = (ss_pairs == host_pairs);
    printf("[%-10s] n=%4d  nnz(L) SuiteSparse=%5zu  AMD-host=%5zu  match=%s\n", name, n,
           ss_pairs.size(), host_pairs.size(), match ? "OK" : "FAIL");

    cs_sfree(S);
    cs_nfree(N);
    cs_spfree(M);
    return match ? 0 : 1;
}

int main()
{
    int fails = 0;
    fails += run_case("sym-small", gen_symmetric(8, 0.30, 1));
    fails += run_case("sym-med", gen_symmetric(40, 0.15, 2));
    fails += run_case("sym-dense", gen_symmetric(30, 0.45, 3));
    fails += run_case("sym-sparse", gen_symmetric(60, 0.05, 4));
    fails += run_case("sym-big", gen_symmetric(200, 0.04, 9));
    fails += run_case("sym-big2", gen_symmetric(150, 0.10, 10));
    printf("\n%s (%d failing)\n",
           fails == 0 ? "HOST MATCHES SUITESPARSE ON ALL CASES" : "MISMATCH", fails);
    return fails == 0 ? 0 : 1;
}
