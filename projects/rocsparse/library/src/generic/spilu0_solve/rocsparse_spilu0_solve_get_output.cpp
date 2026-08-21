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

#include "rocsparse_spilu0_solve_descr.hpp"
#include "rocsparse_utility.hpp"

#include "internal/generic/rocsparse_spilu0_solve.h"

template <>
bool rocsparse::enum_utils::is_invalid(rocsparse_spilu0_solve_output value)
{
    switch(value)
    {
    case rocsparse_spilu0_solve_output_refinement_iterations:
    case rocsparse_spilu0_solve_output_refinement_residual:
    {
        return false;
    }
    }
    return true;
};

extern "C" rocsparse_status rocsparse_spilu0_solve_get_output(rocsparse_handle              handle,
                                                              rocsparse_spilu0_solve_descr  descr,
                                                              rocsparse_spilu0_solve_output output,
                                                              void*                         data,
                                                              size_t           data_size_in_bytes,
                                                              rocsparse_error* p_error)
try
{
    ROCSPARSE_ROUTINE_TRACE;

    ROCSPARSE_CHECKARG_HANDLE(0, handle);
    ROCSPARSE_CHECKARG_POINTER(1, descr);
    ROCSPARSE_CHECKARG_ENUM(2, output);
    ROCSPARSE_CHECKARG_POINTER(3, data);
    ROCSPARSE_CHECKARG(
        4, data_size_in_bytes, data_size_in_bytes == 0, rocsparse_status_invalid_size);

    switch(output)
    {
    case rocsparse_spilu0_solve_output_refinement_iterations:
    {
        ROCSPARSE_CHECKARG(4,
                           data_size_in_bytes,
                           data_size_in_bytes != sizeof(int32_t),
                           rocsparse_status_invalid_size);
        *reinterpret_cast<int32_t*>(data) = descr->m_refinement_iterations;
        return rocsparse_status_success;
    }

    case rocsparse_spilu0_solve_output_refinement_residual:
    {
        ROCSPARSE_CHECKARG(4,
                           data_size_in_bytes,
                           data_size_in_bytes != sizeof(double),
                           rocsparse_status_invalid_size);
        *reinterpret_cast<double*>(data) = descr->m_refinement_residual;
        return rocsparse_status_success;
    }
    }

    // LCOV_EXCL_START
    RETURN_IF_ROCSPARSE_ERROR(rocsparse_status_invalid_value);
}
catch(...)
{
    RETURN_ROCSPARSE_EXCEPTION();
}
// LCOV_EXCL_STOP
