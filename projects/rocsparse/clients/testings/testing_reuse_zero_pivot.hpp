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

#include "testing.hpp"

enum class reuse_routine
{
    csrsv,
    csrilu0,
    csric0,
    csrsm
};

// After an analysis with rocsparse_analysis_policy_reuse that picks up the analysis of another
// routine on the same mat_info, the zero pivot of the routine must be the one of a forced analysis.
template <typename T>
void testing_reuse_zero_pivot(reuse_routine dst)
{
    struct lower_matrix
    {
        host_vector<rocsparse_int> ptr, ind;
        host_vector<T>             val;
    };

    // No zero pivot, missing diagonal entry in row 2, zero diagonal entry in row 1.
    const lower_matrix matrices[]
        = {{{0, 1, 3, 5, 7}, {0, 0, 1, 1, 2, 2, 3}, {2, 1, 2, 1, 2, 1, 2}},
           {{0, 1, 3, 4, 6}, {0, 0, 1, 1, 2, 3}, {2, 1, 2, 1, 1, 2}},
           {{0, 1, 3, 5, 7}, {0, 0, 1, 1, 2, 2, 3}, {2, 1, 0, 1, 2, 1, 2}}};

    const rocsparse_operation    trans = rocsparse_operation_none;
    const rocsparse_solve_policy spol  = rocsparse_solve_policy_auto;
    const rocsparse_int          M     = 4;
    const rocsparse_int          nrhs  = 2;
    const T                      alpha = static_cast<T>(1);

    for(const lower_matrix& A : matrices)
    {
        const rocsparse_int          nnz = A.val.size();
        device_vector<rocsparse_int> dptr(A.ptr);
        device_vector<rocsparse_int> dind(A.ind);
        device_vector<T>             dval(A.val);
        host_vector<T>               hones(M * nrhs, static_cast<T>(1));

        auto zero_pivot = [&](reuse_routine             src,
                              rocsparse_analysis_policy apol,
                              rocsparse_int&            position) {
            rocsparse_local_handle    handle;
            rocsparse_local_mat_descr descr;
            rocsparse_local_mat_info  info;
            CHECK_ROCSPARSE_ERROR(rocsparse_set_mat_fill_mode(descr, rocsparse_fill_mode_lower));

            device_vector<T> dx(hones);
            device_vector<T> dy(M);
            device_vector<T> dB(hones);

            size_t buffer_size = 0;
            size_t size;
            CHECK_ROCSPARSE_ERROR(rocsparse_csrsv_buffer_size<T>(
                handle, trans, M, nnz, descr, dval, dptr, dind, info, &size));
            buffer_size = std::max(buffer_size, size);
            CHECK_ROCSPARSE_ERROR(rocsparse_csrilu0_buffer_size<T>(
                handle, M, nnz, descr, dval, dptr, dind, info, &size));
            buffer_size = std::max(buffer_size, size);
            CHECK_ROCSPARSE_ERROR(rocsparse_csric0_buffer_size<T>(
                handle, M, nnz, descr, dval, dptr, dind, info, &size));
            buffer_size = std::max(buffer_size, size);
            CHECK_ROCSPARSE_ERROR(rocsparse_csrsm_buffer_size<T>(handle,
                                                                 trans,
                                                                 trans,
                                                                 M,
                                                                 nrhs,
                                                                 nnz,
                                                                 &alpha,
                                                                 descr,
                                                                 dval,
                                                                 dptr,
                                                                 dind,
                                                                 dB,
                                                                 M,
                                                                 info,
                                                                 spol,
                                                                 &size));
            buffer_size = std::max(buffer_size, size);
            device_vector<char> dbuffer(buffer_size);

            auto analysis = [&](reuse_routine routine, rocsparse_analysis_policy policy) {
                switch(routine)
                {
                case reuse_routine::csrsv:
                    return rocsparse_csrsv_analysis<T>(handle,
                                                       trans,
                                                       M,
                                                       nnz,
                                                       descr,
                                                       dval,
                                                       dptr,
                                                       dind,
                                                       info,
                                                       policy,
                                                       spol,
                                                       dbuffer);
                case reuse_routine::csrilu0:
                    return rocsparse_csrilu0_analysis<T>(
                        handle, M, nnz, descr, dval, dptr, dind, info, policy, spol, dbuffer);
                case reuse_routine::csric0:
                    return rocsparse_csric0_analysis<T>(
                        handle, M, nnz, descr, dval, dptr, dind, info, policy, spol, dbuffer);
                case reuse_routine::csrsm:
                    return rocsparse_csrsm_analysis<T>(handle,
                                                       trans,
                                                       trans,
                                                       M,
                                                       nrhs,
                                                       nnz,
                                                       &alpha,
                                                       descr,
                                                       dval,
                                                       dptr,
                                                       dind,
                                                       dB,
                                                       M,
                                                       info,
                                                       policy,
                                                       spol,
                                                       dbuffer);
                }
                return rocsparse_status_internal_error;
            };

            if(apol == rocsparse_analysis_policy_reuse)
            {
                CHECK_ROCSPARSE_ERROR(analysis(src, rocsparse_analysis_policy_force));
            }
            CHECK_ROCSPARSE_ERROR(analysis(dst, apol));

            rocsparse_status status = rocsparse_status_internal_error;
            switch(dst)
            {
            case reuse_routine::csrsv:
            {
                CHECK_ROCSPARSE_ERROR(rocsparse_csrsv_solve<T>(handle,
                                                               trans,
                                                               M,
                                                               nnz,
                                                               &alpha,
                                                               descr,
                                                               dval,
                                                               dptr,
                                                               dind,
                                                               info,
                                                               dx,
                                                               dy,
                                                               spol,
                                                               dbuffer));
                status = rocsparse_csrsv_zero_pivot(handle, descr, info, &position);
                break;
            }
            case reuse_routine::csrilu0:
            {
                CHECK_ROCSPARSE_ERROR(rocsparse_csrilu0<T>(
                    handle, M, nnz, descr, dval, dptr, dind, info, spol, dbuffer));
                status = rocsparse_csrilu0_zero_pivot(handle, info, &position);
                break;
            }
            case reuse_routine::csric0:
            {
                CHECK_ROCSPARSE_ERROR(rocsparse_csric0<T>(
                    handle, M, nnz, descr, dval, dptr, dind, info, spol, dbuffer));
                status = rocsparse_csric0_zero_pivot(handle, info, &position);
                break;
            }
            case reuse_routine::csrsm:
            {
                break;
            }
            }
            EXPECT_ROCSPARSE_STATUS(
                status, (position != -1) ? rocsparse_status_zero_pivot : rocsparse_status_success);
        };

        rocsparse_int expected;
        zero_pivot(dst, rocsparse_analysis_policy_force, expected);
        for(reuse_routine src : {reuse_routine::csrsv,
                                 reuse_routine::csrilu0,
                                 reuse_routine::csric0,
                                 reuse_routine::csrsm})
        {
            if(src != dst)
            {
                // csrilu0 and csric0 factorize the values in place.
                dval.transfer_from(A.val);
                rocsparse_int position;
                zero_pivot(src, rocsparse_analysis_policy_reuse, position);
                unit_check_scalar<rocsparse_int>(expected, position);
            }
        }
    }
}
