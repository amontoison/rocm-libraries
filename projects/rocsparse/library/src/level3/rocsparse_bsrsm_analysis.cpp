/*! \file */
/* ************************************************************************
 * Copyright (C) 2021-2025 Advanced Micro Devices, Inc. All rights Reserved.
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

#include "internal/level3/rocsparse_bsrsm.h"
#include "rocsparse_control.hpp"
#include "rocsparse_spmat_descr.hpp"
#include "rocsparse_utility.hpp"

#include "../level2/rocsparse_csrsv.hpp"
#include "rocsparse_bsrsm.hpp"

rocsparse_status rocsparse::bsrsm_analysis_quickreturn(rocsparse_handle          handle,
                                                       rocsparse_direction       dir,
                                                       rocsparse_operation       trans_A,
                                                       rocsparse_operation       trans_X,
                                                       rocsparse_int             mb,
                                                       rocsparse_int             nrhs,
                                                       rocsparse_int             nnzb,
                                                       const rocsparse_mat_descr descr,
                                                       const void*               bsr_val,
                                                       const rocsparse_int*      bsr_row_ptr,
                                                       const rocsparse_int*      bsr_col_ind,
                                                       rocsparse_int             block_dim,
                                                       rocsparse_mat_info        info,
                                                       rocsparse_analysis_policy analysis,
                                                       rocsparse_solve_policy    solve,
                                                       void*                     temp_buffer)
{
    ROCSPARSE_ROUTINE_TRACE;

    if(mb == 0 || nrhs == 0)
    {
        return rocsparse_status_success;
    }

    return rocsparse_status_continue;
}

namespace rocsparse
{
    static rocsparse_status bsrsm_analysis_checkarg(rocsparse_handle          handle, //0
                                                    rocsparse_direction       dir, //1
                                                    rocsparse_operation       trans_A, //2
                                                    rocsparse_operation       trans_X, //3
                                                    rocsparse_int             mb, //4
                                                    rocsparse_int             nrhs, //5
                                                    rocsparse_int             nnzb, //6
                                                    const rocsparse_mat_descr descr, //7
                                                    const void*               bsr_val, //8
                                                    const rocsparse_int*      bsr_row_ptr, //9
                                                    const rocsparse_int*      bsr_col_ind, //10
                                                    rocsparse_int             block_dim, //11
                                                    rocsparse_mat_info        info, //12
                                                    rocsparse_analysis_policy analysis, //13
                                                    rocsparse_solve_policy    solve, //14
                                                    void*                     temp_buffer) //15
    {
        ROCSPARSE_ROUTINE_TRACE;

        ROCSPARSE_CHECKARG_HANDLE(0, handle);
        ROCSPARSE_CHECKARG_ENUM(1, dir);
        ROCSPARSE_CHECKARG_ENUM(2, trans_A);
        ROCSPARSE_CHECKARG_ENUM(3, trans_X);
        ROCSPARSE_CHECKARG_SIZE(4, mb);
        ROCSPARSE_CHECKARG_SIZE(5, nrhs);

        const rocsparse_status status = rocsparse::bsrsm_analysis_quickreturn(handle,
                                                                              dir,
                                                                              trans_A,
                                                                              trans_X,
                                                                              mb,
                                                                              nrhs,
                                                                              nnzb,
                                                                              descr,
                                                                              bsr_val,
                                                                              bsr_row_ptr,
                                                                              bsr_col_ind,
                                                                              block_dim,
                                                                              info,
                                                                              analysis,
                                                                              solve,
                                                                              temp_buffer);
        if(status != rocsparse_status_continue)
        {
            RETURN_IF_ROCSPARSE_ERROR(status);
            return rocsparse_status_success;
        }

        ROCSPARSE_CHECKARG_SIZE(6, nnzb);
        ROCSPARSE_CHECKARG_POINTER(7, descr);
        ROCSPARSE_CHECKARG(7,
                           descr,
                           (descr->type != rocsparse_matrix_type_general),
                           rocsparse_status_not_implemented);
        ROCSPARSE_CHECKARG(7,
                           descr,
                           (descr->storage_mode != rocsparse_storage_mode_sorted),
                           rocsparse_status_requires_sorted_storage);
        ROCSPARSE_CHECKARG_ARRAY(8, nnzb, bsr_val);
        ROCSPARSE_CHECKARG_ARRAY(9, mb, bsr_row_ptr);
        ROCSPARSE_CHECKARG_ARRAY(10, nnzb, bsr_col_ind);
        ROCSPARSE_CHECKARG_SIZE(11, block_dim);
        ROCSPARSE_CHECKARG(11, block_dim, (block_dim == 0), rocsparse_status_invalid_size);
        ROCSPARSE_CHECKARG_POINTER(12, info);
        ROCSPARSE_CHECKARG_ENUM(13, analysis);
        ROCSPARSE_CHECKARG_ENUM(14, solve);

        ROCSPARSE_CHECKARG_POINTER(15, temp_buffer);
        return rocsparse_status_continue;
    }
}

template <typename I, typename J, typename T>
rocsparse_status rocsparse::bsrsm_analysis_core(rocsparse_handle          handle,
                                                rocsparse_direction       dir,
                                                rocsparse_operation       trans_A,
                                                rocsparse_operation       trans_X,
                                                J                         mb,
                                                rocsparse_int             nrhs,
                                                I                         nnzb,
                                                const rocsparse_mat_descr descr,
                                                const T*                  bsr_val,
                                                const I*                  bsr_row_ptr,
                                                const J*                  bsr_col_ind,
                                                rocsparse_int             block_dim,
                                                rocsparse_mat_info        info,
                                                rocsparse_analysis_policy analysis,
                                                rocsparse_solve_policy    solve,
                                                void*                     temp_buffer)
{
    ROCSPARSE_ROUTINE_TRACE;

    // Differentiate the analysis policies
    if(analysis == rocsparse_analysis_policy_reuse)
    {
        auto trm = info->get_bsrsm_info(trans_A, descr->fill_mode);

        trm = (trm != nullptr) ? trm : info->get_bsrsv_info(trans_A, descr->fill_mode);
        if((descr->fill_mode == rocsparse_fill_mode_lower) && (trans_A == rocsparse_operation_none))
        {
            trm = (trm != nullptr) ? trm : info->get_bsric0_info(trans_A, descr->fill_mode);
            trm = (trm != nullptr) ? trm : info->get_bsrilu0_info(trans_A, descr->fill_mode);
        }

        if(trm != nullptr)
        {
            info->set_bsrsm_info(trans_A, descr->fill_mode, trm);
            return rocsparse_status_success;
        }
    }

    auto bsrsm_info = info->get_bsrsm_info();
    RETURN_IF_ROCSPARSE_ERROR(bsrsm_info->recreate(
        handle, trans_A, mb, nnzb, descr, bsr_val, bsr_row_ptr, bsr_col_ind, temp_buffer));

    return rocsparse_status_success;
}

namespace rocsparse
{
    template <typename... P>
    static rocsparse_status bsrsm_analysis_impl(P&&... p)
    {
        ROCSPARSE_ROUTINE_TRACE;

        rocsparse::log_trace("rocsparse_Xbsrsm_analysis", p...);

        const rocsparse_status status = rocsparse::bsrsm_analysis_checkarg(p...);

        if(status != rocsparse_status_continue)
        {
            RETURN_IF_ROCSPARSE_ERROR(status);
            return rocsparse_status_success;
        }

        RETURN_IF_ROCSPARSE_ERROR(rocsparse::bsrsm_analysis_core(p...));
        return rocsparse_status_success;
    }

    template <typename I, typename J, typename T>
    static rocsparse_status bsrsm_analysis_dispatch(rocsparse_handle            handle,
                                                    rocsparse_operation         trans_A,
                                                    rocsparse_operation         trans_X,
                                                    rocsparse_const_spmat_descr A,
                                                    int64_t                     nrhs,
                                                    rocsparse_analysis_policy   analysis_policy,
                                                    rocsparse_solve_policy      solve_policy,
                                                    void*                       temp_buffer)
    {
        if(A->rows == 0)
        {
            return rocsparse_status_success;
        }

        return rocsparse::bsrsm_analysis_core<I, J, T>(handle,
                                                       A->block_dir,
                                                       trans_A,
                                                       trans_X,
                                                       static_cast<J>(A->rows),
                                                       static_cast<rocsparse_int>(nrhs),
                                                       static_cast<I>(A->nnz),
                                                       A->descr,
                                                       static_cast<const T*>(A->const_val_data),
                                                       static_cast<const I*>(A->const_row_data),
                                                       static_cast<const J*>(A->const_col_data),
                                                       static_cast<rocsparse_int>(A->block_dim),
                                                       A->info,
                                                       analysis_policy,
                                                       solve_policy,
                                                       temp_buffer);
    }

    template <typename I, typename J>
    static rocsparse_status bsrsm_analysis_dispatch_t(rocsparse_handle            handle,
                                                      rocsparse_operation         trans_A,
                                                      rocsparse_operation         trans_X,
                                                      rocsparse_const_spmat_descr A,
                                                      int64_t                     nrhs,
                                                      rocsparse_analysis_policy   analysis_policy,
                                                      rocsparse_solve_policy      solve_policy,
                                                      void*                       temp_buffer)
    {
        switch(A->data_type)
        {
        case rocsparse_datatype_f32_r:
            return rocsparse::bsrsm_analysis_dispatch<I, J, float>(
                handle, trans_A, trans_X, A, nrhs, analysis_policy, solve_policy, temp_buffer);
        case rocsparse_datatype_f64_r:
            return rocsparse::bsrsm_analysis_dispatch<I, J, double>(
                handle, trans_A, trans_X, A, nrhs, analysis_policy, solve_policy, temp_buffer);
        case rocsparse_datatype_f32_c:
            return rocsparse::bsrsm_analysis_dispatch<I, J, rocsparse_float_complex>(
                handle, trans_A, trans_X, A, nrhs, analysis_policy, solve_policy, temp_buffer);
        case rocsparse_datatype_f64_c:
            return rocsparse::bsrsm_analysis_dispatch<I, J, rocsparse_double_complex>(
                handle, trans_A, trans_X, A, nrhs, analysis_policy, solve_policy, temp_buffer);
        default:
            RETURN_IF_ROCSPARSE_ERROR(rocsparse_status_not_implemented);
        }
        // LCOV_EXCL_START
        return rocsparse_status_success;
        // LCOV_EXCL_STOP
    }
}

rocsparse_status rocsparse::bsrsm_analysis(rocsparse_handle            handle,
                                           rocsparse_operation         trans_A,
                                           rocsparse_operation         trans_X,
                                           rocsparse_const_spmat_descr A,
                                           int64_t                     nrhs,
                                           rocsparse_analysis_policy   analysis_policy,
                                           rocsparse_solve_policy      solve_policy,
                                           void*                       temp_buffer)
{
    ROCSPARSE_ROUTINE_TRACE;

    // Dispatch on the spmat index types (i32/i64) like the other formats.
    if(A->row_type == rocsparse_indextype_i32 && A->col_type == rocsparse_indextype_i32)
    {
        RETURN_IF_ROCSPARSE_ERROR((rocsparse::bsrsm_analysis_dispatch_t<int32_t, int32_t>(
            handle, trans_A, trans_X, A, nrhs, analysis_policy, solve_policy, temp_buffer)));
    }
    else if(A->row_type == rocsparse_indextype_i64 && A->col_type == rocsparse_indextype_i32)
    {
        RETURN_IF_ROCSPARSE_ERROR((rocsparse::bsrsm_analysis_dispatch_t<int64_t, int32_t>(
            handle, trans_A, trans_X, A, nrhs, analysis_policy, solve_policy, temp_buffer)));
    }
    else if(A->row_type == rocsparse_indextype_i64 && A->col_type == rocsparse_indextype_i64)
    {
        RETURN_IF_ROCSPARSE_ERROR((rocsparse::bsrsm_analysis_dispatch_t<int64_t, int64_t>(
            handle, trans_A, trans_X, A, nrhs, analysis_policy, solve_policy, temp_buffer)));
    }
    else
    {
        RETURN_IF_ROCSPARSE_ERROR(rocsparse_status_not_implemented);
    }

    return rocsparse_status_success;
}

/*
 * ===========================================================================
 *    C wrapper
 * ===========================================================================
 */

#define C_IMPL(NAME, TYPE)                                                      \
    extern "C" rocsparse_status NAME(rocsparse_handle          handle,          \
                                     rocsparse_direction       dir,             \
                                     rocsparse_operation       trans_A,         \
                                     rocsparse_operation       trans_X,         \
                                     rocsparse_int             mb,              \
                                     rocsparse_int             nrhs,            \
                                     rocsparse_int             nnzb,            \
                                     const rocsparse_mat_descr descr,           \
                                     const TYPE*               bsr_val,         \
                                     const rocsparse_int*      bsr_row_ptr,     \
                                     const rocsparse_int*      bsr_col_ind,     \
                                     rocsparse_int             block_dim,       \
                                     rocsparse_mat_info        info,            \
                                     rocsparse_analysis_policy analysis,        \
                                     rocsparse_solve_policy    solve,           \
                                     void*                     temp_buffer)     \
    try                                                                         \
    {                                                                           \
        ROCSPARSE_ROUTINE_TRACE;                                                \
        RETURN_IF_ROCSPARSE_ERROR(rocsparse::bsrsm_analysis_impl(handle,        \
                                                                 dir,           \
                                                                 trans_A,       \
                                                                 trans_X,       \
                                                                 mb,            \
                                                                 nrhs,          \
                                                                 nnzb,          \
                                                                 descr,         \
                                                                 bsr_val,       \
                                                                 bsr_row_ptr,   \
                                                                 bsr_col_ind,   \
                                                                 block_dim,     \
                                                                 info,          \
                                                                 analysis,      \
                                                                 solve,         \
                                                                 temp_buffer)); \
        return rocsparse_status_success;                                        \
    }                                                                           \
    catch(...)                                                                  \
    {                                                                           \
        RETURN_ROCSPARSE_EXCEPTION();                                           \
    }

C_IMPL(rocsparse_sbsrsm_analysis, float);
C_IMPL(rocsparse_dbsrsm_analysis, double);
C_IMPL(rocsparse_cbsrsm_analysis, rocsparse_float_complex);
C_IMPL(rocsparse_zbsrsm_analysis, rocsparse_double_complex);

#undef C_IMPL
