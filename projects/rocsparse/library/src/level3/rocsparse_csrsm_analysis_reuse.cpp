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

#include "rocsparse_csrsm_analysis_reuse.hpp"

#include "rocsparse_assign_async.hpp"
#include "rocsparse_mat_info.hpp"
#include "rocsparse_utility.hpp"

rocsparse_status rocsparse::csrsm_analysis_reuse(rocsparse_handle          handle,
                                                 rocsparse_operation       trans,
                                                 const rocsparse_mat_descr descr,
                                                 rocsparse_mat_info        info,
                                                 bool*                     p_reused)
{
    ROCSPARSE_ROUTINE_TRACE;

    const bool lower_none
        = (descr->fill_mode == rocsparse_fill_mode_lower) && (trans == rocsparse_operation_none);

    rocsparse_csrsm_info   csrsm_info = info->get_csrsm_info();
    rocsparse::trm_data_t* source     = nullptr;
    if(csrsm_info->get(trans, descr->fill_mode) != nullptr)
    {
        source = csrsm_info;
    }
    else if(lower_none && info->get_csrilu0_info(trans, descr->fill_mode) != nullptr)
    {
        source = info->get_csrilu0_info();
    }
    else if(lower_none && info->get_csric0_info(trans, descr->fill_mode) != nullptr)
    {
        source = info->get_csric0_info();
    }
    else if(info->get_csrsv_info(trans, descr->fill_mode) != nullptr)
    {
        source = info->get_csrsv_info();
    }

    p_reused[0] = (source != nullptr);
    if(source == nullptr)
    {
        return rocsparse_status_success;
    }

    if(source != csrsm_info)
    {
        csrsm_info->set(trans, descr->fill_mode, source->get(trans, descr->fill_mode));
        RETURN_IF_ROCSPARSE_ERROR(csrsm_info->copy_pivot_info_async(source, handle->stream));
    }

    // The source may never have created its zero pivot.
    if(csrsm_info->get_position() == nullptr)
    {
        RETURN_IF_ROCSPARSE_ERROR(csrsm_info->create_zero_pivot_async(
            csrsm_info->get(trans, descr->fill_mode)->get_index_indextype(), handle->stream));
    }

    // A unit diagonal has no structural zero pivot.
    if(descr->diag_type == rocsparse_diag_type_unit)
    {
        RETURN_IF_ROCSPARSE_ERROR(rocsparse::assign_max_async(
            1, csrsm_info->get_indextype(), csrsm_info->get_position(), handle->stream));
    }
    return rocsparse_status_success;
}
