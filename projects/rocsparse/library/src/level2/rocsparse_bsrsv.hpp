/*! \file */
/* ************************************************************************
 * Copyright (C) 2020-2025 Advanced Micro Devices, Inc. All rights Reserved.
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

#include "rocsparse_handle.hpp"

namespace rocsparse
{
    template <typename I, typename J, typename T>
    rocsparse_status bsrsv_analysis_template(rocsparse_handle          handle,
                                             rocsparse_direction       dir,
                                             rocsparse_operation       trans,
                                             J                         mb,
                                             I                         nnzb,
                                             const rocsparse_mat_descr descr,
                                             const T*                  bsr_val,
                                             const I*                  bsr_row_ptr,
                                             const J*                  bsr_col_ind,
                                             rocsparse_int             block_dim,
                                             rocsparse_mat_info        info,
                                             rocsparse_analysis_policy analysis,
                                             rocsparse_solve_policy    solve,
                                             void*                     temp_buffer);

    template <typename I, typename J, typename T>
    rocsparse_status bsrsv_solve_template(rocsparse_handle          handle,
                                          rocsparse_direction       dir,
                                          rocsparse_operation       trans,
                                          J                         mb,
                                          I                         nnzb,
                                          const T*                  alpha,
                                          const rocsparse_mat_descr descr,
                                          const T*                  bsr_val,
                                          const I*                  bsr_row_ptr,
                                          const J*                  bsr_col_ind,
                                          rocsparse_int             block_dim,
                                          rocsparse_mat_info        info,
                                          const T*                  x,
                                          T*                        y,
                                          rocsparse_solve_policy    policy,
                                          void*                     temp_buffer);

    //
    // Generic (spmat-descriptor based) entry points used by rocsparse_sptrsv.
    // They dispatch on the spmat index (i32/i64) and value types.
    //
    rocsparse_status bsrsv_buffer_size(rocsparse_handle            handle,
                                       rocsparse_operation         trans,
                                       rocsparse_const_spmat_descr A,
                                       size_t*                     buffer_size);

    rocsparse_status bsrsv_analysis(rocsparse_handle            handle,
                                    rocsparse_operation         trans,
                                    rocsparse_const_spmat_descr A,
                                    rocsparse_analysis_policy   analysis_policy,
                                    rocsparse_solve_policy      solve_policy,
                                    void*                       temp_buffer);

    rocsparse_status bsrsv_solve(rocsparse_handle            handle,
                                 rocsparse_operation         trans,
                                 rocsparse_datatype          alpha_datatype,
                                 const void*                 alpha,
                                 rocsparse_const_spmat_descr A,
                                 rocsparse_const_dnvec_descr x,
                                 rocsparse_dnvec_descr       y,
                                 rocsparse_solve_policy      policy,
                                 void*                       temp_buffer);
}
