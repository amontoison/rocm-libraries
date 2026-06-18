/*! \file */
/* ************************************************************************
 * Copyright (C) 2026 Advanced Micro Devices, Inc. All rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * ************************************************************************ */

#pragma once

// ============================================================================
//  symbolic_fillin_computation  --  VERSION 0 SKELETON
//
//  Computes the sparsity pattern F of the *complete* factor (in the given
//  natural order, NO reordering, NO pivoting) so that the caller can copy A
//  into a buffer with this pattern (explicit zeros at fill positions) and turn
//  an INCOMPLETE factorization into the EXACT complete one:
//
//      IC(0)    on full symmetric fill pattern  ==  exact Cholesky
//      ILDLT(0) on full symmetric fill pattern  ==  exact LDL^T   (D diagonal)
//      ILU(0)   on full L+U fill pattern         ==  exact LU      (no pivot)
//
//  Single algorithm for all three -- Jacobi fixed point on the combined
//  L+U pattern F:
//
//      F <- F0
//      repeat
//          for each row i (in parallel):
//              S_i = row_F(i)  U  U_{k in row_F(i), k<i} { j in row_F(k) : j>k }
//          F <- S
//      until nnz(F) stops changing
//
//  Why one kernel covers both symmetric and unsymmetric
//  ----------------------------------------------------
//  F is the smallest superset of pattern(F0) closed under
//      (i,k),(k,j) in F  with  k<i and k<j  =>  (i,j) in F,
//  which is exactly the rank-1 elimination update at pivot k (rows i>k, cols
//  j>k).  The kernel adds (i,j) for k in row(i) (k<i) and j in row(k) (j>k),
//  i.e. k<i and k<j -- identical.  So the *kernel* is mode-agnostic; only the
//  preparation of F0 differs:
//
//      ROCSPARSE_FILLIN_SYMMETRIC : F0 = pattern(A) U pattern(A^T)
//                                   (csr2csc pattern + csrgeam symbolic)
//                                   -> for IC(0) / ILDLT(0)
//      ROCSPARSE_FILLIN_GENERAL   : F0 = pattern(A)              (NO geam)
//                                   -> for ILU(0)
//
//  Reused from csrgemm (csrgemm_symbolic_device.h):
//      rocsparse::insert_key<>()                  -- LDS hash-set insert
//      rocsparse::csrgemm_symbolic_max_row_nnz_*  -- max row nnz reduction
//
//  TODO before production: rows > largest hash bucket (-> multipass bitmap),
//  handle buffers instead of raw hipMalloc, bin-by-nnz load balancing, a
//  Gauss-Seidel sweep variant (converges in ~1 pass), C API / CMake wiring,
//  and validation against a host reference (CSparse cs_symbolic).
// ============================================================================

#include "../conversion/rocsparse_csr2csc.hpp" // rocsparse::csr2csc_template
#include "../extra/csrgemm_symbolic_device.h" // rocsparse::insert_key, max_row_nnz
#include "../extra/rocsparse_csrgeam.hpp" // rocsparse::csrgeam_nnz_template
#include "../extra/rocsparse_csrgeam_symbolic.hpp" // rocsparse::csrgeam_symbolic_template
#include "rocsparse_control.hpp"
#include "rocsparse_primitives.hpp"
#include "rocsparse_utility.hpp"

namespace rocsparse
{
    enum rocsparse_fillin_mode
    {
        rocsparse_fillin_general   = 0, // ILU(0): use pattern(A) as-is
        rocsparse_fillin_symmetric = 1, // IC(0)/ILDLT(0): symmetrize first
    };

    static constexpr uint32_t SYMBFILLIN_HASHVAL = 11u; // collisions only

    // ------------------------------------------------------------------------
    //  One wavefront per row.  COUNT==true -> per-row nnz of S_i;
    //                          COUNT==false -> emit sorted column indices.
    //  Empty-slot sentinel is `m` (valid columns live in [0,m)).
    // ------------------------------------------------------------------------
    template <uint32_t BLOCKSIZE,
              uint32_t WFSIZE,
              uint32_t HASHSIZE,
              bool     COUNT,
              typename I,
              typename J>
    ROCSPARSE_KERNEL(BLOCKSIZE)
    void symbolic_fillin_row_kernel(J                     m,
                                    const I* __restrict__ csr_row_ptr,     // current F
                                    const J* __restrict__ csr_col_ind,     // current F
                                    I* __restrict__       row_nnz_out,     // COUNT only
                                    const I* __restrict__ csr_row_ptr_out, // FILL only
                                    J* __restrict__       csr_col_ind_out, // FILL only
                                    rocsparse_index_base  base)
    {
        static_assert(WFSIZE > 0 && (WFSIZE & (WFSIZE - 1)) == 0, "WFSIZE pow2");
        static_assert(HASHSIZE > 0 && (HASHSIZE & (HASHSIZE - 1)) == 0, "HASHSIZE pow2");

        const int lid = hipThreadIdx_x & (WFSIZE - 1);
        const int wid = hipThreadIdx_x / WFSIZE;
        const J   row = hipBlockIdx_x * (BLOCKSIZE / WFSIZE) + wid;

        __shared__ J stable[BLOCKSIZE / WFSIZE * HASHSIZE];
        J* const     table = &stable[wid * HASHSIZE];

        for(uint32_t i = lid; i < HASHSIZE; i += WFSIZE)
        {
            table[i] = m; // empty
        }
        __threadfence_block();

        if(row >= m)
        {
            return;
        }

        const I row_begin = csr_row_ptr[row] - base;
        const I row_end   = csr_row_ptr[row + 1] - base;

        // (1) own row entries: row_F(row)
        for(I j = row_begin + lid; j < row_end; j += WFSIZE)
        {
            insert_key<SYMBFILLIN_HASHVAL, HASHSIZE>(csr_col_ind[j] - base, table, m);
        }
        __threadfence_block();

        // (2) pivot propagation: for k in row_F(row) with k<row,
        //     merge { j in row_F(k) : j>k }
        for(I j = row_begin + lid; j < row_end; j += WFSIZE)
        {
            const J k = csr_col_ind[j] - base;
            if(k < row)
            {
                const I kb = csr_row_ptr[k] - base;
                const I ke = csr_row_ptr[k + 1] - base;
                for(I t = kb; t < ke; ++t)
                {
                    const J jcol = csr_col_ind[t] - base;
                    if(jcol > k)
                    {
                        insert_key<SYMBFILLIN_HASHVAL, HASHSIZE>(jcol, table, m);
                    }
                }
            }
        }
        __threadfence_block();

        if(COUNT)
        {
            I cnt = 0;
            for(uint32_t i = lid; i < HASHSIZE; i += WFSIZE)
            {
                cnt += __popcll(__ballot(table[i] < m));
            }
            if(lid == 0)
            {
                row_nnz_out[row] = cnt;
            }
        }
        else
        {
            const I out_begin = csr_row_ptr_out[row] - base;
            const I out_end   = csr_row_ptr_out[row + 1] - base;
            for(uint32_t i = lid; i < HASHSIZE; i += WFSIZE)
            {
                const J col = table[i];
                if(col >= m)
                {
                    continue;
                }
                I idx = out_begin;
                for(uint32_t h = 0; h < HASHSIZE; ++h)
                {
                    if(col > table[h])
                    {
                        ++idx;
                    }
                }
                if(idx >= out_begin && idx < out_end)
                {
                    csr_col_ind_out[idx] = col + base;
                }
            }
        }
    }

    template <bool COUNT, typename I, typename J>
    rocsparse_status symbolic_fillin_launch(rocsparse_handle     handle,
                                            J                    m,
                                            uint32_t             hashsize,
                                            const I*             csr_row_ptr,
                                            const J*             csr_col_ind,
                                            I*                   row_nnz_out,
                                            const I*             csr_row_ptr_out,
                                            J*                   csr_col_ind_out,
                                            rocsparse_index_base base)
    {
        constexpr uint32_t BLOCKSIZE      = 128;
        constexpr uint32_t WFSIZE         = 64;
        const uint32_t     rows_per_block = BLOCKSIZE / WFSIZE;
        const dim3         blocks((m - 1) / rows_per_block + 1);
        const dim3         threads(BLOCKSIZE);

#define LAUNCH_SYMBFILLIN(HS)                                                          \
    RETURN_IF_HIPLAUNCHKERNELGGL_ERROR(                                                \
        (rocsparse::symbolic_fillin_row_kernel<BLOCKSIZE, WFSIZE, HS, COUNT, I, J>),   \
        blocks, threads, 0, handle->stream, m, csr_row_ptr, csr_col_ind, row_nnz_out,  \
        csr_row_ptr_out, csr_col_ind_out, base)

        switch(hashsize)
        {
        case 16:
        case 32: LAUNCH_SYMBFILLIN(32); break;
        case 64: LAUNCH_SYMBFILLIN(64); break;
        case 128: LAUNCH_SYMBFILLIN(128); break;
        case 256: LAUNCH_SYMBFILLIN(256); break;
        case 512: LAUNCH_SYMBFILLIN(512); break;
        case 1024: LAUNCH_SYMBFILLIN(1024); break;
        case 2048: LAUNCH_SYMBFILLIN(2048); break;
        case 4096: LAUNCH_SYMBFILLIN(4096); break;
        case 8192: LAUNCH_SYMBFILLIN(8192); break;
        default:
            // TODO: rows > 8192 -> csrgemm_symbolic_fill_block_per_row_multipass_device
            return rocsparse_status_internal_error;
        }
#undef LAUNCH_SYMBFILLIN
        return rocsparse_status_success;
    }

    static inline uint32_t symbolic_fillin_next_pow2(size_t x)
    {
        uint32_t p = 16;
        while(p < x && p < (1u << 30))
        {
            p <<= 1;
        }
        return p;
    }

    template <typename I, typename J>
    rocsparse_status symbolic_fillin_max_row_nnz(rocsparse_handle handle,
                                                 J                m,
                                                 const I*         csr_row_ptr,
                                                 I*               max_row /*host*/)
    {
        constexpr uint32_t BLOCKSIZE = 256;
        const uint32_t     nblocks   = (m - 1) / BLOCKSIZE + 1;

        I* workspace = nullptr;
        RETURN_IF_HIP_ERROR(rocsparse_hipMalloc((void**)&workspace, sizeof(I) * nblocks));

        RETURN_IF_HIPLAUNCHKERNELGGL_ERROR(
            (rocsparse::csrgemm_symbolic_max_row_nnz_part1<BLOCKSIZE, I, J>),
            dim3(nblocks), dim3(BLOCKSIZE), 0, handle->stream, m, csr_row_ptr, workspace);
        // TODO: handle nblocks > BLOCKSIZE (loop / grid-stride part2).
        RETURN_IF_HIPLAUNCHKERNELGGL_ERROR((rocsparse::csrgemm_symbolic_max_row_nnz_part2<BLOCKSIZE, I>),
                                           dim3(1), dim3(BLOCKSIZE), 0, handle->stream, workspace);

        RETURN_IF_HIP_ERROR(
            hipMemcpyAsync(max_row, workspace, sizeof(I), hipMemcpyDeviceToHost, handle->stream));
        RETURN_IF_HIP_ERROR(hipStreamSynchronize(handle->stream));
        RETURN_IF_HIP_ERROR(rocsparse_hipFree(workspace));
        return rocsparse_status_success;
    }

    // ------------------------------------------------------------------------
    //  F0 for the SYMMETRIC modes: F0 = pattern(A) U pattern(A^T).
    //  Skip this entirely (use A directly) when A is already stored as a full
    //  symmetric pattern, or for ROCSPARSE_FILLIN_GENERAL (LU).
    //
    //  Sketch only -- wiring of descriptors / buffers is left as TODO:
    //    1. At = csr2csc_template(A)            (pattern transpose; values ignored)
    //    2. csrgeam_nnz_template(A, At)         -> row_ptr_F0, nnz_F0
    //    3. csrgeam_symbolic_template(A, At)    -> col_ind_F0
    // ------------------------------------------------------------------------
    template <typename I, typename J>
    rocsparse_status symbolic_fillin_symmetrize(rocsparse_handle /*handle*/,
                                                J /*m*/,
                                                I /*nnz_A*/,
                                                const I* /*csr_row_ptr_A*/,
                                                const J* /*csr_col_ind_A*/,
                                                rocsparse_index_base /*base*/,
                                                I** /*out_row_ptr*/,
                                                J** /*out_col_ind*/,
                                                I* /*out_nnz*/)
    {
        // TODO: implement using csr2csc_template + csrgeam_nnz_template
        //       + csrgeam_symbolic_template (see sketch above).
        return rocsparse_status_not_implemented;
    }

    // ------------------------------------------------------------------------
    //  Numeric scatter: fill val_F (size nnz_F) so that
    //      val_F[(i,j)] = A(i,j) if (i,j) in pattern(A), else 0.
    //  One wavefront per row; binary search of each F column in the A row
    //  (both CSR rows assumed sorted ascending).  Run AFTER the pattern F is
    //  known; the caller then runs csric0 / csrildlt0 / csrilu0 on (F, val_F).
    // ------------------------------------------------------------------------
    template <uint32_t BLOCKSIZE, uint32_t WFSIZE, typename T, typename I, typename J>
    ROCSPARSE_KERNEL(BLOCKSIZE)
    void symbolic_fillin_scatter_kernel(J                     m,
                                        const I* __restrict__ csr_row_ptr_A,
                                        const J* __restrict__ csr_col_ind_A,
                                        const T* __restrict__ csr_val_A,
                                        const I* __restrict__ csr_row_ptr_F,
                                        const J* __restrict__ csr_col_ind_F,
                                        T* __restrict__       csr_val_F,
                                        rocsparse_index_base  base)
    {
        const int lid = hipThreadIdx_x & (WFSIZE - 1);
        const int wid = hipThreadIdx_x / WFSIZE;
        const J   row = hipBlockIdx_x * (BLOCKSIZE / WFSIZE) + wid;
        if(row >= m)
        {
            return;
        }

        const I ab = csr_row_ptr_A[row] - base;
        const I ae = csr_row_ptr_A[row + 1] - base;
        const I fb = csr_row_ptr_F[row] - base;
        const I fe = csr_row_ptr_F[row + 1] - base;

        for(I p = fb + lid; p < fe; p += WFSIZE)
        {
            const J col = csr_col_ind_F[p] - base;
            T       v   = static_cast<T>(0);
            I       lo = ab, hi = ae; // binary search col in A's row
            while(lo < hi)
            {
                const I mid = lo + (hi - lo) / 2;
                const J c   = csr_col_ind_A[mid] - base;
                if(c == col)
                {
                    v = csr_val_A[mid];
                    break;
                }
                else if(c < col)
                {
                    lo = mid + 1;
                }
                else
                {
                    hi = mid;
                }
            }
            csr_val_F[p] = v;
        }
    }

    template <typename T, typename I, typename J>
    rocsparse_status symbolic_fillin_scatter_values(rocsparse_handle     handle,
                                                    J                    m,
                                                    const I*             csr_row_ptr_A,
                                                    const J*             csr_col_ind_A,
                                                    const T*             csr_val_A,
                                                    const I*             csr_row_ptr_F,
                                                    const J*             csr_col_ind_F,
                                                    T*                   csr_val_F,
                                                    rocsparse_index_base base)
    {
        constexpr uint32_t BLOCKSIZE = 128;
        constexpr uint32_t WFSIZE    = 64;
        const dim3         blocks((m - 1) / (BLOCKSIZE / WFSIZE) + 1);
        RETURN_IF_HIPLAUNCHKERNELGGL_ERROR(
            (rocsparse::symbolic_fillin_scatter_kernel<BLOCKSIZE, WFSIZE, T, I, J>), blocks,
            dim3(BLOCKSIZE), 0, handle->stream, m, csr_row_ptr_A, csr_col_ind_A, csr_val_A,
            csr_row_ptr_F, csr_col_ind_F, csr_val_F, base);
        return rocsparse_status_success;
    }

    // ------------------------------------------------------------------------
    //  Main entry: complete fill pattern F.
    //  On success *out_* point to freshly hipMalloc'ed device buffers owned by
    //  the caller (v0 memory policy).
    // ------------------------------------------------------------------------
    template <typename I, typename J>
    rocsparse_status symbolic_fillin_computation_template(rocsparse_handle      handle,
                                                          rocsparse_fillin_mode mode,
                                                          J                     m,
                                                          I                     nnz_A,
                                                          const I*              csr_row_ptr_A,
                                                          const J*              csr_col_ind_A,
                                                          rocsparse_index_base  base,
                                                          I**                   out_row_ptr,
                                                          J**                   out_col_ind,
                                                          I*                    out_nnz)
    {
        hipStream_t stream = handle->stream;

        // ---- build F0 -------------------------------------------------------
        I* F_row = nullptr;
        J* F_col = nullptr;
        I  F_nnz = 0;

        // F0 = pattern(A) as-is, for BOTH modes.  The fixed point is identical;
        // `mode` is kept only as a semantic hint for the downstream numeric
        // stage (IC/LDL^T vs LU).
        //
        // NOTE for the symmetric modes: the input must be a FULL symmetric
        // pattern (both triangles).  If you only store half of A, or A is not
        // structurally symmetric, call symbolic_fillin_symmetrize() first
        // (pattern(A) U pattern(A^T)) and pass its result here.
        RETURN_IF_HIP_ERROR(rocsparse_hipMalloc((void**)&F_row, sizeof(I) * (m + 1)));
        RETURN_IF_HIP_ERROR(rocsparse_hipMalloc((void**)&F_col, sizeof(J) * nnz_A));
        RETURN_IF_HIP_ERROR(hipMemcpyAsync(
            F_row, csr_row_ptr_A, sizeof(I) * (m + 1), hipMemcpyDeviceToDevice, stream));
        RETURN_IF_HIP_ERROR(hipMemcpyAsync(
            F_col, csr_col_ind_A, sizeof(J) * nnz_A, hipMemcpyDeviceToDevice, stream));
        F_nnz = nnz_A;
        (void)mode;

        // ---- scratch + scan buffer -----------------------------------------
        I* next_row = nullptr;
        RETURN_IF_HIP_ERROR(rocsparse_hipMalloc((void**)&next_row, sizeof(I) * (m + 1)));

        size_t scan_bytes = 0;
        RETURN_IF_ROCSPARSE_ERROR((rocsparse::primitives::exclusive_scan_buffer_size<I, I>(
            handle, static_cast<I>(base), m + 1, &scan_bytes)));
        void* scan_buf = nullptr;
        RETURN_IF_HIP_ERROR(rocsparse_hipMalloc(&scan_buf, scan_bytes));

        // ---- fixed-point loop ----------------------------------------------
        const int MAX_ITERS = 1000; // safety net; ~tree-height iterations
        for(int it = 0; it < MAX_ITERS; ++it)
        {
            I max_row = 0;
            RETURN_IF_ROCSPARSE_ERROR(
                (symbolic_fillin_max_row_nnz<I, J>(handle, m, F_row, &max_row)));
            const uint32_t hashsize
                = symbolic_fillin_next_pow2(static_cast<size_t>(max_row) * 2);

            // pass 1: count
            RETURN_IF_ROCSPARSE_ERROR((symbolic_fillin_launch<true, I, J>(
                handle, m, hashsize, F_row, F_col, next_row, nullptr, nullptr, base)));

            // prefix sum -> next row_ptr
            RETURN_IF_ROCSPARSE_ERROR((rocsparse::primitives::exclusive_scan<I, I>(
                handle, next_row, next_row, static_cast<I>(base), m + 1, scan_bytes, scan_buf)));

            I new_nnz = 0;
            RETURN_IF_HIP_ERROR(
                hipMemcpyAsync(&new_nnz, next_row + m, sizeof(I), hipMemcpyDeviceToHost, stream));
            RETURN_IF_HIP_ERROR(hipStreamSynchronize(stream));
            new_nnz -= static_cast<I>(base);

            if(new_nnz == F_nnz)
            {
                break; // converged
            }

            // pass 2: fill
            J* new_col = nullptr;
            RETURN_IF_HIP_ERROR(rocsparse_hipMalloc((void**)&new_col, sizeof(J) * new_nnz));
            RETURN_IF_ROCSPARSE_ERROR((symbolic_fillin_launch<false, I, J>(
                handle, m, hashsize, F_row, F_col, nullptr, next_row, new_col, base)));

            RETURN_IF_HIP_ERROR(rocsparse_hipFree(F_col));
            F_col = new_col;
            RETURN_IF_HIP_ERROR(hipMemcpyAsync(
                F_row, next_row, sizeof(I) * (m + 1), hipMemcpyDeviceToDevice, stream));
            F_nnz = new_nnz;
        }

        RETURN_IF_HIP_ERROR(rocsparse_hipFree(next_row));
        RETURN_IF_HIP_ERROR(rocsparse_hipFree(scan_buf));

        *out_row_ptr = F_row;
        *out_col_ind = F_col;
        *out_nnz     = F_nnz;
        return rocsparse_status_success;
    }
}
