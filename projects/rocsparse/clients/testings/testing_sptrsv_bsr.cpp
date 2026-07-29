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

#include "rocsparse_clients_sptrsv.hpp"
#include "testing.hpp"

template <typename I, typename J, typename T>
void testing_sptrsv_bsr_bad_arg(const Arguments& arg)
{
    //
    // Bad args of sptrsv are already tested in testing_sptrsv_csr_bad_arg.
    //
}

template <typename I, typename J, typename T>
void testing_sptrsv_bsr(const Arguments& arg)
{
    if(arg.M != arg.N)
    {
        return;
    }

    const rocsparse_operation   trans_A     = arg.transA;
    const rocsparse_index_base  base        = arg.baseA;
    const rocsparse_sptrsv_alg  alg         = arg.sptrsv_alg;
    const rocsparse_diag_type   diag        = arg.diag;
    const rocsparse_fill_mode   uplo        = arg.uplo;
    const rocsparse_matrix_type matrix_type = arg.matrix_type;
    const rocsparse_direction   dir         = arg.direction;

    // BSR triangular solve does not support the conjugate transpose operation.
    if(trans_A == rocsparse_operation_conjugate_transpose)
    {
        return;
    }

    //
    // Create handle.
    //
    rocsparse_local_handle handle(arg);

    //
    // Create host / device BSR matrix. The matrix factory only generates i32
    // indexed BSR matrices, so we build it in i32 and then widen the index
    // arrays to the requested I / J index types below.
    //
    host_gebsr_matrix<T>   hA;
    device_gebsr_matrix<T> dA;
    {
        rocsparse_matrix_factory<T> matrix_factory(arg);

        rocsparse_int mb = (arg.M + arg.block_dim - 1) / arg.block_dim;
        rocsparse_int nb = (arg.N + arg.block_dim - 1) / arg.block_dim;
        matrix_factory.init_bsr(hA, dA, mb, nb, base);
    }

    const rocsparse_int M = dA.mb * dA.row_block_dim;
    const rocsparse_int N = dA.nb * dA.col_block_dim;

    // Non-squared matrices are not supported.
    if(M != N)
    {
        return;
    }

    //
    // Widen the i32 BSR index arrays to the requested I (row pointer) and
    // J (column index) types, then upload them to the device.
    //
    host_vector<I> hbsr_row_ptr(hA.mb + 1);
    host_vector<J> hbsr_col_ind(hA.nnzb);
    for(rocsparse_int i = 0; i < hA.mb + 1; ++i)
    {
        hbsr_row_ptr[i] = static_cast<I>(hA.ptr[i]);
    }
    for(rocsparse_int i = 0; i < hA.nnzb; ++i)
    {
        hbsr_col_ind[i] = static_cast<J>(hA.ind[i]);
    }
    device_vector<I> dbsr_row_ptr(hbsr_row_ptr);
    device_vector<J> dbsr_col_ind(hbsr_col_ind);

    //
    // Create host data.
    //
    host_scalar<T>       halpha(arg.get_alpha<T>());
    host_dense_vector<T> hx(M);
    rocsparse_init<T>(hx, M, 1, 1);

    //
    // Create device data.
    //
    device_dense_vector<T> dx(hx);
    device_dense_vector<T> dy(M);
    device_scalar<T>       dalpha(halpha);

    //
    // Create descriptors. The sparse matrix uses the widened I / J index arrays
    // (values reuse the i32-generated device value array).
    //
    const rocsparse_datatype ttype = get_datatype<T>();
    rocsparse_local_spmat    A(dA.mb,
                            dA.nb,
                            dA.nnzb,
                            dA.block_direction,
                            dA.row_block_dim,
                            dbsr_row_ptr,
                            dbsr_col_ind,
                            dA.val,
                            get_indextype<I>(),
                            get_indextype<J>(),
                            base,
                            ttype,
                            rocsparse_format_bsr);
    rocsparse_local_dnvec    x(dx);
    rocsparse_local_dnvec    y(dy);

    CHECK_ROCSPARSE_ERROR(
        rocsparse_spmat_set_attribute(A, rocsparse_spmat_fill_mode, &uplo, sizeof(uplo)));
    CHECK_ROCSPARSE_ERROR(
        rocsparse_spmat_set_attribute(A, rocsparse_spmat_diag_type, &diag, sizeof(diag)));
    CHECK_ROCSPARSE_ERROR(rocsparse_spmat_set_attribute(
        A, rocsparse_spmat_matrix_type, &matrix_type, sizeof(matrix_type)));

    rocsparse_sptrsv_descr sptrsv_descr;
    CHECK_ROCSPARSE_ERROR(rocsparse_create_sptrsv_descr(&sptrsv_descr));

    rocsparse_error p_error[1] = {nullptr};
    CHECK_ROCSPARSE_ERROR(rocsparse_sptrsv_set_input(handle,
                                                     sptrsv_descr,
                                                     rocsparse_sptrsv_input_operation,
                                                     &trans_A,
                                                     sizeof(trans_A),
                                                     p_error));

    CHECK_ROCSPARSE_ERROR(rocsparse_sptrsv_set_input(
        handle, sptrsv_descr, rocsparse_sptrsv_input_alg, &alg, sizeof(alg), p_error));

    {
        CHECK_ROCSPARSE_ERROR(rocsparse_sptrsv_set_input(handle,
                                                         sptrsv_descr,
                                                         rocsparse_sptrsv_input_scalar_datatype,
                                                         &ttype,
                                                         sizeof(ttype),
                                                         p_error));
        CHECK_ROCSPARSE_ERROR(rocsparse_sptrsv_set_input(handle,
                                                         sptrsv_descr,
                                                         rocsparse_sptrsv_input_compute_datatype,
                                                         &ttype,
                                                         sizeof(ttype),
                                                         p_error));
    }

    {
        const rocsparse_analysis_policy apol = arg.apol;
        CHECK_ROCSPARSE_ERROR(rocsparse_sptrsv_set_input(handle,
                                                         sptrsv_descr,
                                                         rocsparse_sptrsv_input_analysis_policy,
                                                         &apol,
                                                         sizeof(apol),
                                                         p_error));
    }

    rocsparse_clients::sptrsv_analysis(handle, sptrsv_descr, A, x, y, p_error);

    int64_t          analysis_zero_pivot;
    rocsparse_status analysis_pivot_status
        = rocsparse_sptrsv_get_output(handle,
                                      sptrsv_descr,
                                      rocsparse_sptrsv_output_zero_pivot_position,
                                      &analysis_zero_pivot,
                                      sizeof(analysis_zero_pivot),
                                      p_error);
    if(analysis_pivot_status != rocsparse_status_zero_pivot)
    {
        CHECK_ROCSPARSE_ERROR(analysis_pivot_status);
    }

    if(arg.unit_check)
    {
        // CPU bsrsv (reference uses the original i32 index arrays).
        host_dense_vector<T>       hy(M);
        host_scalar<rocsparse_int> h_analysis_pivot;
        host_scalar<rocsparse_int> h_solve_pivot;
        host_bsrsv<T>(trans_A,
                      dir,
                      hA.mb,
                      hA.nnzb,
                      *halpha,
                      hA.ptr,
                      hA.ind,
                      hA.val,
                      hA.row_block_dim,
                      hx,
                      hy,
                      diag,
                      uplo,
                      base,
                      h_analysis_pivot,
                      h_solve_pivot);

        const bool comparable = (*h_analysis_pivot == -1 && *h_solve_pivot == -1);

        //
        // Solve on host.
        //
        rocsparse_clients::sptrsv_compute(
            handle, sptrsv_descr, A, x, y, rocsparse_pointer_mode_host, halpha, p_error);

        CHECK_HIP_ERROR(hipDeviceSynchronize());
        if(comparable)
        {
            hy.near_check(dy);
        }

        //
        // Solve on device.
        //
        rocsparse_clients::sptrsv_compute(
            handle, sptrsv_descr, A, x, y, rocsparse_pointer_mode_device, dalpha, p_error);

        CHECK_HIP_ERROR(hipDeviceSynchronize());
        if(comparable)
        {
            hy.near_check(dy);
        }
    }

    CHECK_ROCSPARSE_ERROR(rocsparse_destroy_sptrsv_descr(sptrsv_descr));
}

#define INSTANTIATE(ITYPE, JTYPE, TTYPE)                                                 \
    template void testing_sptrsv_bsr_bad_arg<ITYPE, JTYPE, TTYPE>(const Arguments& arg); \
    template void testing_sptrsv_bsr<ITYPE, JTYPE, TTYPE>(const Arguments& arg)

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

void testing_sptrsv_bsr_extra(const Arguments& arg) {}
