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

#include "../csric0/rocsparse_csric0.hpp"
#include "rocsparse_csrildlt0.hpp"
#include "rocsparse_utility.hpp"

rocsparse_status rocsparse::csrildlt0_analysis(rocsparse_handle          handle,
                                               rocsparse_spmat_descr     A,
                                               rocsparse_analysis_policy analysis,
                                               rocsparse_csrildlt0_info* p_csrildlt0_info,
                                               void*                     temp_buffer)
{
    ROCSPARSE_ROUTINE_TRACE;

    if(A->rows == 0)
    {
        return rocsparse_status_success;
    }

    auto info           = A->info;
    auto csrildlt0_info = p_csrildlt0_info[0];

    if(analysis == rocsparse_analysis_policy_reuse)
    {
        const rocsparse_operation op    = rocsparse_operation_none;
        const rocsparse_operation trans = rocsparse_operation_transpose;
        const rocsparse_fill_mode lower = rocsparse_fill_mode_lower;
        const rocsparse_fill_mode upper = rocsparse_fill_mode_upper;
        auto                      dst   = info->get_csrildlt0_info();
        bool                      found;
        RETURN_IF_ROCSPARSE_ERROR(dst->reuse(op,
                                             lower,
                                             {{dst, op, lower},
                                              {info->get_csric0_info(), op, lower},
                                              {info->get_csrilu0_info(), op, lower},
                                              {info->get_csrsv_info(), op, lower},
                                              {info->get_csrsm_info(), op, lower},
                                              {info->get_csrsv_info(), trans, upper},
                                              {info->get_csrsm_info(), trans, upper}},
                                             handle->stream,
                                             &found));
        if(found)
        {
            return rocsparse_status_success;
        }
    }

    if(csrildlt0_info == nullptr)
    {
        csrildlt0_info      = new _rocsparse_csrildlt0_info();
        p_csrildlt0_info[0] = csrildlt0_info;
    }

    RETURN_IF_ROCSPARSE_ERROR(csrildlt0_info->recreate(rocsparse_operation_none,
                                                       rocsparse_fill_mode_lower,
                                                       handle,
                                                       rocsparse_operation_none,
                                                       A->rows,
                                                       A->nnz,
                                                       A->descr,
                                                       A->data_type,
                                                       A->const_val_data,
                                                       A->row_type,
                                                       A->const_row_data,
                                                       A->col_type,
                                                       A->const_col_data,
                                                       temp_buffer));

    return rocsparse_status_success;
}
