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

#include "../preconditioners_solve/rocsparse_preconditioners_backsolve.hpp"
#include "rocsparse_spic0_solve_descr.hpp"
#include "rocsparse_utility.hpp"

#include "internal/generic/rocsparse_spic0_solve.h"

template <>
bool rocsparse::enum_utils::is_invalid(rocsparse_spic0_solve_alg value)
{
    switch(value)
    {
    case rocsparse_spic0_solve_alg_default:
    {
        return false;
    }
    }
    return true;
};

template <>
bool rocsparse::enum_utils::is_invalid(rocsparse_spic0_solve_stage value)
{
    switch(value)
    {
    case rocsparse_spic0_solve_stage_analysis:
    case rocsparse_spic0_solve_stage_solve:
    {
        return false;
    }
    }
    return true;
};

namespace rocsparse
{
    static rocsparse_status spic0_solve(rocsparse_handle            handle,
                                        rocsparse_spic0_solve_descr descr,
                                        rocsparse_const_spmat_descr L,
                                        rocsparse_const_dnmat_descr B,
                                        rocsparse_dnmat_descr       X,
                                        rocsparse_spic0_solve_stage stage,
                                        void*                       buffer)
    {
        ROCSPARSE_ROUTINE_TRACE;

        if(L->format != rocsparse_format_csr)
        {
            // LCOV_EXCL_START
            RETURN_IF_ROCSPARSE_ERROR(rocsparse_status_not_implemented);
            // LCOV_EXCL_STOP
        }

        // Cholesky A = L L^H (Hermitian): forward sweep L Y = B, backward sweep L^H X = Y.
        const rocsparse::backsolve_sweep sweeps[] = {
            {L, rocsparse_operation_none, rocsparse_fill_mode_lower, rocsparse_diag_type_non_unit},
            {L,
             rocsparse_operation_conjugate_transpose,
             rocsparse_fill_mode_lower,
             rocsparse_diag_type_non_unit}};
        const int n_sweeps = 2;

        const rocsparse_spic0_solve_stage previous_stage = descr->get_stage();

        switch(stage)
        {
        case rocsparse_spic0_solve_stage_analysis:
        {
            RETURN_IF_ROCSPARSE_ERROR(
                rocsparse::backsolve_analysis(handle, sweeps, n_sweeps, B, X, buffer));
            descr->set_stage(rocsparse_spic0_solve_stage_analysis);
            return rocsparse_status_success;
        }

        case rocsparse_spic0_solve_stage_solve:
        {
            if(previous_stage == ((rocsparse_spic0_solve_stage)-1))
            {
                RETURN_WITH_MESSAGE_IF_ROCSPARSE_ERROR(
                    rocsparse_status_invalid_value,
                    "invalid stage, the stage rocsparse_spic0_solve_stage_analysis must be "
                    "executed before the stage rocsparse_spic0_solve_stage_solve");
            }

            RETURN_IF_ROCSPARSE_ERROR(
                rocsparse::backsolve_solve(handle, sweeps, n_sweeps, B, X, buffer));

            // Iterative refinement is not implemented yet (see the descriptor plumbing).
            descr->m_refinement_iterations = 0;
            descr->m_refinement_residual   = 0.0;

            descr->set_stage(rocsparse_spic0_solve_stage_solve);
            return rocsparse_status_success;
        }
        }

        // LCOV_EXCL_START
        RETURN_IF_ROCSPARSE_ERROR(rocsparse_status_invalid_value);
        // LCOV_EXCL_STOP
    }
}

/*
 * ===========================================================================
 *    C wrapper
 * ===========================================================================
 */
extern "C" rocsparse_status rocsparse_spic0_solve(rocsparse_handle            handle, // 0
                                                  rocsparse_spic0_solve_descr descr, // 1
                                                  rocsparse_const_spmat_descr L, // 2
                                                  rocsparse_const_dnmat_descr B, // 3
                                                  rocsparse_dnmat_descr       X, // 4
                                                  rocsparse_spic0_solve_stage stage, // 5
                                                  size_t           buffer_size_in_bytes, // 6
                                                  void*            buffer, // 7
                                                  rocsparse_error* p_error)
try
{
    ROCSPARSE_ROUTINE_TRACE;
    ROCSPARSE_CHECKARG_HANDLE(0, handle);
    ROCSPARSE_CHECKARG_POINTER(1, descr);
    ROCSPARSE_CHECKARG_POINTER(2, L);
    ROCSPARSE_CHECKARG_POINTER(3, B);
    ROCSPARSE_CHECKARG_POINTER(4, X);
    ROCSPARSE_CHECKARG_ENUM(5, stage);

    // Only CSR factors are supported for now.
    ROCSPARSE_CHECKARG(2, L, (L->format != rocsparse_format_csr), rocsparse_status_not_implemented);

    // The factor must be lower triangular and non-unit diagonal.
    ROCSPARSE_CHECKARG(
        2, L, (L->descr->fill_mode != rocsparse_fill_mode_lower), rocsparse_status_not_implemented);
    ROCSPARSE_CHECKARG(2,
                       L,
                       (L->descr->diag_type != rocsparse_diag_type_non_unit),
                       rocsparse_status_not_implemented);

    // Matching precisions (no mixed precision yet).
    if(descr->get_compute_datatype() != ((rocsparse_datatype)-1))
    {
        ROCSPARSE_CHECKARG(2,
                           L,
                           (L->data_type != descr->get_compute_datatype()),
                           rocsparse_status_not_implemented);
    }
    ROCSPARSE_CHECKARG(3, B, (B->data_type != L->data_type), rocsparse_status_not_implemented);
    ROCSPARSE_CHECKARG(4, X, (X->data_type != L->data_type), rocsparse_status_not_implemented);

    // Consistent dimensions.
    ROCSPARSE_CHECKARG(2, L, (L->rows != L->cols), rocsparse_status_invalid_size);
    ROCSPARSE_CHECKARG(3, B, (B->rows != L->rows), rocsparse_status_invalid_size);
    ROCSPARSE_CHECKARG(4, X, (X->rows != L->rows), rocsparse_status_invalid_size);
    ROCSPARSE_CHECKARG(4, X, (X->cols != B->cols), rocsparse_status_invalid_size);

    ROCSPARSE_CHECKARG(6,
                       buffer_size_in_bytes,
                       (buffer_size_in_bytes == 0) && (buffer != nullptr),
                       rocsparse_status_invalid_size);
    ROCSPARSE_CHECKARG(7,
                       buffer,
                       (buffer == nullptr) && (buffer_size_in_bytes != 0),
                       rocsparse_status_invalid_pointer);

    RETURN_IF_ROCSPARSE_ERROR(rocsparse::spic0_solve(handle, descr, L, B, X, stage, buffer));

    return rocsparse_status_success;
    // LCOV_EXCL_START
}
catch(...)
{
    RETURN_ROCSPARSE_EXCEPTION();
}
// LCOV_EXCL_STOP
