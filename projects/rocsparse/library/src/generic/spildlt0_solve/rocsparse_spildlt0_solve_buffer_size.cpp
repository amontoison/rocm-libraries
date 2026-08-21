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
#include "rocsparse_spildlt0_solve_descr.hpp"
#include "rocsparse_utility.hpp"

#include "internal/generic/rocsparse_spildlt0_solve.h"

namespace rocsparse
{
    static rocsparse_status spildlt0_solve_buffer_size(rocsparse_handle            handle,
                                                       rocsparse_const_spmat_descr A,
                                                       rocsparse_const_dnmat_descr B,
                                                       rocsparse_const_dnmat_descr X,
                                                       size_t* p_buffer_size_in_bytes)
    {
        ROCSPARSE_ROUTINE_TRACE;

        if(A->format != rocsparse_format_csr)
        {
            // LCOV_EXCL_START
            RETURN_IF_ROCSPARSE_ERROR(rocsparse_status_not_implemented);
            // LCOV_EXCL_STOP
        }

        // LDL^T A = L D L^T (combined factor): the two unit-lower triangular sweeps
        // (forward L, backward L^H) bound the buffer; the diagonal scale is in place.
        const rocsparse::backsolve_sweep sweeps[]
            = {{A, rocsparse_operation_none, rocsparse_fill_mode_lower, rocsparse_diag_type_unit},
               {A,
                rocsparse_operation_conjugate_transpose,
                rocsparse_fill_mode_lower,
                rocsparse_diag_type_unit}};
        const int n_sweeps = 2;

        RETURN_IF_ROCSPARSE_ERROR(rocsparse::backsolve_buffer_size(
            handle, sweeps, n_sweeps, B, X, p_buffer_size_in_bytes));
        return rocsparse_status_success;
    }
}

/*
 * ===========================================================================
 *    C wrapper
 * ===========================================================================
 */
extern "C" rocsparse_status
    rocsparse_spildlt0_solve_buffer_size(rocsparse_handle               handle,
                                         rocsparse_spildlt0_solve_descr spildlt0_solve_descr,
                                         rocsparse_const_spmat_descr    A,
                                         rocsparse_const_dnmat_descr    B,
                                         rocsparse_const_dnmat_descr    X,
                                         rocsparse_spildlt0_solve_stage spildlt0_solve_stage,
                                         size_t*                        p_buffer_size_in_bytes,
                                         rocsparse_error*               p_error)
try
{
    ROCSPARSE_ROUTINE_TRACE;
    ROCSPARSE_CHECKARG_HANDLE(0, handle);
    ROCSPARSE_CHECKARG_POINTER(1, spildlt0_solve_descr);
    ROCSPARSE_CHECKARG_POINTER(2, A);
    ROCSPARSE_CHECKARG_POINTER(3, B);
    ROCSPARSE_CHECKARG_POINTER(4, X);
    ROCSPARSE_CHECKARG_ENUM(5, spildlt0_solve_stage);
    ROCSPARSE_CHECKARG_POINTER(6, p_buffer_size_in_bytes);

    RETURN_IF_ROCSPARSE_ERROR(
        rocsparse::spildlt0_solve_buffer_size(handle, A, B, X, p_buffer_size_in_bytes));

    return rocsparse_status_success;
    // LCOV_EXCL_START
}
catch(...)
{
    RETURN_ROCSPARSE_EXCEPTION();
}
// LCOV_EXCL_STOP
