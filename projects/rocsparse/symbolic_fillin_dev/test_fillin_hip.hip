// ============================================================================
//  Standalone HIP version of the symbolic fill-in (block-per-row, dynamic-LDS
//  hash).  Runs the GPU kernel and self-validates against an exact CPU
//  reference.  Same fixed-point logic as rocsparse_symbolic_fillin.hpp, but
//  dependency-free so it can be built and run on its own.
//
//  Build:  hipcc -O2 -std=c++17 test_fillin_hip.hip -o test_fillin_hip
//  Run  :  ./test_fillin_hip          (needs a GPU)
// ============================================================================

#include <hip/hip_runtime.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <set>
#include <vector>

using std::size_t;

#define HIP_CHECK(cmd)                                                                       \
    do                                                                                       \
    {                                                                                        \
        hipError_t e = (cmd);                                                                \
        if(e != hipSuccess)                                                                  \
        {                                                                                    \
            printf("HIP error %s at %s:%d\n", hipGetErrorString(e), __FILE__, __LINE__);     \
            return 2;                                                                        \
        }                                                                                    \
    } while(0)

// ---------------------------------------------------------------------------
// Host helpers (CSR pattern + exact reference fill).
// ---------------------------------------------------------------------------
struct Csr
{
    int              n = 0;
    std::vector<int> row_ptr, col_ind;
    int              nnz() const { return row_ptr.empty() ? 0 : row_ptr[n]; }
};

static std::vector<std::set<int>> to_rows(const Csr& A)
{
    std::vector<std::set<int>> r(A.n);
    for(int i = 0; i < A.n; ++i)
        for(int p = A.row_ptr[i]; p < A.row_ptr[i + 1]; ++p)
            r[i].insert(A.col_ind[p]);
    return r;
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

// exact fill, natural order, no pivot (ground truth)
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
    return from_rows(P);
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
    return from_rows(P);
}

// ===========================================================================
//  Device kernel: one block per row, dynamic-LDS hash set.
//  empty sentinel = n.  count==true -> write per-row nnz; else emit sorted.
// ===========================================================================
__device__ inline void insert_key_dev(int key, int* table, int hashsize, int empty)
{
    unsigned h = ((unsigned)key * 11u) & (unsigned)(hashsize - 1);
    while(true)
    {
        int cur = table[h];
        if(cur == key)
            return;
        if(cur == empty)
        {
            if(atomicCAS(&table[h], empty, key) == empty)
                return;
        }
        else
        {
            h = (h + 1) & (unsigned)(hashsize - 1);
        }
    }
}

__global__ void fillin_kernel(int        n,
                              const int* __restrict__ row_ptr,
                              const int* __restrict__ col_ind,
                              int* __restrict__ row_nnz_out,
                              const int* __restrict__ row_ptr_out,
                              int* __restrict__ col_ind_out,
                              int        hashsize,
                              int        count)
{
    extern __shared__ int table[];
    const int             row = blockIdx.x;
    const int             tid = threadIdx.x;

    for(int i = tid; i < hashsize; i += blockDim.x)
        table[i] = n; // empty
    __syncthreads();

    const int rb = row_ptr[row];
    const int re = row_ptr[row + 1];

    // (1) own entries
    for(int p = rb + tid; p < re; p += blockDim.x)
        insert_key_dev(col_ind[p], table, hashsize, n);
    __syncthreads();

    // (2) pivot propagation: k in row(row), k<row -> { j in row(k) : j>k }
    for(int p = rb + tid; p < re; p += blockDim.x)
    {
        const int k = col_ind[p];
        if(k < row)
            for(int t = row_ptr[k]; t < row_ptr[k + 1]; ++t)
            {
                const int j = col_ind[t];
                if(j > k)
                    insert_key_dev(j, table, hashsize, n);
            }
    }
    __syncthreads();

    if(count)
    {
        __shared__ int cnt;
        if(tid == 0)
            cnt = 0;
        __syncthreads();
        for(int i = tid; i < hashsize; i += blockDim.x)
            if(table[i] < n)
                atomicAdd(&cnt, 1);
        __syncthreads();
        if(tid == 0)
            row_nnz_out[row] = cnt;
    }
    else
    {
        const int ob = row_ptr_out[row];
        const int oe = row_ptr_out[row + 1];
        for(int i = tid; i < hashsize; i += blockDim.x)
        {
            const int col = table[i];
            if(col >= n)
                continue;
            int idx = ob;
            for(int h = 0; h < hashsize; ++h)
                if(col > table[h])
                    ++idx;
            if(idx >= ob && idx < oe)
                col_ind_out[idx] = col;
        }
    }
}

static int next_pow2(int x)
{
    int p = 16;
    while(p < x)
        p <<= 1;
    return p;
}

// ===========================================================================
//  Host driver: GPU fixed-point loop (host-side prefix sum for simplicity).
// ===========================================================================
static int gpu_fill(const Csr& A, Csr& F_out)
{
    const int n = A.n;

    int *d_Frow, *d_Fcol, *d_nrow;
    HIP_CHECK(hipMalloc(&d_Frow, sizeof(int) * (n + 1)));
    HIP_CHECK(hipMalloc(&d_Fcol, sizeof(int) * A.nnz()));
    HIP_CHECK(hipMalloc(&d_nrow, sizeof(int) * (n + 1)));
    HIP_CHECK(hipMemcpy(d_Frow, A.row_ptr.data(), sizeof(int) * (n + 1), hipMemcpyHostToDevice));
    HIP_CHECK(hipMemcpy(d_Fcol, A.col_ind.data(), sizeof(int) * A.nnz(), hipMemcpyHostToDevice));
    int F_nnz = A.nnz();

    const int BS = 128;

    for(int it = 0; it < 1000; ++it)
    {
        // max row nnz of current F (host side from row_ptr)
        std::vector<int> hrow(n + 1);
        HIP_CHECK(hipMemcpy(hrow.data(), d_Frow, sizeof(int) * (n + 1), hipMemcpyDeviceToHost));
        int maxr = 0;
        for(int i = 0; i < n; ++i)
            maxr = std::max(maxr, hrow[i + 1] - hrow[i]);
        const int hashsize = next_pow2(maxr * 2 + 1);
        const size_t shmem = sizeof(int) * hashsize;

        // pass 1: count
        hipLaunchKernelGGL(fillin_kernel, dim3(n), dim3(BS), shmem, 0, n, d_Frow, d_Fcol, d_nrow,
                           (const int*)nullptr, (int*)nullptr, hashsize, 1);
        HIP_CHECK(hipGetLastError());

        // host exclusive scan -> next row_ptr
        std::vector<int> nnzc(n + 1);
        HIP_CHECK(hipMemcpy(nnzc.data(), d_nrow, sizeof(int) * (n + 1), hipMemcpyDeviceToHost));
        int acc = 0;
        for(int i = 0; i < n; ++i)
        {
            int c     = nnzc[i];
            nnzc[i]   = acc;
            acc += c;
        }
        nnzc[n] = acc;
        const int new_nnz = acc;

        if(new_nnz == F_nnz)
            break; // converged

        HIP_CHECK(hipMemcpy(d_nrow, nnzc.data(), sizeof(int) * (n + 1), hipMemcpyHostToDevice));

        // pass 2: fill
        int* d_newcol;
        HIP_CHECK(hipMalloc(&d_newcol, sizeof(int) * new_nnz));
        hipLaunchKernelGGL(fillin_kernel, dim3(n), dim3(BS), shmem, 0, n, d_Frow, d_Fcol,
                           (int*)nullptr, d_nrow, d_newcol, hashsize, 0);
        HIP_CHECK(hipGetLastError());

        HIP_CHECK(hipFree(d_Fcol));
        d_Fcol = d_newcol;
        HIP_CHECK(hipMemcpy(d_Frow, d_nrow, sizeof(int) * (n + 1), hipMemcpyDeviceToDevice));
        F_nnz = new_nnz;
    }

    F_out.n = n;
    F_out.row_ptr.assign(n + 1, 0);
    F_out.col_ind.assign(F_nnz, 0);
    HIP_CHECK(hipMemcpy(F_out.row_ptr.data(), d_Frow, sizeof(int) * (n + 1), hipMemcpyDeviceToHost));
    HIP_CHECK(hipMemcpy(F_out.col_ind.data(), d_Fcol, sizeof(int) * F_nnz, hipMemcpyDeviceToHost));
    HIP_CHECK(hipFree(d_Frow));
    HIP_CHECK(hipFree(d_Fcol));
    HIP_CHECK(hipFree(d_nrow));
    return 0;
}

static int run_case(const char* name, const Csr& A)
{
    auto ref = reference_fill(A);
    Csr  G;
    int  rc = gpu_fill(A, G);
    if(rc)
        return rc;
    auto gpu = to_rows(G);
    bool ok  = (gpu == ref);
    size_t nf = 0;
    for(auto& r : ref)
        nf += r.size();
    printf("[%-10s] n=%4d nnz(A)=%6d nnz(fill)=%6zu  GPU-vs-ref: %s\n", name, A.n, A.nnz(), nf,
           ok ? "OK" : "FAIL");
    return ok ? 0 : 1;
}

int main()
{
    int dev = 0;
    if(hipGetDeviceCount(&dev) != hipSuccess || dev == 0)
    {
        printf("No HIP device available -- compile-only check passed.\n");
        return 0;
    }
    int fails = 0;
    fails += run_case("sym-small", gen_symmetric(8, 0.30, 1));
    fails += run_case("sym-med", gen_symmetric(40, 0.15, 2));
    fails += run_case("sym-sparse", gen_symmetric(60, 0.05, 4));
    fails += run_case("gen-small", gen_general(8, 0.30, 5));
    fails += run_case("gen-med", gen_general(40, 0.15, 6));
    fails += run_case("gen-sparse", gen_general(60, 0.05, 8));
    printf("\n%s (%d failing)\n", fails == 0 ? "ALL GPU TESTS PASSED" : "GPU TESTS FAILED", fails);
    return fails == 0 ? 0 : 1;
}
