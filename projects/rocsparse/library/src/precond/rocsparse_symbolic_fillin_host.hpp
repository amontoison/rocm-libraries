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

// ============================================================================
//  rocsparse::symbolic_fillin_host -- CPU companion to rocsparse_symbolic_fillin.hpp
//
//  Pure C++/MIT (no HIP, no external deps).  Computes the COMPLETE factor fill
//  pattern in the given natural order (no reordering, no pivoting) so that
//  IC(0)/ILDLT(0)/ILU(0) on the enlarged pattern become exact factorizations.
//
//  Two algorithms -- "smart where it matters":
//
//    symmetric == true  (IC(0)/ILDLT(0)):  elimination tree + ereach row
//        subtrees.  This is the classic SuiteSparse-style symbolic Cholesky
//        (cf. cs_etree / cs_ereach), O(nnz(L)) and single pass -- re-implemented
//        from scratch as pure AMD code.  Input must be a FULL symmetric pattern.
//
//    symmetric == false (ILU(0)):  single-pass up-looking elimination on the
//        combined L+U pattern (correct; cost ~ "elimination game").  The
//        asymptotically optimal upgrade is DFS reachability per row
//        (Gilbert-Peierls) -- left as a TODO if this ever becomes a bottleneck.
//
//  This is also the validation oracle for the GPU kernel and a viable PRIMARY
//  path: since the symbolic is amortized and GPU-hostile, the CPU version is
//  often as fast or faster (see project notes).
// ============================================================================

#include <algorithm>
#include <set>
#include <vector>

namespace rocsparse
{
    // ------------------------------------------------------------------------
    //  Elimination tree of a symmetric pattern (0-based, full pattern stored).
    //  parent[k] = -1 for roots.  cs_etree-style with path compression.
    // ------------------------------------------------------------------------
    inline void symbolic_fillin_etree(int                     n,
                                       const std::vector<int>& Ap,
                                       const std::vector<int>& Ai,
                                       std::vector<int>&       parent)
    {
        parent.assign(n, -1);
        std::vector<int> ancestor(n, -1);
        for(int k = 0; k < n; ++k)
        {
            parent[k]   = -1;
            ancestor[k] = -1;
            for(int p = Ap[k]; p < Ap[k + 1]; ++p)
            {
                int i = Ai[p]; // entry (k,i); for symmetric A this is column k, row i
                // walk i up its current ancestor chain, hooking everything to k
                for(; i != -1 && i < k;)
                {
                    int inext    = ancestor[i];
                    ancestor[i]  = k;
                    if(inext == -1)
                        parent[i] = k;
                    i = inext;
                }
            }
        }
    }

    // ------------------------------------------------------------------------
    //  Symmetric path: row i lower pattern via ereach, then symmetrize -> F.
    // ------------------------------------------------------------------------
    inline void symbolic_fillin_symmetric(int                         n,
                                          const std::vector<int>&     Ap,
                                          const std::vector<int>&     Ai,
                                          std::vector<std::set<int>>& F)
    {
        std::vector<int> parent;
        symbolic_fillin_etree(n, Ap, Ai, parent);

        std::vector<int> flag(n, -1), stack(n);
        std::vector<std::vector<int>> Lrow(n); // lower (incl diagonal)
        for(int i = 0; i < n; ++i)
        {
            flag[i] = i;
            Lrow[i].push_back(i); // diagonal
            for(int p = Ap[i]; p < Ap[i + 1]; ++p)
            {
                int j = Ai[p];
                if(j >= i)
                    continue; // only j < i
                int len = 0;
                for(int s = j; flag[s] != i; s = parent[s])
                {
                    stack[len++] = s;
                    flag[s]      = i;
                }
                for(int t = 0; t < len; ++t)
                    Lrow[i].push_back(stack[t]);
            }
        }

        // F = pattern(L) U pattern(L^T)
        F.assign(n, {});
        for(int i = 0; i < n; ++i)
            for(int c : Lrow[i])
            {
                F[i].insert(c);
                if(c != i)
                    F[c].insert(i);
            }
    }

    // ------------------------------------------------------------------------
    //  General path: single-pass up-looking elimination on combined L+U.
    //  Row i: start from A(i,:); repeatedly take the smallest unprocessed
    //  k < i in the set and merge { j in F[k] : j > k } (F[k] already final).
    // ------------------------------------------------------------------------
    inline void symbolic_fillin_general(int                         n,
                                        const std::vector<int>&     Ap,
                                        const std::vector<int>&     Ai,
                                        std::vector<std::set<int>>& F)
    {
        F.assign(n, {});
        for(int i = 0; i < n; ++i)
        {
            std::set<int>& S = F[i];
            for(int p = Ap[i]; p < Ap[i + 1]; ++p)
                S.insert(Ai[p]);
            S.insert(i);

            std::set<int> done;
            while(true)
            {
                int k = -1;
                for(int c : S)
                {
                    if(c < i && !done.count(c))
                    {
                        k = c;
                        break;
                    }
                }
                if(k == -1)
                    break;
                done.insert(k);
                for(int j : F[k])
                    if(j > k)
                        S.insert(j);
            }
        }
    }

    // ------------------------------------------------------------------------
    //  Main host entry.  Templated on index types I (row_ptr) / J (col_ind).
    //  Input/output CSR are `base`-indexed.  Outputs are resized as needed.
    //  Symmetric mode requires a FULL symmetric input pattern.
    // ------------------------------------------------------------------------
    template <typename I, typename J>
    void symbolic_fillin_host(bool            symmetric,
                              J               n,
                              const I*        csr_row_ptr,
                              const J*        csr_col_ind,
                              I               base,
                              std::vector<I>& F_row_ptr,
                              std::vector<J>& F_col_ind)
    {
        // to 0-based int working copy
        std::vector<int> Ap(n + 1), Ai;
        for(J i = 0; i <= n; ++i)
            Ap[i] = (int)(csr_row_ptr[i] - base);
        Ai.resize(Ap[n]);
        for(int p = 0; p < Ap[n]; ++p)
            Ai[p] = (int)(csr_col_ind[p] - base);

        std::vector<std::set<int>> F;
        if(symmetric)
            symbolic_fillin_symmetric((int)n, Ap, Ai, F);
        else
            symbolic_fillin_general((int)n, Ap, Ai, F);

        // pack to CSR (sorted by std::set), apply base
        F_row_ptr.assign(n + 1, base);
        for(int i = 0; i < (int)n; ++i)
            F_row_ptr[i + 1] = (I)(F_row_ptr[i] + (I)F[i].size());
        F_col_ind.resize((size_t)(F_row_ptr[n] - base));
        size_t pos = 0;
        for(int i = 0; i < (int)n; ++i)
            for(int c : F[i])
                F_col_ind[pos++] = (J)(c + base);
    }
}
