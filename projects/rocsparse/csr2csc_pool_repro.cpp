// csr2csc_pool_repro.cpp
//
// Minimal standalone reproducer for the memory corruption observed while
// debugging rocsparse csritilu0 (alg = sync_split) under the UB26 toolchain
// on gfx950 / MI350.
//
// Background
// ----------
// Inside csritilu0/sync_split, csxsldu transposes the U factor with
// rocsparse::csr2csc (action = numeric). Instrumentation showed that the call
// ZEROES its (const) input value array, which lives in a hipMallocAsync pool
// allocation that is DISJOINT (~36 GB away in VA space) from everything
// csr2csc legitimately writes:
//
//   csr_val (input, pool)   0x71a2_70a0_1300 .. 0x71a2_70a0_16dc   <- comes back all-zero
//   temp_buffer    (pool)   0x71a2_70a0_0100 .. 0x71a2_70a0_0d00
//   csc_col_ptr (output)    0x71ab_1272_de94 .. 0x71ab_1272_e184   <- the only thing coo2csr writes
//
// coo2csr_core (called inside csr2csc) writes only csc_col_ptr (188 ints in the
// user region 0x71ab...), yet right after it the 247-int csr_val buffer in the
// pool region 0x71a2... is fully zeroed. A correctly-bounded kernel writing one
// VA range cannot zero an unrelated VA range -> the defect is below rocSPARSE
// (HIP runtime / hipMallocAsync stream-ordered pool / codegen / driver).
//
// It is ORDER-dependent (only after the async pool has been recycled) and
// SIZE-dependent, so this reproducer dirties the pool first and sweeps several
// matrix sizes / repetitions.
//
// What it checks
// --------------
// csr2csc must treat csr_val as const. After the call we compare csr_val to the
// host copy we uploaded. Any difference (in particular: zeros) is the bug.
//
// Build:
//   hipcc -O3 csr2csc_pool_repro.cpp -o csr2csc_pool_repro \
//        -I<rocsparse_install>/include -L<rocsparse_install>/lib -lrocsparse
//   # e.g. with an in-tree build:
//   #   -I build/release/include -L build/release/library -lrocsparse
//
// Run:
//   LD_LIBRARY_PATH=<rocsparse_install>/lib ./csr2csc_pool_repro
//
#include <hip/hip_runtime.h>
#include <rocsparse/rocsparse.h>

#include <cstdint>
#include <cstdio>
#include <vector>

#define HIP_CHECK(x)                                                         \
    do                                                                       \
    {                                                                        \
        hipError_t _e = (x);                                                 \
        if(_e != hipSuccess)                                                 \
        {                                                                    \
            printf("HIP error '%s' at %s:%d\n", hipGetErrorString(_e),       \
                   __FILE__, __LINE__);                                      \
            return 2;                                                        \
        }                                                                    \
    } while(0)

#define ROC_CHECK(x)                                                         \
    do                                                                       \
    {                                                                        \
        rocsparse_status _s = (x);                                           \
        if(_s != rocsparse_status_success)                                   \
        {                                                                    \
            printf("rocSPARSE error %d at %s:%d\n", (int)_s, __FILE__,        \
                   __LINE__);                                                \
            return 2;                                                        \
        }                                                                    \
    } while(0)

// Strict-upper banded CSR matrix (super- and super-super-diagonal), n x n,
// zero-based. Values are a strictly-nonzero sentinel pattern (1..nnz) so any
// zero that comes back is corruption.
static void build_strict_upper(int                 n,
                               std::vector<int>&    ptr,
                               std::vector<int>&    ind,
                               std::vector<double>& val)
{
    ptr.assign(n + 1, 0);
    for(int i = 0; i < n; ++i)
    {
        int cnt = 0;
        if(i + 1 < n)
            ++cnt;
        if(i + 2 < n)
            ++cnt;
        ptr[i + 1] = ptr[i] + cnt;
    }
    const int nnz = ptr[n];
    ind.resize(nnz);
    val.resize(nnz);
    int k = 0;
    for(int i = 0; i < n; ++i)
    {
        if(i + 1 < n)
        {
            ind[k] = i + 1;
            val[k] = (double)(k + 1);
            ++k;
        }
        if(i + 2 < n)
        {
            ind[k] = i + 2;
            val[k] = (double)(k + 1);
            ++k;
        }
    }
}

static int run_once(rocsparse_handle handle, hipStream_t stream, int n, int rep, int& bug)
{
    std::vector<int>    hptr, hind;
    std::vector<double> hval;
    build_strict_upper(n, hptr, hind, hval);
    const int m   = n;
    const int nnz = hptr[n];
    if(nnz == 0)
        return 0;

    // 1) Dirty the stream-ordered pool: allocate, fill with a nonzero pattern,
    //    free. Subsequent pool allocations are then served from recycled,
    //    non-zeroed memory -- the condition under which the bug appears.
    {
        void*        d     = nullptr;
        const size_t bytes = (size_t)64 << 20;
        HIP_CHECK(hipMallocAsync(&d, bytes, stream));
        HIP_CHECK(hipMemsetAsync(d, 0xCD, bytes, stream));
        HIP_CHECK(hipFreeAsync(d, stream));
    }

    // 2) csr2csc INPUTS from the async pool (mimics csxsldu's tmp_* temporaries).
    int*    dptr = nullptr;
    int*    dind = nullptr;
    double* dval = nullptr;
    HIP_CHECK(hipMallocAsync((void**)&dptr, sizeof(int) * (m + 1), stream));
    HIP_CHECK(hipMallocAsync((void**)&dind, sizeof(int) * nnz, stream));
    HIP_CHECK(hipMallocAsync((void**)&dval, sizeof(double) * nnz, stream));
    HIP_CHECK(hipMemcpyAsync(dptr, hptr.data(), sizeof(int) * (m + 1), hipMemcpyHostToDevice, stream));
    HIP_CHECK(hipMemcpyAsync(dind, hind.data(), sizeof(int) * nnz, hipMemcpyHostToDevice, stream));
    HIP_CHECK(hipMemcpyAsync(dval, hval.data(), sizeof(double) * nnz, hipMemcpyHostToDevice, stream));

    // temp_buffer, also from the async pool.
    size_t bufsz = 0;
    ROC_CHECK(rocsparse_csr2csc_buffer_size(
        handle, m, n, nnz, dptr, dind, rocsparse_action_numeric, &bufsz));
    void* dtmp = nullptr;
    HIP_CHECK(hipMallocAsync(&dtmp, bufsz, stream));

    // 3) OUTPUTS in a separate, regular (non-pool) allocation -- disjoint, like
    //    the user-provided csritilu0 working buffer in the original failure.
    int*    cptr = nullptr;
    int*    cind = nullptr;
    double* cval = nullptr;
    HIP_CHECK(hipMalloc((void**)&cptr, sizeof(int) * (n + 1)));
    HIP_CHECK(hipMalloc((void**)&cind, sizeof(int) * nnz));
    HIP_CHECK(hipMalloc((void**)&cval, sizeof(double) * nnz));

    HIP_CHECK(hipStreamSynchronize(stream));

    // 4) Transpose. csr_val is a CONST input; csr2csc must not modify it.
    ROC_CHECK(rocsparse_dcsr2csc(handle,
                                 m,
                                 n,
                                 nnz,
                                 dval,
                                 dptr,
                                 dind,
                                 cval,
                                 cind,
                                 cptr,
                                 rocsparse_action_numeric,
                                 rocsparse_index_base_zero,
                                 dtmp));
    HIP_CHECK(hipStreamSynchronize(stream));

    // 5) Did the const input survive the call?
    std::vector<double> back(nnz);
    HIP_CHECK(hipMemcpy(back.data(), dval, sizeof(double) * nnz, hipMemcpyDeviceToHost));
    int zeros = 0, changed = 0;
    for(int i = 0; i < nnz; ++i)
    {
        if(back[i] == 0.0)
            ++zeros;
        if(back[i] != hval[i])
            ++changed;
    }

    printf("n=%-4d nnz=%-5d rep=%d : csr_val changed=%-5d zeros=%-5d  "
           "| csr_val@%p temp@%p csc_col@%p%s\n",
           n,
           nnz,
           rep,
           changed,
           zeros,
           (void*)dval,
           (void*)dtmp,
           (void*)cptr,
           changed ? "   <<< CORRUPTED" : "");

    if(changed)
        bug = 1;

    HIP_CHECK(hipFreeAsync(dptr, stream));
    HIP_CHECK(hipFreeAsync(dind, stream));
    HIP_CHECK(hipFreeAsync(dval, stream));
    HIP_CHECK(hipFreeAsync(dtmp, stream));
    HIP_CHECK(hipFree(cptr));
    HIP_CHECK(hipFree(cind));
    HIP_CHECK(hipFree(cval));
    HIP_CHECK(hipStreamSynchronize(stream));
    return 0;
}

int main()
{
    int devId = 0;
    HIP_CHECK(hipSetDevice(devId));
    hipDeviceProp_t prop;
    HIP_CHECK(hipGetDeviceProperties(&prop, devId));
    printf("Device: %s\n", prop.name);

    hipStream_t stream;
    HIP_CHECK(hipStreamCreate(&stream));
    rocsparse_handle handle;
    ROC_CHECK(rocsparse_create_handle(&handle));
    ROC_CHECK(rocsparse_set_stream(handle, stream));

    int bug = 0;
    // Sweep sizes (endbit = bitwidth(n) varies => different radix-sort pass
    // parity) and repeat to maximise the chance of hitting the pool state.
    const int sizes[] = {50, 100, 128, 150, 187, 200, 250, 300, 400};
    for(int rep = 0; rep < 8 && !bug; ++rep)
        for(int s = 0; s < (int)(sizeof(sizes) / sizeof(sizes[0])) && !bug; ++s)
        {
            int rc = run_once(handle, stream, sizes[s], rep, bug);
            if(rc)
                return rc;
        }

    ROC_CHECK(rocsparse_destroy_handle(handle));
    HIP_CHECK(hipStreamDestroy(stream));

    printf("\n%s\n",
           bug ? "REPRODUCED: rocsparse_dcsr2csc modified/zeroed its const "
                 "csr_val input (disjoint-buffer corruption)."
               : "No corruption observed in this run (try more reps / sizes, "
                 "or the integer-value variant -- see notes).");
    return bug ? 1 : 0;
}
