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

#include "rocsparse_spildlt0_solve_descr.hpp"
#include "rocsparse_utility.hpp"

#include "internal/generic/rocsparse_spildlt0_solve.h"

template <>
bool rocsparse::enum_utils::is_invalid(rocsparse_spildlt0_solve_input value)
{
    switch(value)
    {
    case rocsparse_spildlt0_solve_input_alg:
    case rocsparse_spildlt0_solve_input_analysis_policy:
    case rocsparse_spildlt0_solve_input_compute_datatype:
    case rocsparse_spildlt0_solve_input_refinement_steps:
    case rocsparse_spildlt0_solve_input_matrix:
    {
        return false;
    }
    }
    return true;
};

extern "C" rocsparse_status rocsparse_spildlt0_solve_set_input(rocsparse_handle handle,
                                                               rocsparse_spildlt0_solve_descr descr,
                                                               rocsparse_spildlt0_solve_input input,
                                                               const void*                    data,
                                                               size_t           data_size_in_bytes,
                                                               rocsparse_error* p_error)
try
{
    ROCSPARSE_ROUTINE_TRACE;

    ROCSPARSE_CHECKARG_HANDLE(0, handle);
    ROCSPARSE_CHECKARG_POINTER(1, descr);
    ROCSPARSE_CHECKARG_ENUM(2, input);
    ROCSPARSE_CHECKARG_POINTER(3, data);

    switch(input)
    {
    case rocsparse_spildlt0_solve_input_alg:
    {
        ROCSPARSE_CHECKARG(4,
                           data_size_in_bytes,
                           data_size_in_bytes != sizeof(rocsparse_spildlt0_solve_alg),
                           rocsparse_status_invalid_value);

        RETURN_WITH_MESSAGE_IF_ROCSPARSE_ERROR(descr->get_stage()
                                                       != ((rocsparse_spildlt0_solve_stage)-1)
                                                   ? rocsparse_status_invalid_value
                                                   : rocsparse_status_success,
                                               "rocsparse_spildlt0_solve_set_input cannot modify "
                                               "the descriptor after any of the stages "
                                               "rocsparse_spildlt0_solve_stage was executed");

        descr->set_alg(*reinterpret_cast<const rocsparse_spildlt0_solve_alg*>(data));
        return rocsparse_status_success;
    }

    case rocsparse_spildlt0_solve_input_analysis_policy:
    {
        ROCSPARSE_CHECKARG(4,
                           data_size_in_bytes,
                           data_size_in_bytes != sizeof(rocsparse_analysis_policy),
                           rocsparse_status_invalid_value);

        RETURN_WITH_MESSAGE_IF_ROCSPARSE_ERROR(descr->get_stage()
                                                       != ((rocsparse_spildlt0_solve_stage)-1)
                                                   ? rocsparse_status_invalid_value
                                                   : rocsparse_status_success,
                                               "rocsparse_spildlt0_solve_set_input cannot modify "
                                               "the descriptor after any of the stages "
                                               "rocsparse_spildlt0_solve_stage was executed");

        descr->set_analysis_policy(*reinterpret_cast<const rocsparse_analysis_policy*>(data));
        return rocsparse_status_success;
    }

    case rocsparse_spildlt0_solve_input_compute_datatype:
    {
        ROCSPARSE_CHECKARG(4,
                           data_size_in_bytes,
                           data_size_in_bytes != sizeof(rocsparse_datatype),
                           rocsparse_status_invalid_value);

        RETURN_WITH_MESSAGE_IF_ROCSPARSE_ERROR(descr->get_stage()
                                                       != ((rocsparse_spildlt0_solve_stage)-1)
                                                   ? rocsparse_status_invalid_value
                                                   : rocsparse_status_success,
                                               "rocsparse_spildlt0_solve_set_input cannot modify "
                                               "the descriptor after any of the stages "
                                               "rocsparse_spildlt0_solve_stage was executed");

        descr->set_compute_datatype(*reinterpret_cast<const rocsparse_datatype*>(data));
        return rocsparse_status_success;
    }

    case rocsparse_spildlt0_solve_input_refinement_steps:
    {
        ROCSPARSE_CHECKARG(4,
                           data_size_in_bytes,
                           data_size_in_bytes != sizeof(int32_t),
                           rocsparse_status_invalid_value);
        descr->set_refinement_steps(*reinterpret_cast<const int32_t*>(data));
        return rocsparse_status_success;
    }

    case rocsparse_spildlt0_solve_input_matrix:
    {
        ROCSPARSE_CHECKARG(4,
                           data_size_in_bytes,
                           data_size_in_bytes != sizeof(rocsparse_const_spmat_descr),
                           rocsparse_status_invalid_value);
        descr->set_matrix(*reinterpret_cast<const rocsparse_const_spmat_descr*>(data));
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
