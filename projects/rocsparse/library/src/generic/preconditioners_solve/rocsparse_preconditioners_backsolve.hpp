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

// Factorization-agnostic driver shared by the preconditioner backsolve routines
// (rocsparse_spic0_solve, rocsparse_spilu0_solve, rocsparse_spildlt0_solve). A
// backsolve is an ordered list of triangular sweeps solving op(M) out = in, chained
// through internal ping-pong intermediates; e.g. Cholesky is { (L, none), (L, T) }.

#pragma once

#include "rocsparse_common.hpp"
#include "rocsparse_datatype_utils.hpp"
#include "rocsparse_handle.hpp"
#include "rocsparse_utility.hpp"

#include "internal/generic/rocsparse_spsm.h"

namespace rocsparse
{
    // A triangular sweep op(matrix) out = in. fill_mode and diag_type are applied to
    // matrix->descr for the sweep, so one matrix may serve several sweeps with
    // different attributes (e.g. the combined LU factor: unit-lower then non-unit-upper).
    struct backsolve_sweep
    {
        rocsparse_const_spmat_descr matrix;
        rocsparse_operation         operation;
        rocsparse_fill_mode         fill_mode;
        rocsparse_diag_type         diag_type;
    };

    namespace backsolve
    {
        // Applies a sweep's fill mode / diagonal type to matrix->descr for the
        // duration of one spsm call, restoring the originals on scope exit.
        struct sweep_attribute_scope
        {
            rocsparse_mat_descr m_descr;
            rocsparse_fill_mode m_fill;
            rocsparse_diag_type m_diag;
            explicit sweep_attribute_scope(const rocsparse::backsolve_sweep& sweep)
                : m_descr(sweep.matrix->descr)
                , m_fill(sweep.matrix->descr->fill_mode)
                , m_diag(sweep.matrix->descr->diag_type)
            {
                m_descr->fill_mode = sweep.fill_mode;
                m_descr->diag_type = sweep.diag_type;
            }
            ~sweep_attribute_scope()
            {
                m_descr->fill_mode = m_fill;
                m_descr->diag_type = m_diag;
            }
        };

        // Alignment of each intermediate block so the spsm sub-buffer that follows
        // stays aligned.
        static constexpr size_t alignment = 256;

        inline size_t block_bytes(int64_t m, int64_t nrhs, rocsparse_datatype datatype)
        {
            const size_t raw = static_cast<size_t>(m) * static_cast<size_t>(nrhs)
                               * rocsparse::datatype_sizeof(datatype);
            return ((raw + alignment - 1) / alignment) * alignment;
        }

        // Ping-pong intermediates needed to chain n_sweeps: the last sweep writes X,
        // the interior ones alternate between two scratch blocks.
        inline int intermediate_count(int n_sweeps)
        {
            return (n_sweeps <= 1) ? 0 : ((n_sweeps == 2) ? 1 : 2);
        }

        // The sweeps always solve with alpha = 1.
        inline const void* device_one(rocsparse_handle handle, rocsparse_datatype datatype)
        {
            switch(datatype)
            {
            case rocsparse_datatype_f32_r:
            case rocsparse_datatype_f32_c:
            {
                return handle->sone;
            }
            case rocsparse_datatype_f64_r:
            case rocsparse_datatype_f64_c:
            {
                return handle->done;
            }
            default:
            {
                return nullptr;
            }
            }
        }

        // Forces device pointer mode so spsm reads the alpha = 1 constant from the
        // handle's device buffers, restoring the caller's mode on scope exit.
        struct device_pointer_mode_guard
        {
            rocsparse_handle       m_handle;
            rocsparse_pointer_mode m_saved;
            explicit device_pointer_mode_guard(rocsparse_handle handle)
                : m_handle(handle)
                , m_saved(handle->pointer_mode)
            {
                m_handle->pointer_mode = rocsparse_pointer_mode_device;
            }
            ~device_pointer_mode_guard()
            {
                m_handle->pointer_mode = m_saved;
            }
        };

        inline rocsparse_status create_intermediate(rocsparse_dnmat_descr* Y,
                                                    int64_t                m,
                                                    int64_t                nrhs,
                                                    rocsparse_datatype     datatype,
                                                    rocsparse_order        order,
                                                    void*                  values)
        {
            const int64_t ld = (order == rocsparse_order_column) ? m : nrhs;
            RETURN_IF_ROCSPARSE_ERROR(
                rocsparse_create_dnmat_descr(Y, m, nrhs, ld, values, datatype, order));
            return rocsparse_status_success;
        }
    }

    // Ping-pong intermediates plus the largest spsm buffer over all sweeps.
    inline rocsparse_status backsolve_buffer_size(rocsparse_handle                  handle,
                                                  const rocsparse::backsolve_sweep* sweeps,
                                                  int                               n_sweeps,
                                                  rocsparse_const_dnmat_descr       B,
                                                  rocsparse_const_dnmat_descr       X,
                                                  size_t*                           buffer_size)
    {
        const rocsparse_datatype datatype = B->data_type;
        const int64_t            m        = B->rows;
        const int64_t            nrhs     = B->cols;

        const size_t block     = rocsparse::backsolve::block_bytes(m, nrhs, datatype);
        const int    n_int     = rocsparse::backsolve::intermediate_count(n_sweeps);
        const size_t int_bytes = static_cast<size_t>(n_int) * block;

        // Sizing never dereferences the values pointer, so a dummy is enough.
        char                  dummy = 0;
        rocsparse_dnmat_descr Y{};
        RETURN_IF_ROCSPARSE_ERROR(rocsparse::backsolve::create_intermediate(
            &Y, m, nrhs, datatype, X->order, static_cast<void*>(&dummy)));

        char             alpha_dummy = 0;
        size_t           max_spsm    = 0;
        rocsparse_status status      = rocsparse_status_success;
        for(int i = 0; i < n_sweeps; ++i)
        {
            rocsparse::backsolve::sweep_attribute_scope scope(sweeps[i]);
            rocsparse_const_dnmat_descr                 in = (i == 0) ? B : Y;
            size_t                                      s  = 0;
            status                                         = rocsparse_spsm(handle,
                                    sweeps[i].operation,
                                    rocsparse_operation_none,
                                    static_cast<const void*>(&alpha_dummy),
                                    sweeps[i].matrix,
                                    in,
                                    Y,
                                    datatype,
                                    rocsparse_spsm_alg_default,
                                    rocsparse_spsm_stage_buffer_size,
                                    &s,
                                    nullptr);
            if(status != rocsparse_status_success)
            {
                break;
            }
            max_spsm = rocsparse::max(max_spsm, s);
        }

        RETURN_IF_ROCSPARSE_ERROR(rocsparse_destroy_dnmat_descr(Y));
        RETURN_IF_ROCSPARSE_ERROR(status);

        *buffer_size = rocsparse::max(static_cast<size_t>(4), int_bytes + max_spsm);
        return rocsparse_status_success;
    }

    inline rocsparse_status backsolve_analysis(rocsparse_handle                  handle,
                                               const rocsparse::backsolve_sweep* sweeps,
                                               int                               n_sweeps,
                                               rocsparse_const_dnmat_descr       B,
                                               rocsparse_dnmat_descr             X,
                                               void*                             buffer)
    {
        const rocsparse_datatype datatype = B->data_type;
        const int64_t            m        = B->rows;
        const int64_t            nrhs     = B->cols;

        const size_t block    = rocsparse::backsolve::block_bytes(m, nrhs, datatype);
        const int    n_int    = rocsparse::backsolve::intermediate_count(n_sweeps);
        void*        spsm_buf = static_cast<char*>(buffer) + static_cast<size_t>(n_int) * block;

        // Every sweep shares the same dimensions, so B and X analyse them all.
        char alpha_dummy = 0;
        for(int i = 0; i < n_sweeps; ++i)
        {
            rocsparse::backsolve::sweep_attribute_scope scope(sweeps[i]);
            RETURN_IF_ROCSPARSE_ERROR(rocsparse_spsm(handle,
                                                     sweeps[i].operation,
                                                     rocsparse_operation_none,
                                                     static_cast<const void*>(&alpha_dummy),
                                                     sweeps[i].matrix,
                                                     B,
                                                     X,
                                                     datatype,
                                                     rocsparse_spsm_alg_default,
                                                     rocsparse_spsm_stage_preprocess,
                                                     nullptr,
                                                     spsm_buf));
        }
        return rocsparse_status_success;
    }

    // Chains B -> ... -> X, one spsm compute per sweep.
    inline rocsparse_status backsolve_solve(rocsparse_handle                  handle,
                                            const rocsparse::backsolve_sweep* sweeps,
                                            int                               n_sweeps,
                                            rocsparse_const_dnmat_descr       B,
                                            rocsparse_dnmat_descr             X,
                                            void*                             buffer)
    {
        const rocsparse_datatype datatype = B->data_type;
        const int64_t            m        = B->rows;
        const int64_t            nrhs     = B->cols;

        const size_t block    = rocsparse::backsolve::block_bytes(m, nrhs, datatype);
        const int    n_int    = rocsparse::backsolve::intermediate_count(n_sweeps);
        void*        spsm_buf = static_cast<char*>(buffer) + static_cast<size_t>(n_int) * block;

        rocsparse_dnmat_descr intermediates[2] = {};
        rocsparse_status      status           = rocsparse_status_success;
        for(int j = 0; j < n_int; ++j)
        {
            void* values = static_cast<char*>(buffer) + static_cast<size_t>(j) * block;
            status       = rocsparse::backsolve::create_intermediate(
                &intermediates[j], m, nrhs, datatype, X->order, values);
            if(status != rocsparse_status_success)
            {
                break;
            }
        }

        if(status == rocsparse_status_success)
        {
            const void* alpha = rocsparse::backsolve::device_one(handle, datatype);
            rocsparse::backsolve::device_pointer_mode_guard pm_guard(handle);

            rocsparse_const_dnmat_descr in = B;
            for(int i = 0; i < n_sweeps; ++i)
            {
                rocsparse::backsolve::sweep_attribute_scope scope(sweeps[i]);
                rocsparse_dnmat_descr out = (i == n_sweeps - 1) ? X : intermediates[i % n_int];
                status                    = rocsparse_spsm(handle,
                                        sweeps[i].operation,
                                        rocsparse_operation_none,
                                        alpha,
                                        sweeps[i].matrix,
                                        in,
                                        out,
                                        datatype,
                                        rocsparse_spsm_alg_default,
                                        rocsparse_spsm_stage_compute,
                                        nullptr,
                                        spsm_buf);
                if(status != rocsparse_status_success)
                {
                    break;
                }
                in = out;
            }
        }

        for(int j = 0; j < n_int; ++j)
        {
            if(intermediates[j] != nullptr)
            {
                const rocsparse_status destroy_status
                    = rocsparse_destroy_dnmat_descr(intermediates[j]);
                if(status == rocsparse_status_success)
                {
                    status = destroy_status;
                }
            }
        }

        RETURN_IF_ROCSPARSE_ERROR(status);
        return rocsparse_status_success;
    }
}
