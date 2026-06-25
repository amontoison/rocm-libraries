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

#include <cmath>
#include <complex>
#include <iostream>
#include <rocsparse/rocsparse.h>

#define HIP_CHECK(stat)                                                                       \
    {                                                                                         \
        if(stat != hipSuccess)                                                                \
        {                                                                                     \
            std::cerr << "Error: hip error " << stat << " in line " << __LINE__ << std::endl; \
            return -1;                                                                        \
        }                                                                                     \
    }

#define ROCSPARSE_CHECK(stat)                                                         \
    {                                                                                 \
        if(stat != rocsparse_status_success)                                          \
        {                                                                             \
            std::cerr << "Error: rocsparse error " << stat << " in line " << __LINE__ \
                      << std::endl;                                                   \
            return -1;                                                                \
        }                                                                             \
    }

//! [doc example]

//
// Apply a single triangular/diagonal solve y = op(A)^-1 (alpha * x) with spsv,
// running the three required stages (buffer size, preprocess, compute).
//
static int spsv_solve(rocsparse_handle      handle,
                      rocsparse_operation   trans,
                      const void*           alpha,
                      rocsparse_spmat_descr A,
                      rocsparse_dnvec_descr x,
                      rocsparse_dnvec_descr y,
                      rocsparse_datatype    compute_type)
{
    size_t buffer_size;
    ROCSPARSE_CHECK(rocsparse_spsv(handle,
                                   trans,
                                   alpha,
                                   A,
                                   x,
                                   y,
                                   compute_type,
                                   rocsparse_spsv_alg_default,
                                   rocsparse_spsv_stage_buffer_size,
                                   &buffer_size,
                                   nullptr));

    void* buffer = nullptr;
    HIP_CHECK(hipMalloc(&buffer, (buffer_size > 0) ? buffer_size : 4));

    ROCSPARSE_CHECK(rocsparse_spsv(handle,
                                   trans,
                                   alpha,
                                   A,
                                   x,
                                   y,
                                   compute_type,
                                   rocsparse_spsv_alg_default,
                                   rocsparse_spsv_stage_preprocess,
                                   &buffer_size,
                                   buffer));

    ROCSPARSE_CHECK(rocsparse_spsv(handle,
                                   trans,
                                   alpha,
                                   A,
                                   x,
                                   y,
                                   compute_type,
                                   rocsparse_spsv_alg_default,
                                   rocsparse_spsv_stage_compute,
                                   &buffer_size,
                                   buffer));

    HIP_CHECK(hipFree(buffer));
    return 0;
}

//
// Factorize a symmetric/hermitian quasi-definite matrix with the incomplete
// LDL^H factorization (rocsparse_spildlt0), then solve A * x = b with a
// three-phase back-solve:
//   1. L   y = b   (lower, unit diagonal)
//   2. D   z = y   (diagonal only)
//   3. L^H x = z   (lower, unit diagonal, [conjugate] transposed)
//
// The factor matrix is reused for all three phases; only the fill mode, diagonal
// type and operation differ. A separate spmat descriptor is used per phase so
// that each gets its own triangular-solve analysis. The third phase uses
// rocsparse_operation_transpose in the real case and
// rocsparse_operation_conjugate_transpose in the complex (hermitian) case.
//
// T    : scalar type of the matrix values (real or std::complex).
// REAL : real type of the diagonal D produced by the factorization.
//
template <typename T, typename REAL>
static int run_ildlt_solve(const char*          label,
                           rocsparse_datatype   data_type,
                           rocsparse_index_base idx_base,
                           rocsparse_operation  herm_op,
                           int32_t              m,
                           int32_t              nnz,
                           const int32_t*       hcsr_row_ptr,
                           const int32_t*       hcsr_col_ind,
                           const T*             hcsr_val,
                           const T*             hb,
                           const T*             A_dense) // full m x m, row-major
{
    std::cout << "=== " << label << " ===" << std::endl;

    //
    // Offload the matrix and the right-hand side to the device.
    //
    int32_t* dcsr_row_ptr;
    int32_t* dcsr_col_ind;
    T*       dcsr_val;
    REAL*    ddiag; // dense diagonal output D (m real entries)
    T*       db; // right-hand side b
    T*       dy; // L^-1 b
    T*       dz; // D^-1 y
    T*       dx; // L^-H z (solution)
    HIP_CHECK(hipMalloc(&dcsr_row_ptr, sizeof(int32_t) * (m + 1)));
    HIP_CHECK(hipMalloc(&dcsr_col_ind, sizeof(int32_t) * nnz));
    HIP_CHECK(hipMalloc(&dcsr_val, sizeof(T) * nnz));
    HIP_CHECK(hipMalloc(&ddiag, sizeof(REAL) * m));
    HIP_CHECK(hipMalloc(&db, sizeof(T) * m));
    HIP_CHECK(hipMalloc(&dy, sizeof(T) * m));
    HIP_CHECK(hipMalloc(&dz, sizeof(T) * m));
    HIP_CHECK(hipMalloc(&dx, sizeof(T) * m));

    HIP_CHECK(
        hipMemcpy(dcsr_row_ptr, hcsr_row_ptr, sizeof(int32_t) * (m + 1), hipMemcpyHostToDevice));
    HIP_CHECK(hipMemcpy(dcsr_col_ind, hcsr_col_ind, sizeof(int32_t) * nnz, hipMemcpyHostToDevice));
    HIP_CHECK(hipMemcpy(dcsr_val, hcsr_val, sizeof(T) * nnz, hipMemcpyHostToDevice));
    HIP_CHECK(hipMemcpy(db, hb, sizeof(T) * m, hipMemcpyHostToDevice));

    rocsparse_handle handle;
    ROCSPARSE_CHECK(rocsparse_create_handle(&handle));

    hipStream_t stream;
    ROCSPARSE_CHECK(rocsparse_get_stream(handle, &stream));

    //
    // Create the sparse matrix descriptor for the factorization.
    //
    rocsparse_spmat_descr matA;
    ROCSPARSE_CHECK(rocsparse_create_csr_descr(&matA,
                                               m,
                                               m,
                                               nnz,
                                               dcsr_row_ptr,
                                               dcsr_col_ind,
                                               dcsr_val,
                                               rocsparse_indextype_i32,
                                               rocsparse_indextype_i32,
                                               idx_base,
                                               data_type));

    // =====================================================================
    // 1. Incomplete LDL^H factorization (rocsparse_spildlt0).
    //    On output, dcsr_val holds the strictly lower part of L (unit diagonal
    //    implicit) and D on the CSR diagonal, while ddiag holds D as a dense
    //    real-valued vector.
    // =====================================================================
    rocsparse_spildlt0_descr spildlt0_descr;
    ROCSPARSE_CHECK(rocsparse_spildlt0_descr_create(handle, &spildlt0_descr, nullptr));

    const rocsparse_spildlt0_alg spildlt0_alg = rocsparse_spildlt0_alg_default;
    ROCSPARSE_CHECK(rocsparse_spildlt0_set_input(handle,
                                                 spildlt0_descr,
                                                 rocsparse_spildlt0_input_alg,
                                                 &spildlt0_alg,
                                                 sizeof(spildlt0_alg),
                                                 nullptr));

    ROCSPARSE_CHECK(rocsparse_spildlt0_set_input(handle,
                                                 spildlt0_descr,
                                                 rocsparse_spildlt0_input_compute_datatype,
                                                 &data_type,
                                                 sizeof(data_type),
                                                 nullptr));

    const double singularity_tolerance = 1e-10;
    ROCSPARSE_CHECK(rocsparse_spildlt0_set_input(handle,
                                                 spildlt0_descr,
                                                 rocsparse_spildlt0_input_singularity_tolerance,
                                                 &singularity_tolerance,
                                                 sizeof(double),
                                                 nullptr));

    // Analysis stage.
    size_t spildlt0_buffer_size;
    void*  spildlt0_buffer = nullptr;
    ROCSPARSE_CHECK(rocsparse_spildlt0_buffer_size(handle,
                                                   spildlt0_descr,
                                                   matA,
                                                   matA,
                                                   rocsparse_spildlt0_stage_analysis,
                                                   &spildlt0_buffer_size,
                                                   nullptr));
    HIP_CHECK(hipMalloc(&spildlt0_buffer, spildlt0_buffer_size));
    ROCSPARSE_CHECK(rocsparse_spildlt0(handle,
                                       spildlt0_descr,
                                       matA,
                                       matA,
                                       rocsparse_spildlt0_stage_analysis,
                                       spildlt0_buffer_size,
                                       spildlt0_buffer,
                                       nullptr));

    // Check for any singularities after analysis.
    ROCSPARSE_CHECK(rocsparse_set_pointer_mode(handle, rocsparse_pointer_mode_host));
    rocsparse_singularity post_analysis_singularity;
    int64_t               singularity_position;
    ROCSPARSE_CHECK(rocsparse_spildlt0_get_output(handle,
                                                  spildlt0_descr,
                                                  rocsparse_spildlt0_output_singularity,
                                                  &post_analysis_singularity,
                                                  sizeof(rocsparse_singularity),
                                                  nullptr));
    ROCSPARSE_CHECK(rocsparse_spildlt0_get_output(handle,
                                                  spildlt0_descr,
                                                  rocsparse_spildlt0_output_singularity_position,
                                                  &singularity_position,
                                                  sizeof(int64_t),
                                                  nullptr));
    HIP_CHECK(hipStreamSynchronize(stream));

    if(post_analysis_singularity == rocsparse_singularity_symbolic)
    {
        std::cout << "symbolic singularity detected at position: " << singularity_position
                  << std::endl;
        ROCSPARSE_CHECK(rocsparse_status_zero_pivot);
    }

    // Provide the dense diagonal output pointer before the compute stage.
    ROCSPARSE_CHECK(rocsparse_spildlt0_set_input(
        handle, spildlt0_descr, rocsparse_spildlt0_input_diag, &ddiag, sizeof(void*), nullptr));

    // Compute stage.
    ROCSPARSE_CHECK(rocsparse_spildlt0_buffer_size(handle,
                                                   spildlt0_descr,
                                                   matA,
                                                   matA,
                                                   rocsparse_spildlt0_stage_compute,
                                                   &spildlt0_buffer_size,
                                                   nullptr));
    HIP_CHECK(hipFree(spildlt0_buffer));
    HIP_CHECK(hipMalloc(&spildlt0_buffer, spildlt0_buffer_size));
    ROCSPARSE_CHECK(rocsparse_spildlt0(handle,
                                       spildlt0_descr,
                                       matA,
                                       matA,
                                       rocsparse_spildlt0_stage_compute,
                                       spildlt0_buffer_size,
                                       spildlt0_buffer,
                                       nullptr));

    // Check for any singularities after compute.
    rocsparse_singularity post_compute_singularity;
    ROCSPARSE_CHECK(rocsparse_spildlt0_get_output(handle,
                                                  spildlt0_descr,
                                                  rocsparse_spildlt0_output_singularity,
                                                  &post_compute_singularity,
                                                  sizeof(rocsparse_singularity),
                                                  nullptr));
    ROCSPARSE_CHECK(rocsparse_spildlt0_get_output(handle,
                                                  spildlt0_descr,
                                                  rocsparse_spildlt0_output_singularity_position,
                                                  &singularity_position,
                                                  sizeof(int64_t),
                                                  nullptr));
    HIP_CHECK(hipStreamSynchronize(stream));

    if(post_compute_singularity == rocsparse_singularity_numeric_exact
       || post_compute_singularity == rocsparse_singularity_numeric_near)
    {
        std::cout << "numeric singularity detected at position: " << singularity_position
                  << std::endl;
    }

    HIP_CHECK(hipFree(spildlt0_buffer));
    ROCSPARSE_CHECK(rocsparse_spildlt0_descr_destroy(handle, spildlt0_descr, nullptr));

    // =====================================================================
    // 2. Three-phase back-solve with spsv.
    // =====================================================================
    const T                   alpha         = static_cast<T>(1);
    const rocsparse_fill_mode fill_lower    = rocsparse_fill_mode_lower;
    const rocsparse_fill_mode fill_diagonal = rocsparse_fill_mode_diagonal;
    const rocsparse_diag_type diag_unit     = rocsparse_diag_type_unit;
    const rocsparse_diag_type diag_non_unit = rocsparse_diag_type_non_unit;

    rocsparse_dnvec_descr vecB, vecY, vecZ, vecX;
    ROCSPARSE_CHECK(rocsparse_create_dnvec_descr(&vecB, m, db, data_type));
    ROCSPARSE_CHECK(rocsparse_create_dnvec_descr(&vecY, m, dy, data_type));
    ROCSPARSE_CHECK(rocsparse_create_dnvec_descr(&vecZ, m, dz, data_type));
    ROCSPARSE_CHECK(rocsparse_create_dnvec_descr(&vecX, m, dx, data_type));

    auto make_factor_descr = [&](rocsparse_spmat_descr* descr,
                                 rocsparse_fill_mode    fill,
                                 rocsparse_diag_type    diag) -> rocsparse_status {
        rocsparse_status status = rocsparse_create_csr_descr(descr,
                                                             m,
                                                             m,
                                                             nnz,
                                                             dcsr_row_ptr,
                                                             dcsr_col_ind,
                                                             dcsr_val,
                                                             rocsparse_indextype_i32,
                                                             rocsparse_indextype_i32,
                                                             idx_base,
                                                             data_type);
        if(status != rocsparse_status_success)
        {
            return status;
        }
        status
            = rocsparse_spmat_set_attribute(*descr, rocsparse_spmat_fill_mode, &fill, sizeof(fill));
        if(status != rocsparse_status_success)
        {
            return status;
        }
        return rocsparse_spmat_set_attribute(
            *descr, rocsparse_spmat_diag_type, &diag, sizeof(diag));
    };

    // Factor descriptors: L (unit lower), D (diagonal), L^H (unit lower, [conj] transposed).
    rocsparse_spmat_descr matL, matD, matLt;
    ROCSPARSE_CHECK(make_factor_descr(&matL, fill_lower, diag_unit));
    ROCSPARSE_CHECK(make_factor_descr(&matD, fill_diagonal, diag_non_unit));
    ROCSPARSE_CHECK(make_factor_descr(&matLt, fill_lower, diag_unit));

    // Phase 1: solve L y = b.
    if(spsv_solve(handle, rocsparse_operation_none, &alpha, matL, vecB, vecY, data_type) != 0)
    {
        return -1;
    }

    // Phase 2: solve D z = y (diagonal-only solve).
    if(spsv_solve(handle, rocsparse_operation_none, &alpha, matD, vecY, vecZ, data_type) != 0)
    {
        return -1;
    }

    // Phase 3: solve L^H x = z (transpose for real, conjugate transpose for complex).
    if(spsv_solve(handle, herm_op, &alpha, matLt, vecZ, vecX, data_type) != 0)
    {
        return -1;
    }

    //
    // Copy the solution back to the host and report the residual ||A x - b||_inf.
    //
    std::vector<T> hx(m);
    HIP_CHECK(hipMemcpy(hx.data(), dx, sizeof(T) * m, hipMemcpyDeviceToHost));

    REAL residual = static_cast<REAL>(0);
    std::cout << "Solution x =";
    for(int i = 0; i < m; ++i)
    {
        T axi = static_cast<T>(0);
        for(int j = 0; j < m; ++j)
        {
            axi += A_dense[i * m + j] * hx[j];
        }
        const REAL ri = std::abs(axi - hb[i]);
        residual      = (ri > residual) ? ri : residual;
        std::cout << " " << hx[i];
    }
    std::cout << std::endl;
    std::cout << "Residual ||A x - b||_inf = " << residual << std::endl;

    //
    // Clean up.
    //
    ROCSPARSE_CHECK(rocsparse_destroy_dnvec_descr(vecB));
    ROCSPARSE_CHECK(rocsparse_destroy_dnvec_descr(vecY));
    ROCSPARSE_CHECK(rocsparse_destroy_dnvec_descr(vecZ));
    ROCSPARSE_CHECK(rocsparse_destroy_dnvec_descr(vecX));
    ROCSPARSE_CHECK(rocsparse_destroy_spmat_descr(matL));
    ROCSPARSE_CHECK(rocsparse_destroy_spmat_descr(matD));
    ROCSPARSE_CHECK(rocsparse_destroy_spmat_descr(matLt));
    ROCSPARSE_CHECK(rocsparse_destroy_spmat_descr(matA));
    ROCSPARSE_CHECK(rocsparse_destroy_handle(handle));

    HIP_CHECK(hipFree(dcsr_row_ptr));
    HIP_CHECK(hipFree(dcsr_col_ind));
    HIP_CHECK(hipFree(dcsr_val));
    HIP_CHECK(hipFree(ddiag));
    HIP_CHECK(hipFree(db));
    HIP_CHECK(hipFree(dy));
    HIP_CHECK(hipFree(dz));
    HIP_CHECK(hipFree(dx));

    return 0;
}

int main()
{
    static constexpr int32_t m   = 4;
    static constexpr int32_t nnz = 7;

    //
    // Example 1: real symmetric quasi-definite (SQD) matrix, zero-based indexing.
    //
    //     4  1 | 0  0
    // A = 1  3 | 1  0
    //     ----+-----
    //     0  1 | -2 1
    //     0  0 | 1 -2
    //
    // The (1,1) block is positive definite and the (2,2) block is negative
    // definite, so A is SQD. The tridiagonal pattern produces no fill-in, hence
    // ILDLT(0) is exact and the back-solve recovers x = [1, 1, 1, 1] exactly.
    //
    {
        const int32_t row_ptr[m + 1] = {0, 1, 3, 5, 7};
        const int32_t col_ind[nnz]   = {0, 0, 1, 1, 2, 2, 3};
        const double  val[nnz]       = {4.0, 1.0, 3.0, 1.0, -2.0, 1.0, -2.0};
        const double  b[m]           = {5.0, 5.0, 0.0, -1.0};

        // Full symmetric matrix (row-major) used only for the residual check.
        const double A_dense[m * m]
            = {4.0, 1.0, 0.0, 0.0, 1.0, 3.0, 1.0, 0.0, 0.0, 1.0, -2.0, 1.0, 0.0, 0.0, 1.0, -2.0};

        if(run_ildlt_solve<double, double>("real SQD, zero-based",
                                           rocsparse_datatype_f64_r,
                                           rocsparse_index_base_zero,
                                           rocsparse_operation_transpose,
                                           m,
                                           nnz,
                                           row_ptr,
                                           col_ind,
                                           val,
                                           b,
                                           A_dense)
           != 0)
        {
            return -1;
        }
    }

    //
    // Example 2: complex hermitian quasi-definite matrix, one-based indexing.
    //
    //     4       1-i  | 0     0
    // A = 1+i     3    | -i    0
    //     -----------+-----------
    //     0       i    | -2    1+i
    //     0       0    | 1-i   -2
    //
    // A is hermitian (A = A^H) with real diagonal; the (1,1) block is positive
    // definite and the (2,2) block is negative definite. ILDLT(0) computes
    // A = L D L^H with real D, and the third solve phase uses the conjugate
    // transpose. The back-solve recovers x = [1, 1, 1, 1].
    //
    {
        using cd = std::complex<double>;

        // One-based indexing: both row_ptr and col_ind use base 1.
        const int32_t row_ptr[m + 1] = {1, 2, 4, 6, 8};
        const int32_t col_ind[nnz]   = {1, 1, 2, 2, 3, 3, 4};
        const cd      val[nnz]       = {cd(4.0, 0.0),
                             cd(1.0, 1.0),
                             cd(3.0, 0.0),
                             cd(0.0, 1.0),
                             cd(-2.0, 0.0),
                             cd(1.0, -1.0),
                             cd(-2.0, 0.0)};
        const cd      b[m]           = {cd(5.0, -1.0), cd(4.0, 0.0), cd(-1.0, 2.0), cd(-1.0, -1.0)};

        // Full hermitian matrix (row-major) used only for the residual check.
        const cd A_dense[m * m] = {cd(4.0, 0.0),
                                   cd(1.0, -1.0),
                                   cd(0.0, 0.0),
                                   cd(0.0, 0.0),
                                   cd(1.0, 1.0),
                                   cd(3.0, 0.0),
                                   cd(0.0, -1.0),
                                   cd(0.0, 0.0),
                                   cd(0.0, 0.0),
                                   cd(0.0, 1.0),
                                   cd(-2.0, 0.0),
                                   cd(1.0, 1.0),
                                   cd(0.0, 0.0),
                                   cd(0.0, 0.0),
                                   cd(1.0, -1.0),
                                   cd(-2.0, 0.0)};

        // std::complex<double> has the same memory layout as
        // rocsparse_double_complex, so the device buffers (described with
        // rocsparse_datatype_f64_c) can be filled directly from std::complex data.
        if(run_ildlt_solve<cd, double>("complex hermitian, one-based",
                                       rocsparse_datatype_f64_c,
                                       rocsparse_index_base_one,
                                       rocsparse_operation_conjugate_transpose,
                                       m,
                                       nnz,
                                       row_ptr,
                                       col_ind,
                                       val,
                                       b,
                                       A_dense)
           != 0)
        {
            return -1;
        }
    }

    return 0;
}
//! [doc example]
