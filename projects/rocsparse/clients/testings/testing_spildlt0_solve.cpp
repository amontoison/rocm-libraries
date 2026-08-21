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
 * THE SOFTWARE IS PROVIDED AS IS, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * ************************************************************************ */

#include "testing_spildlt0_solve.hpp"
#include "testing.hpp"

template <typename I, typename J, typename T>
void testing_spildlt0_solve_bad_arg(const Arguments& arg)
{
    J m    = 100;
    J n    = 100;
    J nrhs = 16;
    I nnz  = 100;

    rocsparse_index_base base = rocsparse_index_base_zero;

    rocsparse_indextype itype        = get_indextype<I>();
    rocsparse_indextype jtype        = get_indextype<J>();
    rocsparse_datatype  compute_type = get_datatype<T>();

    rocsparse_local_handle local_handle;
    rocsparse_handle       handle = local_handle;

    // Lower-triangular factor and dense right-hand sides.
    rocsparse_local_spmat local_L(
        m, n, nnz, (void*)0x4, (void*)0x4, (void*)0x4, itype, jtype, base, compute_type);
    rocsparse_local_dnmat local_B(m, nrhs, m, (void*)0x4, compute_type, rocsparse_order_column);
    rocsparse_local_dnmat local_X(m, nrhs, m, (void*)0x4, compute_type, rocsparse_order_column);

    rocsparse_spmat_descr matL = local_L;
    rocsparse_dnmat_descr matB = local_B;
    rocsparse_dnmat_descr matX = local_X;

    const rocsparse_fill_mode fill_mode = rocsparse_fill_mode_lower;
    const rocsparse_diag_type diag_type = rocsparse_diag_type_non_unit;
    CHECK_ROCSPARSE_ERROR(rocsparse_spmat_set_attribute(
        matL, rocsparse_spmat_fill_mode, &fill_mode, sizeof(fill_mode)));
    CHECK_ROCSPARSE_ERROR(rocsparse_spmat_set_attribute(
        matL, rocsparse_spmat_diag_type, &diag_type, sizeof(diag_type)));

    // Descriptor.
    rocsparse_spildlt0_solve_descr descr = nullptr;
    CHECK_ROCSPARSE_ERROR(rocsparse_spildlt0_solve_descr_create(handle, &descr, nullptr));

    // descr_create bad args.
    EXPECT_ROCSPARSE_STATUS(rocsparse_spildlt0_solve_descr_create(handle, nullptr, nullptr),
                            rocsparse_status_invalid_pointer);

    // set_input bad args.
    const rocsparse_datatype dt = compute_type;
    EXPECT_ROCSPARSE_STATUS(
        rocsparse_spildlt0_solve_set_input(handle,
                                           nullptr,
                                           rocsparse_spildlt0_solve_input_compute_datatype,
                                           &dt,
                                           sizeof(dt),
                                           nullptr),
        rocsparse_status_invalid_pointer);
    EXPECT_ROCSPARSE_STATUS(
        rocsparse_spildlt0_solve_set_input(handle,
                                           descr,
                                           rocsparse_spildlt0_solve_input_compute_datatype,
                                           nullptr,
                                           sizeof(dt),
                                           nullptr),
        rocsparse_status_invalid_pointer);
    CHECK_ROCSPARSE_ERROR(rocsparse_spildlt0_solve_set_input(
        handle, descr, rocsparse_spildlt0_solve_input_compute_datatype, &dt, sizeof(dt), nullptr));

    // buffer_size bad args.
    size_t buffer_size;
    EXPECT_ROCSPARSE_STATUS(
        rocsparse_spildlt0_solve_buffer_size(handle,
                                             descr,
                                             nullptr,
                                             matB,
                                             matX,
                                             rocsparse_spildlt0_solve_stage_analysis,
                                             &buffer_size,
                                             nullptr),
        rocsparse_status_invalid_pointer);
    EXPECT_ROCSPARSE_STATUS(
        rocsparse_spildlt0_solve_buffer_size(handle,
                                             descr,
                                             matL,
                                             matB,
                                             matX,
                                             rocsparse_spildlt0_solve_stage_analysis,
                                             nullptr,
                                             nullptr),
        rocsparse_status_invalid_pointer);

    // solve bad args.
    EXPECT_ROCSPARSE_STATUS(rocsparse_spildlt0_solve(handle,
                                                     descr,
                                                     nullptr,
                                                     matB,
                                                     matX,
                                                     rocsparse_spildlt0_solve_stage_solve,
                                                     0,
                                                     nullptr,
                                                     nullptr),
                            rocsparse_status_invalid_pointer);
    EXPECT_ROCSPARSE_STATUS(rocsparse_spildlt0_solve(handle,
                                                     descr,
                                                     matL,
                                                     nullptr,
                                                     matX,
                                                     rocsparse_spildlt0_solve_stage_solve,
                                                     0,
                                                     nullptr,
                                                     nullptr),
                            rocsparse_status_invalid_pointer);

    CHECK_ROCSPARSE_ERROR(rocsparse_spildlt0_solve_descr_destroy(handle, descr, nullptr));
}

template <typename I, typename J, typename T>
void testing_spildlt0_solve(const Arguments& arg)
{
    J                    M    = arg.M;
    J                    nrhs = (arg.K > 0) ? arg.K : 1;
    rocsparse_index_base base = arg.baseA;

    rocsparse_indextype itype        = get_indextype<I>();
    rocsparse_indextype jtype        = get_indextype<J>();
    rocsparse_datatype  compute_type = get_datatype<T>();

    rocsparse_local_handle handle(arg);

    // Quick return for trivial sizes.
    if(M <= 0)
    {
        return;
    }

    // Build a deterministic combined LDL^T factor: strictly-lower entries hold the
    // unit-lower L (sub-diagonal 1) and the diagonal holds D (4). Well conditioned.
    const T diag_value = static_cast<T>(4);
    const I nnz        = static_cast<I>(2) * M - 1;

    host_vector<I> hcsr_row_ptr(M + 1);
    host_vector<J> hcsr_col_ind(nnz);
    host_vector<T> hcsr_val(nnz);

    hcsr_row_ptr[0] = base;
    I k             = 0;
    for(J i = 0; i < M; ++i)
    {
        if(i > 0)
        {
            hcsr_col_ind[k] = (i - 1) + base;
            hcsr_val[k]     = static_cast<T>(1);
            ++k;
        }
        hcsr_col_ind[k] = i + base;
        hcsr_val[k]     = diag_value;
        ++k;
        hcsr_row_ptr[i + 1] = k + base;
    }

    // Right-hand side B (column-major, ld = M) and the host reference solution.
    host_dense_matrix<T> hB(M, nrhs);
    rocsparse_matrix_utils::init(hB);

    host_dense_matrix<T> hX_gold(M, nrhs);
    hX_gold = hB;

    // Reference: forward L Y = B (unit lower), then Y <- D^{-1} Y, then L^H X = Y
    // (unit lower transpose), in place on hX_gold.
    J analysis_pivot = -1;
    J solve_pivot    = -1;
    host_csrsm<I, J, T>(M,
                        nrhs,
                        nnz,
                        rocsparse_operation_none,
                        rocsparse_operation_none,
                        static_cast<T>(1),
                        hcsr_row_ptr,
                        hcsr_col_ind,
                        hcsr_val,
                        hX_gold,
                        M,
                        rocsparse_order_column,
                        rocsparse_diag_type_unit,
                        rocsparse_fill_mode_lower,
                        base,
                        &analysis_pivot,
                        &solve_pivot);
    for(J i = 0; i < M; ++i)
    {
        for(J c = 0; c < nrhs; ++c)
        {
            hX_gold[i + c * M] = hX_gold[i + c * M] / diag_value;
        }
    }
    host_csrsm<I, J, T>(M,
                        nrhs,
                        nnz,
                        rocsparse_operation_conjugate_transpose,
                        rocsparse_operation_none,
                        static_cast<T>(1),
                        hcsr_row_ptr,
                        hcsr_col_ind,
                        hcsr_val,
                        hX_gold,
                        M,
                        rocsparse_order_column,
                        rocsparse_diag_type_unit,
                        rocsparse_fill_mode_lower,
                        base,
                        &analysis_pivot,
                        &solve_pivot);

    // Device data.
    device_vector<I>       dcsr_row_ptr(hcsr_row_ptr);
    device_vector<J>       dcsr_col_ind(hcsr_col_ind);
    device_vector<T>       dcsr_val(hcsr_val);
    device_dense_matrix<T> dB(M, nrhs);
    device_dense_matrix<T> dX(M, nrhs);
    CHECK_HIP_ERROR(hipMemcpy(dB, hB, sizeof(T) * M * nrhs, hipMemcpyHostToDevice));

    // The combined LDL^T factor's fill mode / diagonal type are selected internally
    // per sweep, so no attributes are set on the descriptor here.
    rocsparse_local_spmat matL(
        M, M, nnz, dcsr_row_ptr, dcsr_col_ind, dcsr_val, itype, jtype, base, compute_type);

    rocsparse_local_dnmat matB(M, nrhs, M, dB, compute_type, rocsparse_order_column);
    rocsparse_local_dnmat matX(M, nrhs, M, dX, compute_type, rocsparse_order_column);

    // Descriptor.
    rocsparse_spildlt0_solve_descr descr = nullptr;
    CHECK_ROCSPARSE_ERROR(rocsparse_spildlt0_solve_descr_create(handle, &descr, nullptr));
    CHECK_ROCSPARSE_ERROR(
        rocsparse_spildlt0_solve_set_input(handle,
                                           descr,
                                           rocsparse_spildlt0_solve_input_compute_datatype,
                                           &compute_type,
                                           sizeof(compute_type),
                                           nullptr));

    // Buffer.
    size_t buffer_size;
    CHECK_ROCSPARSE_ERROR(
        rocsparse_spildlt0_solve_buffer_size(handle,
                                             descr,
                                             matL,
                                             matB,
                                             matX,
                                             rocsparse_spildlt0_solve_stage_analysis,
                                             &buffer_size,
                                             nullptr));
    void* dbuffer;
    CHECK_HIP_ERROR(rocsparse_hipMalloc(&dbuffer, buffer_size));

    // Analysis + solve.
    CHECK_ROCSPARSE_ERROR(rocsparse_spildlt0_solve(handle,
                                                   descr,
                                                   matL,
                                                   matB,
                                                   matX,
                                                   rocsparse_spildlt0_solve_stage_analysis,
                                                   buffer_size,
                                                   dbuffer,
                                                   nullptr));

    if(arg.unit_check)
    {
        CHECK_ROCSPARSE_ERROR(rocsparse_spildlt0_solve(handle,
                                                       descr,
                                                       matL,
                                                       matB,
                                                       matX,
                                                       rocsparse_spildlt0_solve_stage_solve,
                                                       buffer_size,
                                                       dbuffer,
                                                       nullptr));

        host_dense_matrix<T> hX(M, nrhs);
        CHECK_HIP_ERROR(hipMemcpy(hX, dX, sizeof(T) * M * nrhs, hipMemcpyDeviceToHost));

        if(analysis_pivot == -1 && solve_pivot == -1)
        {
            hX_gold.near_check(hX);
        }
    }

    CHECK_HIP_ERROR(rocsparse_hipFree(dbuffer));
    CHECK_ROCSPARSE_ERROR(rocsparse_spildlt0_solve_descr_destroy(handle, descr, nullptr));
}

void testing_spildlt0_solve_extra(const Arguments& arg) {}

#define INSTANTIATE(I, J, T)                                                     \
    template void testing_spildlt0_solve_bad_arg<I, J, T>(const Arguments& arg); \
    template void testing_spildlt0_solve<I, J, T>(const Arguments& arg)

INSTANTIATE(int32_t, int32_t, float);
INSTANTIATE(int32_t, int32_t, double);
INSTANTIATE(int32_t, int32_t, rocsparse_float_complex);
INSTANTIATE(int32_t, int32_t, rocsparse_double_complex);

INSTANTIATE(int64_t, int32_t, float);
INSTANTIATE(int64_t, int32_t, double);
INSTANTIATE(int64_t, int32_t, rocsparse_float_complex);
INSTANTIATE(int64_t, int32_t, rocsparse_double_complex);

INSTANTIATE(int64_t, int64_t, float);
INSTANTIATE(int64_t, int64_t, double);
INSTANTIATE(int64_t, int64_t, rocsparse_float_complex);
INSTANTIATE(int64_t, int64_t, rocsparse_double_complex);

#undef INSTANTIATE
