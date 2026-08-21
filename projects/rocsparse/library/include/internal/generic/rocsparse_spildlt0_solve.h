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
#ifndef ROCSPARSE_SPILDLT0_SOLVE_H
#define ROCSPARSE_SPILDLT0_SOLVE_H

#include "../../rocsparse-types.h"
#include "rocsparse/rocsparse-export.h"

#ifdef __cplusplus
extern "C" {
#endif

/*! \ingroup generic_module
 *  \brief Triangular backsolve on an incomplete LDL^T factor of level 0.
 *
 *  \details
 *  \p rocsparse_spildlt0_solve_buffer_size returns the size of the temporary buffer
 *  that is required by \ref rocsparse_spildlt0_solve and must be allocated by the user.
 *
 *  \note
 *  This routine is non-blocking and executed asynchronously with respect to the host.
 *
 *  \note
 *  The supported sparse format for the factor \p A is \ref rocsparse_format_csr.
 *
 *  @param[in]
 *  handle                  handle to the rocSPARSE library context queue.
 *  @param[in]
 *  spildlt0_solve_descr       SpILDLT0 solve descriptor.
 *  @param[in]
 *  A                       descriptor of the combined incomplete LDL^T factor (unit lower L and diagonal D).
 *  @param[in]
 *  B                       descriptor of the dense right-hand side matrix.
 *  @param[in]
 *  X                       descriptor of the dense solution matrix. In-place \p X = \p B is allowed.
 *  @param[in]
 *  spildlt0_solve_stage       stage for the SpILDLT0 solve computation.
 *  @param[out]
 *  p_buffer_size_in_bytes  number of bytes of the buffer.
 *  @param[out]
 *  p_error                 error descriptor created if the returned status is not \ref rocsparse_status_success. A null pointer can be passed if an error descriptor is not required.
 *
 *  \retval rocsparse_status_success the operation completed successfully.
 *  \retval rocsparse_status_invalid_handle the library context was not initialized.
 *  \retval rocsparse_status_not_implemented the sparse format of \p A is not supported.
 *  \retval rocsparse_status_invalid_value the \p spildlt0_solve_stage value is invalid.
 *  \retval rocsparse_status_invalid_pointer \p spildlt0_solve_descr, \p A, \p B, \p X, or \p p_buffer_size_in_bytes pointer is invalid.
 */
ROCSPARSE_EXPORT
rocsparse_status
    rocsparse_spildlt0_solve_buffer_size(rocsparse_handle               handle,
                                         rocsparse_spildlt0_solve_descr spildlt0_solve_descr,
                                         rocsparse_const_spmat_descr    A,
                                         rocsparse_const_dnmat_descr    B,
                                         rocsparse_const_dnmat_descr    X,
                                         rocsparse_spildlt0_solve_stage spildlt0_solve_stage,
                                         size_t*                        p_buffer_size_in_bytes,
                                         rocsparse_error*               p_error);

/*! \ingroup generic_module
 *  \brief Triangular backsolve on an incomplete LDL^T factor of level 0.
 *
 *  \details
 *  \p rocsparse_spildlt0_solve solves the linear system \f$A X = B\f$ where the sparse
 *  \f$m \times m\f$ matrix \f$A\f$ has been factorized as \f$A \approx L D L^H\f$ by
 *  \ref rocsparse_spildlt0 and stored as a single combined factor (unit lower \f$L\f$ and
 *  diagonal \f$D\f$). The routine chains the forward sweep, diagonal scale and backward sweep
 *  \f[
 *    L Y = B, \qquad Y \leftarrow D^{-1} Y, \qquad L^H X = Y
 *  \f]
 *  in a single call, hiding the fill mode, diagonal type and transpose selection from the user.
 *
 *  Performing the above operation requires two stages, the stage \ref rocsparse_spildlt0_solve_stage_analysis
 *  and the stage \ref rocsparse_spildlt0_solve_stage_solve. The stage \ref rocsparse_spildlt0_solve_stage_analysis
 *  is required to perform the stage \ref rocsparse_spildlt0_solve_stage_solve and only needs to be called once
 *  for a given sparsity pattern of \f$A\f$, while the stage \ref rocsparse_spildlt0_solve_stage_solve can be
 *  repeatedly used with different right-hand sides.
 *
 *  \p rocsparse_spildlt0_solve supports the following data types for \p A, \p B and \p X:
 *  \ref rocsparse_datatype_f32_r, \ref rocsparse_datatype_f64_r, \ref rocsparse_datatype_f32_c, and \ref rocsparse_datatype_f64_c.
 *
 *  \note The descriptor \p spildlt0_solve_descr needs to be configured with \ref rocsparse_spildlt0_solve_set_input.
 *  \note The factor \p A must be provided as a \ref rocsparse_format_csr matrix; it is solved as
 *        unit-lower, diagonal, then unit-lower-transpose, so its fill mode and diagonal type are selected internally.
 *  \note Multiple right-hand sides are supported; a single right-hand side is expressed as a dense matrix with one column.
 *
 *  @param[in]
 *  handle                  handle to the rocSPARSE library context queue.
 *  @param[in]
 *  spildlt0_solve_descr       SpILDLT0 solve descriptor.
 *  @param[in]
 *  A                       descriptor of the combined incomplete LDL^T factor (unit lower L and diagonal D).
 *  @param[in]
 *  B                       descriptor of the dense right-hand side matrix.
 *  @param[out]
 *  X                       descriptor of the dense solution matrix. In-place \p X = \p B is allowed.
 *  @param[in]
 *  spildlt0_solve_stage       stage for the SpILDLT0 solve computation.
 *  @param[in]
 *  buffer_size_in_bytes    number of bytes of the buffer.
 *  @param[in]
 *  buffer                  buffer allocated by the user.
 *  @param[out]
 *  p_error                 error descriptor created if the returned status is not \ref rocsparse_status_success. A null pointer can be passed if an error descriptor is not required.
 *
 *  \retval rocsparse_status_success the operation completed successfully.
 *  \retval rocsparse_status_invalid_handle the library context was not initialized.
 *  \retval rocsparse_status_not_implemented the sparse format of \p A is not supported.
 *  \retval rocsparse_status_invalid_value the \p spildlt0_solve_stage value is invalid.
 *  \retval rocsparse_status_invalid_pointer \p spildlt0_solve_descr, \p A, \p B, or \p X pointer is invalid.
 *
 *  \par Example
 *  \snippet example_rocsparse_spildlt0_solve.cpp doc example
 */
ROCSPARSE_EXPORT
rocsparse_status rocsparse_spildlt0_solve(rocsparse_handle               handle,
                                          rocsparse_spildlt0_solve_descr spildlt0_solve_descr,
                                          rocsparse_const_spmat_descr    A,
                                          rocsparse_const_dnmat_descr    B,
                                          rocsparse_dnmat_descr          X,
                                          rocsparse_spildlt0_solve_stage spildlt0_solve_stage,
                                          size_t                         buffer_size_in_bytes,
                                          void*                          buffer,
                                          rocsparse_error*               p_error);

#ifdef __cplusplus
}
#endif
#endif
