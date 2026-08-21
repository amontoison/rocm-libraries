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

#include "rocsparse_spildlt0_solve_scale.hpp"
#include "rocsparse_common.hpp"
#include "rocsparse_handle.hpp"
#include "rocsparse_utility.hpp"

namespace rocsparse
{
    template <uint32_t BLOCKSIZE, typename I, typename J, typename T>
    ROCSPARSE_KERNEL(BLOCKSIZE)
    void spildlt0_solve_diagonal_scale_kernel(J m,
                                              J nrhs,
                                              const I* __restrict__ csr_row_ptr,
                                              const J* __restrict__ csr_col_ind,
                                              const T* __restrict__ csr_val,
                                              rocsparse_index_base base,
                                              T* __restrict__ X,
                                              int64_t ld,
                                              bool    col_major)
    {
        const J row = blockIdx.x * BLOCKSIZE + threadIdx.x;
        if(row >= m)
        {
            return;
        }

        // Diagonal value D_row (the entry at column == row).
        T d = static_cast<T>(0);
        for(I j = csr_row_ptr[row] - base; j < csr_row_ptr[row + 1] - base; ++j)
        {
            if((csr_col_ind[j] - base) == row)
            {
                d = csr_val[j];
                break;
            }
        }

        for(J c = 0; c < nrhs; ++c)
        {
            const int64_t idx = col_major
                                    ? (static_cast<int64_t>(row) + static_cast<int64_t>(c) * ld)
                                    : (static_cast<int64_t>(row) * ld + static_cast<int64_t>(c));
            X[idx]            = X[idx] / d;
        }
    }

    template <typename I, typename J, typename T>
    static rocsparse_status spildlt0_solve_diagonal_scale_template(rocsparse_handle handle,
                                                                   rocsparse_const_spmat_descr A,
                                                                   rocsparse_dnmat_descr       X)
    {
        const J m    = static_cast<J>(A->rows);
        const J nrhs = static_cast<J>(X->cols);
        if(m == 0 || nrhs == 0)
        {
            return rocsparse_status_success;
        }

        constexpr uint32_t BLOCKSIZE = 256;
        const dim3         blocks((m - 1) / BLOCKSIZE + 1);
        const dim3         threads(BLOCKSIZE);
        const bool         col_major = (X->order == rocsparse_order_column);

        RETURN_IF_HIPLAUNCHKERNELGGL_ERROR(
            (rocsparse::spildlt0_solve_diagonal_scale_kernel<BLOCKSIZE, I, J, T>),
            blocks,
            threads,
            0,
            handle->stream,
            m,
            nrhs,
            reinterpret_cast<const I*>(A->const_row_data),
            reinterpret_cast<const J*>(A->const_col_data),
            reinterpret_cast<const T*>(A->const_val_data),
            A->idx_base,
            reinterpret_cast<T*>(X->values),
            X->ld,
            col_major);
        return rocsparse_status_success;
    }

    template <typename I, typename J>
    static rocsparse_status spildlt0_solve_diagonal_scale_dispatch_datatype(
        rocsparse_handle handle, rocsparse_const_spmat_descr A, rocsparse_dnmat_descr X)
    {
        switch(A->data_type)
        {
        case rocsparse_datatype_f32_r:
            return rocsparse::spildlt0_solve_diagonal_scale_template<I, J, float>(handle, A, X);
        case rocsparse_datatype_f64_r:
            return rocsparse::spildlt0_solve_diagonal_scale_template<I, J, double>(handle, A, X);
        case rocsparse_datatype_f32_c:
            return rocsparse::spildlt0_solve_diagonal_scale_template<I, J, rocsparse_float_complex>(
                handle, A, X);
        case rocsparse_datatype_f64_c:
            return rocsparse::
                spildlt0_solve_diagonal_scale_template<I, J, rocsparse_double_complex>(
                    handle, A, X);
        default:
            // LCOV_EXCL_START
            RETURN_IF_ROCSPARSE_ERROR(rocsparse_status_not_implemented);
            // LCOV_EXCL_STOP
        }
    }
}

rocsparse_status rocsparse::spildlt0_solve_diagonal_scale(rocsparse_handle            handle,
                                                          rocsparse_const_spmat_descr A,
                                                          rocsparse_dnmat_descr       X)
{
    ROCSPARSE_ROUTINE_TRACE;

    if(A->row_type == rocsparse_indextype_i32 && A->col_type == rocsparse_indextype_i32)
    {
        return rocsparse::spildlt0_solve_diagonal_scale_dispatch_datatype<int32_t, int32_t>(
            handle, A, X);
    }
    else if(A->row_type == rocsparse_indextype_i64 && A->col_type == rocsparse_indextype_i32)
    {
        return rocsparse::spildlt0_solve_diagonal_scale_dispatch_datatype<int64_t, int32_t>(
            handle, A, X);
    }
    else if(A->row_type == rocsparse_indextype_i64 && A->col_type == rocsparse_indextype_i64)
    {
        return rocsparse::spildlt0_solve_diagonal_scale_dispatch_datatype<int64_t, int64_t>(
            handle, A, X);
    }

    // LCOV_EXCL_START
    RETURN_IF_ROCSPARSE_ERROR(rocsparse_status_not_implemented);
    // LCOV_EXCL_STOP
}
