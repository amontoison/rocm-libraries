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
#pragma once

#include "internal/generic/rocsparse_spic0_solve.h"

struct _rocsparse_spic0_solve_descr
{
protected:
    rocsparse_spic0_solve_stage m_stage;
    rocsparse_spic0_solve_alg   m_alg;
    rocsparse_datatype          m_compute_datatype;
    rocsparse_analysis_policy   m_analysis_policy;
    rocsparse_format            m_format;

    // Iterative refinement configuration (plumbing; the refinement loop itself is
    // a follow-up, see rocsparse_spic0_solve.cpp).
    int32_t                     m_refinement_steps{}; // 0 disables iterative refinement.
    rocsparse_const_spmat_descr m_matrix{}; // original A, borrowed (not owned).

public:
    // Outputs.
    int32_t m_refinement_iterations{};
    double  m_refinement_residual{};

    ~_rocsparse_spic0_solve_descr();
    _rocsparse_spic0_solve_descr();

    rocsparse_spic0_solve_stage get_stage() const;
    rocsparse_spic0_solve_alg   get_alg() const;
    rocsparse_datatype          get_compute_datatype() const;
    void                        set_stage(rocsparse_spic0_solve_stage value);
    void                        set_alg(rocsparse_spic0_solve_alg value);
    void                        set_compute_datatype(rocsparse_datatype value);

    rocsparse_analysis_policy get_analysis_policy() const;
    void                      set_analysis_policy(rocsparse_analysis_policy value);

    rocsparse_format get_format() const;
    void             set_format(rocsparse_format value);

    int32_t get_refinement_steps() const;
    void    set_refinement_steps(int32_t value);

    rocsparse_const_spmat_descr get_matrix() const;
    void                        set_matrix(rocsparse_const_spmat_descr value);
};
