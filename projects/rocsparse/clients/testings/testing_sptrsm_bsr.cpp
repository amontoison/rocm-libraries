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

#include "testing.hpp"

template <typename I, typename J, typename T>
void testing_sptrsm_bsr_bad_arg(const Arguments& arg)
{
    //
    // Bad args of sptrsm are already tested in testing_sptrsm_csr_bad_arg.
    //
}

template <typename I, typename J, typename T>
void testing_sptrsm_bsr(const Arguments& arg)
{
    if(arg.M != arg.N)
    {
        return;
    }

    const rocsparse_operation   trans_A     = arg.transA;
    const rocsparse_operation   trans_X     = arg.transB;
    const rocsparse_index_base  base        = arg.baseA;
    const rocsparse_diag_type   diag        = arg.diag;
    const rocsparse_fill_mode   uplo        = arg.uplo;
    const rocsparse_matrix_type matrix_type = arg.matrix_type;
    const rocsparse_direction   dir         = arg.direction;
    const rocsparse_order       order_B     = arg.orderB;
    const rocsparse_order       order_C     = arg.orderC;

    // The native BSR sptrsm path only supports column-major X/Y with a
    // non-transposed X operation and non-conjugate A operation.
    if(trans_X != rocsparse_operation_none || order_B != rocsparse_order_column
       || order_C != rocsparse_order_column || trans_A == rocsparse_operation_conjugate_transpose)
    {
        return;
    }

    const rocsparse_datatype ttype = get_datatype<T>();

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
    if(M != N)
    {
        return;
    }

    const rocsparse_int K   = arg.K;
    const int64_t       ldb = M;
    const int64_t       ldc = M;

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
    // Create host data (column-major right-hand side B and solution C).
    //
    host_scalar<T>       halpha(arg.get_alpha<T>());
    host_dense_matrix<T> hB(M, K);
    rocsparse_matrix_utils::init_exact(hB);

    host_dense_matrix<T>       hX_gold(M, K);
    host_scalar<rocsparse_int> h_analysis_pivot;
    host_scalar<rocsparse_int> h_solve_pivot;
    host_bsrsm<T>(hA.mb,
                  K,
                  hA.nnzb,
                  dir,
                  trans_A,
                  trans_X,
                  *halpha,
                  hA.ptr,
                  hA.ind,
                  hA.val,
                  hA.row_block_dim,
                  hB,
                  ldb,
                  hX_gold,
                  ldc,
                  diag,
                  uplo,
                  base,
                  h_analysis_pivot,
                  h_solve_pivot);

    const bool comparable = (*h_analysis_pivot == -1 && *h_solve_pivot == -1);

    //
    // Create device data.
    //
    device_dense_matrix<T> dB(hB);
    device_dense_matrix<T> dC(M, K);
    device_scalar<T>       dalpha(halpha);

    //
    // Create descriptors. The sparse matrix uses the widened I / J index arrays
    // (values reuse the i32-generated device value array).
    //
    rocsparse_local_spmat A(dA.mb,
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
    rocsparse_local_dnmat B(M, K, ldb, dB, ttype, order_B);
    rocsparse_local_dnmat C(M, K, ldc, dC, ttype, order_C);

    CHECK_ROCSPARSE_ERROR(
        rocsparse_spmat_set_attribute(A, rocsparse_spmat_fill_mode, &uplo, sizeof(uplo)));
    CHECK_ROCSPARSE_ERROR(
        rocsparse_spmat_set_attribute(A, rocsparse_spmat_diag_type, &diag, sizeof(diag)));
    CHECK_ROCSPARSE_ERROR(rocsparse_spmat_set_attribute(
        A, rocsparse_spmat_matrix_type, &matrix_type, sizeof(matrix_type)));

    rocsparse_sptrsm_descr sptrsm_descr;
    CHECK_ROCSPARSE_ERROR(rocsparse_create_sptrsm_descr(&sptrsm_descr));

    rocsparse_error p_error[1] = {nullptr};

    {
        const rocsparse_sptrsm_alg alg = rocsparse_sptrsm_alg_default;
        CHECK_ROCSPARSE_ERROR(rocsparse_sptrsm_set_input(
            handle, sptrsm_descr, rocsparse_sptrsm_input_alg, &alg, sizeof(alg), p_error));
    }
    CHECK_ROCSPARSE_ERROR(rocsparse_sptrsm_set_input(handle,
                                                     sptrsm_descr,
                                                     rocsparse_sptrsm_input_operation_A,
                                                     &trans_A,
                                                     sizeof(trans_A),
                                                     p_error));
    CHECK_ROCSPARSE_ERROR(rocsparse_sptrsm_set_input(handle,
                                                     sptrsm_descr,
                                                     rocsparse_sptrsm_input_operation_X,
                                                     &trans_X,
                                                     sizeof(trans_X),
                                                     p_error));
    CHECK_ROCSPARSE_ERROR(rocsparse_sptrsm_set_input(handle,
                                                     sptrsm_descr,
                                                     rocsparse_sptrsm_input_scalar_datatype,
                                                     &ttype,
                                                     sizeof(ttype),
                                                     p_error));
    CHECK_ROCSPARSE_ERROR(rocsparse_sptrsm_set_input(handle,
                                                     sptrsm_descr,
                                                     rocsparse_sptrsm_input_compute_datatype,
                                                     &ttype,
                                                     sizeof(ttype),
                                                     p_error));
    {
        const rocsparse_analysis_policy apol = arg.apol;
        CHECK_ROCSPARSE_ERROR(rocsparse_sptrsm_set_input(handle,
                                                         sptrsm_descr,
                                                         rocsparse_sptrsm_input_analysis_policy,
                                                         &apol,
                                                         sizeof(apol),
                                                         p_error));
    }

    //
    // Analysis.
    //
    {
        size_t buffer_size_in_bytes;
        CHECK_ROCSPARSE_ERROR(rocsparse_sptrsm_buffer_size(handle,
                                                           sptrsm_descr,
                                                           A,
                                                           B,
                                                           C,
                                                           rocsparse_sptrsm_stage_analysis,
                                                           &buffer_size_in_bytes,
                                                           p_error));
        void* buffer;
        CHECK_HIP_ERROR(rocsparse_hipMalloc(&buffer, buffer_size_in_bytes));
        CHECK_ROCSPARSE_ERROR(rocsparse_sptrsm(handle,
                                               sptrsm_descr,
                                               A,
                                               B,
                                               C,
                                               rocsparse_sptrsm_stage_analysis,
                                               buffer_size_in_bytes,
                                               buffer,
                                               p_error));
        CHECK_HIP_ERROR(rocsparse_hipFree(buffer));
    }

    if(arg.unit_check)
    {
        size_t buffer_size_in_bytes;
        CHECK_ROCSPARSE_ERROR(rocsparse_sptrsm_buffer_size(handle,
                                                           sptrsm_descr,
                                                           A,
                                                           B,
                                                           C,
                                                           rocsparse_sptrsm_stage_compute,
                                                           &buffer_size_in_bytes,
                                                           p_error));
        void* buffer;
        CHECK_HIP_ERROR(rocsparse_hipMalloc(&buffer, buffer_size_in_bytes));

        //
        // Solve on host.
        //
        CHECK_ROCSPARSE_ERROR(rocsparse_set_pointer_mode(handle, rocsparse_pointer_mode_host));
        CHECK_ROCSPARSE_ERROR(rocsparse_sptrsm_set_input(handle,
                                                         sptrsm_descr,
                                                         rocsparse_sptrsm_input_scalar_alpha,
                                                         halpha,
                                                         sizeof(const T*),
                                                         p_error));
        CHECK_ROCSPARSE_ERROR(rocsparse_sptrsm(handle,
                                               sptrsm_descr,
                                               A,
                                               B,
                                               C,
                                               rocsparse_sptrsm_stage_compute,
                                               buffer_size_in_bytes,
                                               buffer,
                                               p_error));
        CHECK_HIP_ERROR(hipDeviceSynchronize());
        if(comparable)
        {
            hX_gold.near_check(dC);
        }

        //
        // Solve on device.
        //
        CHECK_HIP_ERROR(hipMemset(dC, 0, sizeof(T) * M * K));
        CHECK_ROCSPARSE_ERROR(rocsparse_set_pointer_mode(handle, rocsparse_pointer_mode_device));
        CHECK_ROCSPARSE_ERROR(rocsparse_sptrsm_set_input(handle,
                                                         sptrsm_descr,
                                                         rocsparse_sptrsm_input_scalar_alpha,
                                                         dalpha,
                                                         sizeof(const T*),
                                                         p_error));
        CHECK_ROCSPARSE_ERROR(rocsparse_sptrsm(handle,
                                               sptrsm_descr,
                                               A,
                                               B,
                                               C,
                                               rocsparse_sptrsm_stage_compute,
                                               buffer_size_in_bytes,
                                               buffer,
                                               p_error));
        CHECK_HIP_ERROR(hipDeviceSynchronize());
        if(comparable)
        {
            hX_gold.near_check(dC);
        }

        CHECK_HIP_ERROR(rocsparse_hipFree(buffer));
    }

    CHECK_ROCSPARSE_ERROR(rocsparse_destroy_sptrsm_descr(sptrsm_descr));
}

#define INSTANTIATE(ITYPE, JTYPE, TTYPE)                                                 \
    template void testing_sptrsm_bsr_bad_arg<ITYPE, JTYPE, TTYPE>(const Arguments& arg); \
    template void testing_sptrsm_bsr<ITYPE, JTYPE, TTYPE>(const Arguments& arg)

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

void testing_sptrsm_bsr_extra(const Arguments& arg) {}
