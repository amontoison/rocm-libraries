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

#include "rocsparse_spic0_solve_descr.hpp"
#include "rocsparse_utility.hpp"

#include "internal/generic/rocsparse_spic0_solve.h"

_rocsparse_spic0_solve_descr::~_rocsparse_spic0_solve_descr()
{
    this->m_stage            = ((rocsparse_spic0_solve_stage)-1);
    this->m_alg              = ((rocsparse_spic0_solve_alg)-1);
    this->m_compute_datatype = ((rocsparse_datatype)-1);
    this->m_analysis_policy  = ((rocsparse_analysis_policy)-1);
    this->m_format           = ((rocsparse_format)-1);
    this->m_matrix           = nullptr;
}

_rocsparse_spic0_solve_descr::_rocsparse_spic0_solve_descr()
    : m_stage((rocsparse_spic0_solve_stage)-1)
    , m_alg((rocsparse_spic0_solve_alg)-1)
    , m_compute_datatype((rocsparse_datatype)-1)
    , m_analysis_policy(rocsparse_analysis_policy_force)
    , m_format((rocsparse_format)-1)
{
}

rocsparse_spic0_solve_stage _rocsparse_spic0_solve_descr::get_stage() const
{
    return this->m_stage;
}

rocsparse_spic0_solve_alg _rocsparse_spic0_solve_descr::get_alg() const
{
    return this->m_alg;
}

rocsparse_datatype _rocsparse_spic0_solve_descr::get_compute_datatype() const
{
    return this->m_compute_datatype;
}

void _rocsparse_spic0_solve_descr::set_stage(rocsparse_spic0_solve_stage value)
{
    this->m_stage = value;
}

void _rocsparse_spic0_solve_descr::set_alg(rocsparse_spic0_solve_alg value)
{
    this->m_alg = value;
}

void _rocsparse_spic0_solve_descr::set_compute_datatype(rocsparse_datatype value)
{
    this->m_compute_datatype = value;
}

rocsparse_analysis_policy _rocsparse_spic0_solve_descr::get_analysis_policy() const
{
    return this->m_analysis_policy;
}

void _rocsparse_spic0_solve_descr::set_analysis_policy(rocsparse_analysis_policy value)
{
    this->m_analysis_policy = value;
}

rocsparse_format _rocsparse_spic0_solve_descr::get_format() const
{
    return this->m_format;
}

void _rocsparse_spic0_solve_descr::set_format(rocsparse_format value)
{
    this->m_format = value;
}

int32_t _rocsparse_spic0_solve_descr::get_refinement_steps() const
{
    return this->m_refinement_steps;
}

void _rocsparse_spic0_solve_descr::set_refinement_steps(int32_t value)
{
    this->m_refinement_steps = value;
}

rocsparse_const_spmat_descr _rocsparse_spic0_solve_descr::get_matrix() const
{
    return this->m_matrix;
}

void _rocsparse_spic0_solve_descr::set_matrix(rocsparse_const_spmat_descr value)
{
    this->m_matrix = value;
}

extern "C" rocsparse_status rocsparse_spic0_solve_descr_create(rocsparse_handle             handle,
                                                               rocsparse_spic0_solve_descr* descr,
                                                               rocsparse_error*             p_error)
try
{
    ROCSPARSE_ROUTINE_TRACE;
    ROCSPARSE_CHECKARG_POINTER(1, descr);
    *descr = new _rocsparse_spic0_solve_descr();
    return rocsparse_status_success;
    // LCOV_EXCL_START
}
catch(...)
{
    RETURN_ROCSPARSE_EXCEPTION();
}
// LCOV_EXCL_STOP

extern "C" rocsparse_status rocsparse_spic0_solve_descr_destroy(rocsparse_handle            handle,
                                                                rocsparse_spic0_solve_descr descr,
                                                                rocsparse_error*            p_error)
try
{
    ROCSPARSE_ROUTINE_TRACE;
    if(descr != nullptr)
    {
        delete descr;
    }
    return rocsparse_status_success;
    // LCOV_EXCL_START
}
catch(...)
{
    RETURN_ROCSPARSE_EXCEPTION();
}
// LCOV_EXCL_STOP
