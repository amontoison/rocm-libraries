/*! \file */
/* ************************************************************************
 * Copyright (C) 2021-2026 Advanced Micro Devices, Inc. All rights Reserved.
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

#include "bsrsm_device.h"
#include "bsrsm_device_large.h"
#include "rocsparse_bsrsm.hpp"
#include "rocsparse_common.h"
#include "rocsparse_control.hpp"
#include "rocsparse_utility.hpp"

namespace rocsparse
{
#define LAUNCH_BSRSM_GTHR_DIM(bsize, wfsize, dim)                                            \
    RETURN_IF_HIPLAUNCHKERNELGGL_ERROR((rocsparse::bsr_gather<wfsize, bsize / wfsize, dim>), \
                                       dim3((wfsize * nnzb - 1) / bsize + 1),                \
                                       dim3(wfsize, bsize / wfsize),                         \
                                       0,                                                    \
                                       stream,                                               \
                                       dir,                                                  \
                                       nnzb,                                                 \
                                       (const I*)trm_info->get_transposed_perm(),            \
                                       bsr_val,                                              \
                                       bsrt_val,                                             \
                                       (I)block_dim)

#define LAUNCH_BSRSM_GTHR(bsize, wfsize, dim) \
    if(dim <= 2)                              \
    {                                         \
        LAUNCH_BSRSM_GTHR_DIM(bsize, 4, 2);   \
    }                                         \
    else if(dim <= 4)                         \
    {                                         \
        LAUNCH_BSRSM_GTHR_DIM(bsize, 16, 4);  \
    }                                         \
    else if(wfsize == 32)                     \
    {                                         \
        LAUNCH_BSRSM_GTHR_DIM(bsize, 16, 4);  \
    }                                         \
    else                                      \
    {                                         \
        LAUNCH_BSRSM_GTHR_DIM(bsize, 64, 8);  \
    }

    template <uint32_t BLOCKSIZE, typename J, typename T>
    ROCSPARSE_KERNEL(BLOCKSIZE)
    void bsrsm_copy_scale(J             m,
                          rocsparse_int n,
                          ROCSPARSE_DEVICE_HOST_SCALAR_PARAMS(T, alpha),
                          const T* B,
                          int64_t  ldb,
                          T*       X,
                          int64_t  ldx,
                          bool     is_host_mode)
    {
        ROCSPARSE_DEVICE_HOST_SCALAR_GET(alpha);
        rocsparse::bsrsm_copy_scale_device(m, n, alpha, B, ldb, X, ldx);
    }

    template <typename I, typename J, typename T>
    rocsparse_status bsrsm_solve_template_large(rocsparse_handle          handle,
                                                rocsparse_direction       dir,
                                                rocsparse_operation       trans_A,
                                                rocsparse_operation       trans_X,
                                                J                         mb,
                                                rocsparse_int             nrhs,
                                                I                         nnzb,
                                                const T*                  alpha,
                                                const rocsparse_mat_descr descr,
                                                const T*                  bsr_val,
                                                const I*                  bsr_row_ptr,
                                                const J*                  bsr_col_ind,
                                                rocsparse_int             block_dim,
                                                rocsparse_mat_info        info,
                                                const T*                  B,
                                                int64_t                   ldb,
                                                T*                        X,
                                                int64_t                   ldx,
                                                void*                     temp_buffer)
    {
        ROCSPARSE_ROUTINE_TRACE;

#define LAUNCH_LARGE_KERNEL(K_, M_, S_)                                            \
    dim3 bsrsm_blocks(((nrhs - 1) / NCOL + 1) * mb);                               \
    dim3 bsrsm_threads(NCOL* M_);                                                  \
    RETURN_IF_HIPLAUNCHKERNELGGL_ERROR((K_<NCOL * M_, NCOL, S_>),                  \
                                       bsrsm_blocks,                               \
                                       bsrsm_threads,                              \
                                       0,                                          \
                                       stream,                                     \
                                       mb,                                         \
                                       nrhs,                                       \
                                       local_bsr_row_ptr,                          \
                                       local_bsr_col_ind,                          \
                                       local_bsr_val,                              \
                                       block_dim,                                  \
                                       Xt,                                         \
                                       ldimX,                                      \
                                       done_array,                                 \
                                       (const J*)trm_info->get_row_map(),          \
                                       (J*)info->get_bsrsm_info()->get_position(), \
                                       descr->base,                                \
                                       descr->diag_type,                           \
                                       dir);

        hipStream_t stream = handle->stream;

        // Buffer
        char* ptr = reinterpret_cast<char*>(temp_buffer);

        ptr += 256;

        // 16 columns per block seem to work very well
        static constexpr uint32_t NCOL = 16;

        const int narrays = (nrhs - 1) / NCOL + 1;

        // done_array
        int* done_array = reinterpret_cast<int*>(ptr);
        ptr += ((sizeof(int) * size_t(mb) * narrays - 1) / 256 + 1) * 256;

        // Temporary array to store transpose of X
        T* Xt = X;
        if(trans_X == rocsparse_operation_none)
        {
            Xt = reinterpret_cast<T*>(ptr);
            ptr += ((sizeof(T) * size_t(mb) * block_dim * nrhs - 1) / 256 + 1) * 256;
        }

        // Initialize buffers
        RETURN_IF_HIP_ERROR(
            rocsparse_hipMemsetAsync(done_array, 0, sizeof(int) * mb * narrays, stream));

        auto bsrsm_info = info->get_bsrsm_info();
        auto trm_info   = info->get_bsrsm_info(trans_A, descr->fill_mode);

        // If diag type is unit, re-initialize zero pivot to remove structural zeros
        if(descr->diag_type == rocsparse_diag_type_unit)
        {
            static const J max = std::numeric_limits<J>::max();
            RETURN_IF_HIP_ERROR(rocsparse_hipMemcpyAsync(
                bsrsm_info->get_position(), &max, sizeof(J), hipMemcpyHostToDevice, stream));
        }

        rocsparse_fill_mode fill_mode = descr->fill_mode;

        // Transpose X if X is not transposed yet to improve performance
        int64_t ldimX = ldx;
        if(trans_X == rocsparse_operation_none)
        {
            // Leading dimension for transposed X
            ldimX = nrhs;

            if(handle->pointer_mode == rocsparse_pointer_mode_device)
            {
                RETURN_IF_ROCSPARSE_ERROR(
                    rocsparse::dense_transpose(handle,
                                               static_cast<int64_t>(mb) * block_dim,
                                               static_cast<int64_t>(nrhs),
                                               alpha,
                                               B,
                                               ldb,
                                               Xt,
                                               ldimX));
            }
            else
            {
                RETURN_IF_ROCSPARSE_ERROR(
                    rocsparse::dense_transpose(handle,
                                               static_cast<int64_t>(mb) * block_dim,
                                               static_cast<int64_t>(nrhs),
                                               *alpha,
                                               B,
                                               ldb,
                                               Xt,
                                               ldimX));
            }
        }
        else
        {
            // Copy B into X and scale it with alpha
            RETURN_IF_HIPLAUNCHKERNELGGL_ERROR((rocsparse::bsrsm_copy_scale<1024>),
                                               dim3((mb * block_dim - 1) / 1024 + 1),
                                               dim3(1024),
                                               0,
                                               stream,
                                               mb * block_dim,
                                               nrhs,
                                               ROCSPARSE_DEVICE_HOST_SCALAR_ARGS(handle, alpha),
                                               B,
                                               ldb,
                                               X,
                                               ldx,
                                               handle->pointer_mode == rocsparse_pointer_mode_host);
        }

        // Pointers to differentiate between transpose mode
        const I* local_bsr_row_ptr = bsr_row_ptr;
        const J* local_bsr_col_ind = bsr_col_ind;
        const T* local_bsr_val     = bsr_val;

        // When computing transposed triangular solve, we first need to update the
        // transposed matrix values
        if(trans_A == rocsparse_operation_transpose)
        {
            T* bsrt_val = reinterpret_cast<T*>(ptr);

            LAUNCH_BSRSM_GTHR(256, 64, block_dim);

            local_bsr_row_ptr = (const I*)trm_info->get_transposed_row_ptr();
            local_bsr_col_ind = (const J*)trm_info->get_transposed_col_ind();
            local_bsr_val     = (const T*)bsrt_val;

            fill_mode = (fill_mode == rocsparse_fill_mode_lower) ? rocsparse_fill_mode_upper
                                                                 : rocsparse_fill_mode_lower;
        }

        // Determine gcn_arch and ASIC revision
        const std::string gcn_arch_name = rocsparse::handle_get_arch_name(handle);
        const int         asicRev       = handle->asic_rev;
        const int         wfSize        = handle->wavefront_size;

        // gfx908 A0/1
        if(gcn_arch_name == rocpsarse_arch_names::gfx908 && asicRev < 2)
        {
            if(fill_mode == rocsparse_fill_mode_upper)
            {
                LAUNCH_LARGE_KERNEL(rocsparse::bsrsm_upper_large_kernel, 16, true);
            }
            else
            {
                LAUNCH_LARGE_KERNEL(rocsparse::bsrsm_lower_large_kernel, 16, true);
            }
        }
        else
        {
            // Select tuned kernel

            uint32_t nbsr = rocsparse::max(4U, fnp2(block_dim));

            while(nbsr > wfSize)
            {
                nbsr >>= 1;
            }

            switch(nbsr)
            {
#define DEFINE_CASE(i)                                               \
    case i:                                                          \
    {                                                                \
        if(fill_mode == rocsparse_fill_mode_upper)                   \
        {                                                            \
            LAUNCH_LARGE_KERNEL(bsrsm_upper_large_kernel, i, false); \
        }                                                            \
        else                                                         \
        {                                                            \
            LAUNCH_LARGE_KERNEL(bsrsm_lower_large_kernel, i, false); \
        }                                                            \
        break;                                                       \
    }

                DEFINE_CASE(4);
                DEFINE_CASE(8);
                DEFINE_CASE(16);
                DEFINE_CASE(32);
                DEFINE_CASE(64);
#undef DEFINE_CASE
            }
        }
#undef LAUNCH_LARGE_KERNEL

        // Transpose X back if X was not initially transposed
        if(trans_X == rocsparse_operation_none)
        {
            RETURN_IF_ROCSPARSE_ERROR(
                rocsparse::dense_transpose_back(handle,
                                                static_cast<int64_t>(mb) * block_dim,
                                                static_cast<int64_t>(nrhs),
                                                Xt,
                                                ldimX,
                                                X,
                                                ldx));
        }

        return rocsparse_status_success;
    }
}

#define INSTANTIATE(I, J, T)                                                  \
    template rocsparse_status rocsparse::bsrsm_solve_template_large<I, J, T>( \
        rocsparse_handle          handle,                                     \
        rocsparse_direction       dir,                                        \
        rocsparse_operation       trans_A,                                    \
        rocsparse_operation       trans_X,                                    \
        J                         mb,                                         \
        rocsparse_int             nrhs,                                       \
        I                         nnzb,                                       \
        const T*                  alpha,                                      \
        const rocsparse_mat_descr descr,                                      \
        const T*                  bsr_val,                                    \
        const I*                  bsr_row_ptr,                                \
        const J*                  bsr_col_ind,                                \
        rocsparse_int             block_dim,                                  \
        rocsparse_mat_info        info,                                       \
        const T*                  B,                                          \
        int64_t                   ldb,                                        \
        T*                        X,                                          \
        int64_t                   ldx,                                        \
        void*                     temp_buffer)

#define INSTANTIATE_IJ(I, J)                    \
    INSTANTIATE(I, J, float);                   \
    INSTANTIATE(I, J, double);                  \
    INSTANTIATE(I, J, rocsparse_float_complex); \
    INSTANTIATE(I, J, rocsparse_double_complex)

INSTANTIATE_IJ(int32_t, int32_t);
INSTANTIATE_IJ(int64_t, int32_t);
INSTANTIATE_IJ(int64_t, int64_t);

#undef INSTANTIATE_IJ
#undef INSTANTIATE
