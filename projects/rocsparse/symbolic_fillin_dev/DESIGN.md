# Symbolic fill-in for IC(0) / ILDLᵀ(0) / ILU(0) — design & algorithm

> Companion to `README.md` (which is the build/run guide). This document is the
> *why*: the math, the algorithms, the design choices, performance expectations,
> and the integration checklist.

## 1. Problem and the core trick

We have a sparse matrix **A** that has **already been permuted** in an earlier
phase (fill-reducing ordering applied upstream). We want, in that fixed natural
order and **without pivoting**, the sparsity pattern **F** of the *complete*
triangular factor.

Why: if we copy A into a buffer with pattern F (the original values plus
**explicit zeros** at the fill positions) and then run the *incomplete*
factorization on that enlarged pattern, the result is the **exact complete**
factorization — because nothing is ever dropped (every fill position already
exists):

```
IC(0)    on the complete pattern  ==  exact Cholesky      (A = L Lᵀ)
ILDLᵀ(0) on the complete pattern  ==  exact LDLᵀ          (A = L D Lᵀ, D diagonal)
ILU(0)   on the complete pattern  ==  exact LU            (A = L U, no pivoting)
```

So the whole task reduces to **computing F** (a symbolic factorization) and a
**numeric scatter** (A → buffer with explicit zeros).

### Stability caveat (orthogonal to the symbolic work)
Running a complete factorization with **no pivoting** in natural order is:
- **clean for SPD** (`IC(0)`),
- **fragile for symmetric indefinite** (`ILDLᵀ` may need 2×2 pivots),
- **risky for general** (`ILU` can hit zero/small pivots).
The symbolic computation is always correct; the *numeric* robustness is the
caller's responsibility (use SPD for IC, ensure diagonal dominance / accept the
limits for ILU).

## 2. The math: fill-path theorem

F is the smallest superset of `pattern(A)` closed under the elimination rule:

```
(i,k) ∈ F  and  (k,j) ∈ F  with  k < i  and  k < j   ⇒   (i,j) ∈ F
```

Equivalently (Rose–Tarjan *fill-path theorem*): `(i,j)` fills in **iff** there is
a path `i → … → j` in the graph of A whose intermediate vertices all have index
`< min(i,j)`. The rule above is exactly the rank-1 update at pivot `k` (it
touches rows `i>k` and columns `j>k`).

## 3. One rule, both symmetric and unsymmetric

The closure rule is identical for Cholesky and LU; only the **input pattern**
differs:

| Factorization | Input pattern `F₀` | Output `F` |
|---|---|---|
| IC(0) / ILDLᵀ(0) | full **symmetric** pattern (both triangles) | symmetric `L`+`Lᵀ` |
| ILU(0) | `pattern(A)` as-is | combined `L`+`U` |

Because the rule `k<i ∧ k<j` *is* the rank-1 LU update, **the same kernel
computes both**. For the symmetric modes the input must be a full symmetric
pattern; if only half is stored (or A is not structurally symmetric), build
`pattern(A) ∪ pattern(Aᵀ)` first — that is the only place a `geam`/transpose is
needed. **No `geam` is used for ILU.**

## 4. GPU algorithm — Jacobi fixed point (`rocsparse_symbolic_fillin.hpp`)

```
F ← F₀
repeat
    for each row i (in parallel):
        S_i = row_F(i) ∪ ⋃_{k ∈ row_F(i), k<i} { j ∈ row_F(k) : j>k }
    F ← S
until nnz(F) stops changing
```

- Every iteration is **embarrassingly parallel across rows** (reads old `F`,
  writes new `F`; no intra-iteration dependency, no elimination tree).
- Each row is exactly a **per-row set union** → implemented with the `csrgemm`
  symbolic hash-set primitive (see §6).
- Two passes per iteration: **count** (per-row nnz) → `exclusive_scan` →
  **fill** (sorted column indices). Same structure as `csrgemm` symbolic.
- Converges in ≈ longest-fill-path (etree height) iterations — **2–8** in the
  validation set; worst case `O(n)`.

This is a *version 0*: correct and fully on-device, optimized for simplicity not
speed (host sync per iteration for the convergence test, re-allocation per
iteration, `O(HASHSIZE²)` sorted emit, single hash-size bucket).

## 5. CPU algorithm (`rocsparse_symbolic_fillin_host.hpp`)

Pure C++/MIT, no dependencies. **Smart where it matters:**

- **Symmetric (IC/ILDLᵀ):** elimination tree + `ereach` row subtrees — the
  classic SuiteSparse-style symbolic Cholesky (`cs_etree` / `cs_ereach`),
  **`O(nnz(L))`, single pass**, re-implemented from scratch. Validated to match
  CXSparse `cs_chol` byte-for-byte (see §9).
- **General (ILU):** single-pass up-looking elimination (correct;
  cost ≈ "elimination game"). Optimal upgrade = DFS reachability per row
  (Gilbert–Peierls) — *not yet implemented*, fine to add only if it becomes a
  bottleneck.

We deliberately do **not** reimplement the fill-reducing **ordering**
(AMD / nested dissection) — that is the genuinely hard part and is out of scope
(applied upstream).

## 6. Reuse map — what comes from `csrgemm`

In `library/src/extra/csrgemm_symbolic_device.h`:

| Reused | Symbol | Role |
|---|---|---|
| LDS hash-set insert | `insert_key` (`:523`) | the per-row "union of column sets" primitive |
| max row nnz | `csrgemm_symbolic_max_row_nnz_part1/2` | pick the hash size per iteration |
| (future) load balancing | `csrgemm_symbolic_group_reduce_part1/2/3` | bin rows by nnz |
| (future) long rows | `..._fill_block_per_row_multipass_device` (`:41`) | bitmap-by-chunks for rows > largest bucket |

Key point: **the "per-row LDS hash set + bin-by-nnz" machinery is a generic
symbolic primitive.** Any operator whose output pattern is "unions of input
patterns" builds on it. We are *not* calling `csrgemm` as an operation —
symbolic `A·A` would ignore the ordering constraint and massively over-fill.

## 7. Numeric scatter

After F is known, build `val_F` (size `nnz(F)`) with `val_F[(i,j)] = A(i,j)` if
present else `0`. One wavefront per row, binary-search each F column in the A
row (`symbolic_fillin_scatter_kernel`). Then hand `(F, val_F)` to
`csric0` / `csrildlt0` / `csrilu0`.

## 8. Performance expectations — read this before optimizing

- **The GPU symbolic is NOT a performance win over a good CPU symbolic.** It is
  graph / integer / irregular work (arithmetic intensity ≈ 0), and the v0 does
  `iters×` the work of the optimal CPU algorithm.
- **The pattern is amortized** (computed once, reused over N numeric
  refactorizations) → the symbolic is `~1/N` of total runtime either way; the
  absolute gain from moving it to the GPU is small.
- **The real reason to have a GPU version is architectural:** an all-device
  chain with **no host↔device round-trip** (no PCIe). The win is data movement,
  not symbolic FLOPs.
- Consequence: the **CPU routine is likely the primary path**; the GPU routine
  is for the all-on-device use case. Both exist in pure AMD/MIT code and
  cross-validate each other.

## 9. Validation (all reproducible — see README)

| Check | Status |
|---|---|
| Fixed-point fill == exact reference (sym + general) | ✅ runs (`test_fillin_cpu`) |
| Host etree+ereach == exact reference | ✅ runs |
| **Host == SuiteSparse `cs_chol`** (incl. n=200, nnz(L)=12207) | ✅ runs (`validate_suitesparse`, dev-only) |
| ILU(0)/IC(0)/ILDLᵀ(0) exact on complete pattern | ✅ `‖·‖ ≈ 1e-15` |
| Standalone GPU kernel | ✅ compiles (`hipcc`) |
| End-to-end vs real `rocsparse_dcsrilu0`/`dcsric0` | ✅ compiles + **links** `librocsparse.so` (run needs a GPU) |
| In-tree GPU header | ✅ `hipcc -fsyntax-only` against the rocSPARSE tree |

## 10. Licensing

rocSPARSE is **MIT**; SuiteSparse is **(L)GPL** → it **cannot** be a dependency
of the shipped library. Both CPU and GPU paths are therefore **own, pure-AMD
code**. SuiteSparse/CXSparse is used **only as a dev-only validation oracle**
(downloaded locally, never linked into rocSPARSE).

## 11. Future GPU improvements (by impact)

The v0 GPU kernel is correct but intentionally unoptimized. In priority order:

1. **Elimination tree + level scheduling (the big algorithmic win).** Replace the
   Jacobi fixed point (which redoes 2–8 full scans) with the *optimal* symbolic:
   structure of column `j` = union of its **etree children** structures + `A(:,j)`,
   `O(nnz(L))` in a single pass. Parallelism comes from **level sets** (columns at
   the same etree level are independent → one kernel per level, finalized). For
   the general/LU case the symmetric etree doesn't apply — use topological levels
   of the directed fill DAG instead. The etree can be built on-device (atomic
   union-find) or supplied once by the host (tiny, `O(nnz)`, amortized).
2. **Device-side convergence + scan.** Drop the per-iteration host copy of
   `row_ptr[n]` and `hipStreamSynchronize`; use rocprim `exclusive_scan` and a
   device `changed` flag.
3. **Bin-by-nnz + multipass.** Reuse `csrgemm_symbolic_group_reduce_part1/2/3` to
   bin rows and launch a right-sized kernel; add
   `csrgemm_symbolic_fill_block_per_row_multipass_device` for rows beyond LDS
   capacity (>8192) — currently a **correctness gap**, not just perf.
4. **Sorted emit in `O(nnz)`** instead of the `O(HASHSIZE²)` "count-smaller"
   loop — hash compaction + sort, or a dense bitmap accumulator (sorted for free).
5. **Preallocate** a generous (or geometrically grown) buffer instead of
   `hipMalloc`/`hipFree` per iteration.
6. **Supernodes / amalgamation + dense top-of-tree** (advanced). Group columns
   with identical structure into supernodes (fewer tasks, shorter critical path,
   regular dense work); switch to many-threads-per-column near the root, where
   the dense separator otherwise dominates.

Caveat: worth doing only if the symbolic becomes a *measured* bottleneck. Since
it is amortized and the CPU path is likely primary, prioritize **running on GPU +
integration** first; if optimizing, start with #2/#3/#4 (easy, reuse csrgemm),
then #1 (etree) for the principled algorithm.

## 12. Remaining work (integration checklist)

- [ ] **Run on a real GPU** (`test_end_to_end`, `test_fillin_hip`) — the missing runtime proof.
- [ ] C API + `*_buffer_size` + CMake + template instantiations.
- [ ] gtest in `clients/` mirroring `test_fillin_cpu`.
- [ ] Export `rocsparse_dcsrildlt0`; add it to the end-to-end test.
- [ ] GPU: wire `symbolic_fillin_symmetrize` (geam), multipass for rows > 8192,
      bin load-balancing, device-side convergence/scan, Gauss-Seidel sweep.
- [ ] CPU: DFS (Gilbert–Peierls) for the optimal general path.

## File map

| File | Role |
|---|---|
| `library/src/precond/rocsparse_symbolic_fillin.hpp` | GPU: kernel + Jacobi driver + numeric scatter |
| `library/src/precond/rocsparse_symbolic_fillin_host.hpp` | CPU: etree+ereach (sym) / up-looking (general) |
| `symbolic_fillin_dev/test_fillin_cpu.cpp` | CPU proof (fill + ILU/IC/ILDLᵀ exactness) |
| `symbolic_fillin_dev/test_fillin_hip.hip` | standalone GPU kernel + self-check |
| `symbolic_fillin_dev/test_end_to_end.hip` | feeds pattern into real rocSPARSE routines |
| `symbolic_fillin_dev/validate_suitesparse.cpp` | dev-only cross-check vs CXSparse |
