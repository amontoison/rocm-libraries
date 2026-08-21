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
int main()
{
    //
    // Combined incomplete LDL^T factor of a matrix A = L * D * L^T, with unit-lower L
    // (sub-diagonal entries) and diagonal D stored in a single matrix. Provided directly
    // for brevity; in practice it is produced by rocsparse_spildlt0.
    //
    //        4 0 0 0        1 0 0 0        4 0 0 0
    // LDL' = 1 4 0 0  ( L = 1 1 0 0 , D = 0 4 0 0 )
    //        0 1 4 0        0 1 1 0        0 0 4 0
    //        0 0 1 4        0 0 1 1        0 0 0 4
    //
    static constexpr int32_t              m         = 4;
    static constexpr int32_t              nnz       = 7;
    static constexpr int32_t              nrhs      = 2;
    static constexpr rocsparse_index_base idx_base  = rocsparse_index_base_zero;
    static constexpr rocsparse_indextype  idx_type  = rocsparse_indextype_i32;
    static constexpr rocsparse_datatype   data_type = rocsparse_datatype_f64_r;

    const int32_t hcsr_row_ptr[m + 1]
        = {idx_base + 0, idx_base + 1, idx_base + 3, idx_base + 5, idx_base + 7};
    const int32_t hcsr_col_ind[nnz] = {idx_base + 0,
                                       idx_base + 0,
                                       idx_base + 1,
                                       idx_base + 1,
                                       idx_base + 2,
                                       idx_base + 2,
                                       idx_base + 3};
    const double  hcsr_val[nnz]     = {4, 1, 4, 1, 4, 1, 4};

    // Two right-hand sides, stored column-major (m x nrhs).
    const double hB[m * nrhs] = {1, 2, 3, 4, 4, 3, 2, 1};

    //
    // Offload the factor and the right-hand side to the device.
    //
    int32_t* dcsr_row_ptr;
    int32_t* dcsr_col_ind;
    double*  dcsr_val;
    double*  dB;
    double*  dX;
    HIP_CHECK(hipMalloc(&dcsr_row_ptr, sizeof(int32_t) * (m + 1)));
    HIP_CHECK(hipMalloc(&dcsr_col_ind, sizeof(int32_t) * nnz));
    HIP_CHECK(hipMalloc(&dcsr_val, sizeof(double) * nnz));
    HIP_CHECK(hipMalloc(&dB, sizeof(double) * m * nrhs));
    HIP_CHECK(hipMalloc(&dX, sizeof(double) * m * nrhs));
    HIP_CHECK(
        hipMemcpy(dcsr_row_ptr, hcsr_row_ptr, sizeof(int32_t) * (m + 1), hipMemcpyHostToDevice));
    HIP_CHECK(hipMemcpy(dcsr_col_ind, hcsr_col_ind, sizeof(int32_t) * nnz, hipMemcpyHostToDevice));
    HIP_CHECK(hipMemcpy(dcsr_val, hcsr_val, sizeof(double) * nnz, hipMemcpyHostToDevice));
    HIP_CHECK(hipMemcpy(dB, hB, sizeof(double) * m * nrhs, hipMemcpyHostToDevice));

    rocsparse_handle handle;
    ROCSPARSE_CHECK(rocsparse_create_handle(&handle));

    //
    // Create the sparse combined LDL^T factor. Its fill mode and diagonal type are
    // selected internally per sweep, so no attributes need to be set here.
    //
    rocsparse_spmat_descr matL;
    ROCSPARSE_CHECK(rocsparse_create_csr_descr(&matL,
                                               m,
                                               m,
                                               nnz,
                                               dcsr_row_ptr,
                                               dcsr_col_ind,
                                               dcsr_val,
                                               idx_type,
                                               idx_type,
                                               idx_base,
                                               data_type));

    //
    // Create the dense right-hand side and solution matrices (column-major).
    //
    rocsparse_dnmat_descr matB, matX;
    ROCSPARSE_CHECK(
        rocsparse_create_dnmat_descr(&matB, m, nrhs, m, dB, data_type, rocsparse_order_column));
    ROCSPARSE_CHECK(
        rocsparse_create_dnmat_descr(&matX, m, nrhs, m, dX, data_type, rocsparse_order_column));

    //
    // Create and configure the SpILDLT0 solve descriptor.
    //
    rocsparse_spildlt0_solve_descr descr;
    ROCSPARSE_CHECK(rocsparse_spildlt0_solve_descr_create(handle, &descr, nullptr));

    const rocsparse_datatype compute_datatype = data_type;
    ROCSPARSE_CHECK(
        rocsparse_spildlt0_solve_set_input(handle,
                                           descr,
                                           rocsparse_spildlt0_solve_input_compute_datatype,
                                           &compute_datatype,
                                           sizeof(compute_datatype),
                                           nullptr));

    //
    // Query the buffer size, allocate the buffer, and run the analysis + solve stages.
    //
    size_t buffer_size;
    void*  buffer;
    ROCSPARSE_CHECK(rocsparse_spildlt0_solve_buffer_size(handle,
                                                         descr,
                                                         matL,
                                                         matB,
                                                         matX,
                                                         rocsparse_spildlt0_solve_stage_analysis,
                                                         &buffer_size,
                                                         nullptr));
    HIP_CHECK(hipMalloc(&buffer, buffer_size));

    ROCSPARSE_CHECK(rocsparse_spildlt0_solve(handle,
                                             descr,
                                             matL,
                                             matB,
                                             matX,
                                             rocsparse_spildlt0_solve_stage_analysis,
                                             buffer_size,
                                             buffer,
                                             nullptr));

    ROCSPARSE_CHECK(rocsparse_spildlt0_solve(handle,
                                             descr,
                                             matL,
                                             matB,
                                             matX,
                                             rocsparse_spildlt0_solve_stage_solve,
                                             buffer_size,
                                             buffer,
                                             nullptr));

    //
    // Retrieve the solution X such that (L L^T) X = B.
    //
    double hX[m * nrhs];
    HIP_CHECK(hipMemcpy(hX, dX, sizeof(double) * m * nrhs, hipMemcpyDeviceToHost));
    for(int32_t j = 0; j < nrhs; ++j)
    {
        std::cout << "x[:," << j << "] =";
        for(int32_t i = 0; i < m; ++i)
        {
            std::cout << " " << hX[i + j * m];
        }
        std::cout << std::endl;
    }

    //
    // Clean up.
    //
    HIP_CHECK(hipFree(buffer));
    ROCSPARSE_CHECK(rocsparse_spildlt0_solve_descr_destroy(handle, descr, nullptr));
    ROCSPARSE_CHECK(rocsparse_destroy_dnmat_descr(matB));
    ROCSPARSE_CHECK(rocsparse_destroy_dnmat_descr(matX));
    ROCSPARSE_CHECK(rocsparse_destroy_spmat_descr(matL));
    ROCSPARSE_CHECK(rocsparse_destroy_handle(handle));
    HIP_CHECK(hipFree(dcsr_row_ptr));
    HIP_CHECK(hipFree(dcsr_col_ind));
    HIP_CHECK(hipFree(dcsr_val));
    HIP_CHECK(hipFree(dB));
    HIP_CHECK(hipFree(dX));

    return 0;
}
//! [doc example]
