// ============================================================================
//  End-to-end: complete fill pattern  ->  scatter A with explicit zeros  ->
//  REAL rocSPARSE rocsparse_dcsrilu0 / rocsparse_dcsric0  ->  verify the
//  factorization is EXACT (because the pattern is complete).
//
//  ildlt0 has no public C API yet (WIP), so it is covered by the CPU proof
//  (test_fillin_cpu) only; add it here once rocsparse_dcsrildlt0 is exported.
//
//  Build (against the in-tree build):
//    hipcc -O2 -std=c++17 test_end_to_end.hip -o test_end_to_end \
//      -I ../build/release/include \
//      -L ../build/release/library -lrocsparse \
//      -Wl,-rpath,../build/release/library
//  Run (needs a GPU):  ./test_end_to_end
// ============================================================================

#include <hip/hip_runtime.h>
#include <rocsparse/rocsparse.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <random>
#include <set>
#include <vector>

using std::size_t;

#define HIP_CHECK(c)                                                                    \
    do                                                                                  \
    {                                                                                   \
        hipError_t e = (c);                                                             \
        if(e != hipSuccess)                                                             \
        {                                                                               \
            printf("HIP error %s @%d\n", hipGetErrorString(e), __LINE__);               \
            return 2;                                                                   \
        }                                                                               \
    } while(0)
#define ROC_CHECK(c)                                                                    \
    do                                                                                  \
    {                                                                                   \
        rocsparse_status s = (c);                                                       \
        if(s != rocsparse_status_success)                                               \
        {                                                                               \
            printf("rocsparse error %d @%d\n", (int)s, __LINE__);                       \
            return 3;                                                                   \
        }                                                                               \
    } while(0)

struct Csr
{
    int                 n = 0;
    std::vector<int>    row_ptr, col_ind;
    std::vector<double> val;
    int                 nnz() const { return row_ptr.empty() ? 0 : row_ptr[n]; }
};

static std::vector<std::set<int>> to_rows(const Csr& A)
{
    std::vector<std::set<int>> r(A.n);
    for(int i = 0; i < A.n; ++i)
        for(int p = A.row_ptr[i]; p < A.row_ptr[i + 1]; ++p)
            r[i].insert(A.col_ind[p]);
    return r;
}

// exact complete fill (ground truth pattern; equals the GPU routine's output)
static std::vector<std::set<int>> reference_fill(const Csr& A)
{
    auto F = to_rows(A);
    for(int i = 0; i < A.n; ++i)
        F[i].insert(i);
    for(int k = 0; k < A.n; ++k)
        for(int i = k + 1; i < A.n; ++i)
            if(F[i].count(k))
                for(int j : F[k])
                    if(j > k)
                        F[i].insert(j);
    return F;
}

// scatter: build CSR with pattern F, A's values where present, 0 elsewhere
static Csr scatter(const Csr& A, const std::vector<std::set<int>>& F)
{
    Csr S;
    S.n = A.n;
    S.row_ptr.assign(A.n + 1, 0);
    for(int i = 0; i < A.n; ++i)
        S.row_ptr[i + 1] = S.row_ptr[i] + (int)F[i].size();
    S.col_ind.reserve(S.row_ptr[A.n]);
    S.val.reserve(S.row_ptr[A.n]);
    auto Arows = std::vector<std::map<int, double>>(A.n);
    for(int i = 0; i < A.n; ++i)
        for(int p = A.row_ptr[i]; p < A.row_ptr[i + 1]; ++p)
            Arows[i][A.col_ind[p]] = A.val[p];
    for(int i = 0; i < A.n; ++i)
        for(int j : F[i])
        {
            S.col_ind.push_back(j);
            auto it = Arows[i].find(j);
            S.val.push_back(it == Arows[i].end() ? 0.0 : it->second);
        }
    return S;
}

static Csr from_rows(const std::vector<std::set<int>>& rows)
{
    Csr A;
    A.n = (int)rows.size();
    A.row_ptr.assign(A.n + 1, 0);
    for(int i = 0; i < A.n; ++i)
        A.row_ptr[i + 1] = A.row_ptr[i] + (int)rows[i].size();
    for(int i = 0; i < A.n; ++i)
        for(int c : rows[i])
            A.col_ind.push_back(c);
    return A;
}

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
    Csr A = from_rows(P);
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

// dense lookups from a factored CSR (pattern S, values vals)
static double getv(const Csr& S, const std::vector<double>& vals, int i, int j)
{
    for(int p = S.row_ptr[i]; p < S.row_ptr[i + 1]; ++p)
        if(S.col_ind[p] == j)
            return vals[p];
    return 0.0;
}
static double dense_A(const Csr& A, int i, int j)
{
    return getv(A, A.val, i, j);
}

// ---- device factorization helpers ----------------------------------------
struct Dev
{
    int *   row = nullptr, *col = nullptr;
    double* val = nullptr;
};
static int upload(const Csr& S, Dev& d)
{
    HIP_CHECK(hipMalloc(&d.row, sizeof(int) * (S.n + 1)));
    HIP_CHECK(hipMalloc(&d.col, sizeof(int) * S.nnz()));
    HIP_CHECK(hipMalloc(&d.val, sizeof(double) * S.nnz()));
    HIP_CHECK(hipMemcpy(d.row, S.row_ptr.data(), sizeof(int) * (S.n + 1), hipMemcpyHostToDevice));
    HIP_CHECK(hipMemcpy(d.col, S.col_ind.data(), sizeof(int) * S.nnz(), hipMemcpyHostToDevice));
    HIP_CHECK(hipMemcpy(d.val, S.val.data(), sizeof(double) * S.nnz(), hipMemcpyHostToDevice));
    return 0;
}

static int test_ilu0(rocsparse_handle h, const Csr& A)
{
    auto F = reference_fill(A);
    Csr  S = scatter(A, F);
    Dev  d;
    if(upload(S, d))
        return 2;

    rocsparse_mat_descr descr;
    rocsparse_mat_info  info;
    ROC_CHECK(rocsparse_create_mat_descr(&descr));
    ROC_CHECK(rocsparse_create_mat_info(&info));

    size_t bs = 0;
    ROC_CHECK(rocsparse_dcsrilu0_buffer_size(h, S.n, S.nnz(), descr, d.val, d.row, d.col, info, &bs));
    void* buf = nullptr;
    HIP_CHECK(hipMalloc(&buf, bs));
    ROC_CHECK(rocsparse_dcsrilu0_analysis(h, S.n, S.nnz(), descr, d.val, d.row, d.col, info,
                                          rocsparse_analysis_policy_force,
                                          rocsparse_solve_policy_auto, buf));
    ROC_CHECK(rocsparse_dcsrilu0(h, S.n, S.nnz(), descr, d.val, d.row, d.col, info,
                                 rocsparse_solve_policy_auto, buf));

    std::vector<double> fv(S.nnz());
    HIP_CHECK(hipMemcpy(fv.data(), d.val, sizeof(double) * S.nnz(), hipMemcpyDeviceToHost));

    // reconstruct L (unit, strict lower) * U (on/above diag) and compare to A
    double mx = 0.0;
    for(int i = 0; i < A.n; ++i)
        for(int j = 0; j < A.n; ++j)
        {
            double s = 0.0;
            for(int k = 0; k <= std::min(i, j); ++k)
            {
                double lik = (k == i) ? 1.0 : getv(S, fv, i, k);
                double ukj = getv(S, fv, k, j);
                s += lik * ukj;
            }
            mx = std::max(mx, std::fabs(s - dense_A(A, i, j)));
        }

    printf("  ILU0   n=%3d nnz(fill)=%5d  ||LU-A||=%.2e  %s\n", A.n, S.nnz(), mx,
           mx < 1e-9 ? "OK" : "FAIL");
    rocsparse_destroy_mat_descr(descr);
    rocsparse_destroy_mat_info(info);
    (void)hipFree(buf);
    (void)hipFree(d.row);
    (void)hipFree(d.col);
    (void)hipFree(d.val);
    return mx < 1e-9 ? 0 : 1;
}

static int test_ic0(rocsparse_handle h, const Csr& A)
{
    auto F = reference_fill(A);
    Csr  S = scatter(A, F);
    Dev  d;
    if(upload(S, d))
        return 2;

    rocsparse_mat_descr descr;
    rocsparse_mat_info  info;
    ROC_CHECK(rocsparse_create_mat_descr(&descr));
    ROC_CHECK(rocsparse_create_mat_info(&info));

    size_t bs = 0;
    ROC_CHECK(rocsparse_dcsric0_buffer_size(h, S.n, S.nnz(), descr, d.val, d.row, d.col, info, &bs));
    void* buf = nullptr;
    HIP_CHECK(hipMalloc(&buf, bs));
    ROC_CHECK(rocsparse_dcsric0_analysis(h, S.n, S.nnz(), descr, d.val, d.row, d.col, info,
                                         rocsparse_analysis_policy_force,
                                         rocsparse_solve_policy_auto, buf));
    ROC_CHECK(rocsparse_dcsric0(h, S.n, S.nnz(), descr, d.val, d.row, d.col, info,
                                rocsparse_solve_policy_auto, buf));

    std::vector<double> fv(S.nnz());
    HIP_CHECK(hipMemcpy(fv.data(), d.val, sizeof(double) * S.nnz(), hipMemcpyDeviceToHost));

    // L is the lower triangle (incl diagonal) of the factored matrix; check L*L^T == A
    double mx = 0.0;
    for(int i = 0; i < A.n; ++i)
        for(int j = 0; j < A.n; ++j)
        {
            double s = 0.0;
            for(int k = 0; k <= std::min(i, j); ++k)
                s += getv(S, fv, i, k) * getv(S, fv, j, k);
            mx = std::max(mx, std::fabs(s - dense_A(A, i, j)));
        }

    printf("  IC0    n=%3d nnz(fill)=%5d  ||LL^T-A||=%.2e  %s\n", A.n, S.nnz(), mx,
           mx < 1e-9 ? "OK" : "FAIL");
    rocsparse_destroy_mat_descr(descr);
    rocsparse_destroy_mat_info(info);
    (void)hipFree(buf);
    (void)hipFree(d.row);
    (void)hipFree(d.col);
    (void)hipFree(d.val);
    return mx < 1e-9 ? 0 : 1;
}

int main()
{
    int dev = 0;
    if(hipGetDeviceCount(&dev) != hipSuccess || dev == 0)
    {
        printf("No HIP device available -- compile/link check only.\n");
        return 0;
    }
    rocsparse_handle h;
    if(rocsparse_create_handle(&h) != rocsparse_status_success)
    {
        printf("cannot create rocsparse handle\n");
        return 3;
    }

    int fails = 0;
    printf("ILU0 (general matrices):\n");
    fails += test_ilu0(h, gen_general(8, 0.30, 5));
    fails += test_ilu0(h, gen_general(40, 0.15, 6));
    printf("ILU0 + IC0 (symmetric SPD matrices):\n");
    fails += test_ilu0(h, gen_symmetric(40, 0.15, 2));
    fails += test_ic0(h, gen_symmetric(8, 0.30, 1));
    fails += test_ic0(h, gen_symmetric(40, 0.15, 2));
    fails += test_ic0(h, gen_symmetric(60, 0.05, 4));

    rocsparse_destroy_handle(h);
    printf("\n%s (%d failing)\n", fails == 0 ? "ALL rocSPARSE END-TO-END TESTS PASSED" : "FAILED",
           fails);
    return fails == 0 ? 0 : 1;
}
