# Symbolic fill-in — working prototype + validation

Computes the **complete** factor fill pattern (natural order, no pivoting) so that
`IC(0)` / `ILDLT(0)` / `ILU(0)` run on the enlarged pattern (A copied in with
explicit zeros) become the **exact** complete factorization.

Algorithm: **Jacobi fixed point** on the combined `L+U` pattern `F`:
per row `i`, `S_i = row(i) ∪ ⋃_{k∈row(i),k<i} {j∈row(k):j>k}`, iterate until
`nnz(F)` stops changing. The per-row union reuses the `csrgemm` symbolic
hash-set primitive (`insert_key`). One kernel covers symmetric and general; only
`F0` prep differs (general = `pattern(A)`; symmetric = full symmetric pattern,
optionally `pattern(A)∪pattern(Aᵀ)` via `csr2csc`+`csrgeam`).

## Files

| File | What |
|---|---|
| `test_fillin_cpu.cpp` | CPU proof: fixed-point fill == exact reference fill; numeric scatter; `‖LU−A‖` check that ILU(0)-on-full-pattern == exact LU. **Runs without a GPU.** |
| `test_fillin_hip.hip` | Standalone HIP kernel (block-per-row, dynamic-LDS hash) + self-validation vs CPU reference. **Needs a GPU to run; compiles with `hipcc`.** |
| `test_end_to_end.hip` | Feeds the complete pattern into the **real** `rocsparse_dcsrilu0` / `rocsparse_dcsric0` and checks the factorization is exact. **Compiles + links against `librocsparse.so`; needs a GPU to run.** |
| `../library/src/precond/rocsparse_symbolic_fillin.hpp` | In-tree **GPU** header (kernel + driver + numeric scatter + optional geam symmetrize stub). Passes `hipcc -fsyntax-only` against the rocSPARSE tree; **not yet wired into CMake/C-API.** |
| `../library/src/precond/rocsparse_symbolic_fillin_host.hpp` | In-tree **CPU** routine, pure C++/MIT (no deps). `symmetric`: elimination-tree + `ereach` (SuiteSparse-style symbolic Cholesky, `O(nnz(L))`); `general`: single-pass up-looking. **Validated** against the reference for all cases. |

| `validate_suitesparse.cpp` | Cross-checks the host **symmetric** fill vs SuiteSparse CXSparse `cs_schol(natural)+cs_chol`. **Dev-only** (CXSparse is LGPL — used as an oracle, never linked into rocSPARSE). Matches exactly on all cases (incl. n=200). |

> `ildlt0`: covered by the CPU proof only — there is no public `rocsparse_dcsrildlt0` C API yet (WIP on this branch). Add it to `test_end_to_end` once exported.

### Optional: SuiteSparse cross-validation (dev-only, no root)

```sh
cd /tmp && mkdir -p ss && cd ss
apt-get download libcxsparse4 libsuitesparse-dev
dpkg -x libcxsparse4_*.deb root && dpkg -x libsuitesparse-dev_*.deb root
cd -    # back to symbolic_fillin_dev
g++ -O2 -std=c++17 validate_suitesparse.cpp -o validate_suitesparse \
  -I/tmp/ss/root/usr/include/suitesparse \
  -L/tmp/ss/root/usr/lib/x86_64-linux-gnu -lcxsparse \
  -Wl,-rpath,/tmp/ss/root/usr/lib/x86_64-linux-gnu && ./validate_suitesparse
```
Only the symmetric path is comparable (CXSparse `cs_lu` pivots; our fill is no-pivot natural order).

## Build & run

```sh
# CPU validation (no GPU required)
g++ -O2 -std=c++17 test_fillin_cpu.cpp -o test_fillin_cpu && ./test_fillin_cpu

# GPU standalone (requires a GPU)
hipcc -O2 -std=c++17 test_fillin_hip.hip -o test_fillin_hip && ./test_fillin_hip

# End-to-end against real rocSPARSE (requires a GPU to run)
hipcc -O2 -std=c++17 test_end_to_end.hip -o test_end_to_end \
  -I ../build/release/include -L ../build/release/library -lrocsparse \
  -Wl,-rpath,../build/release/library && ./test_end_to_end
```

Verified CPU output: all symmetric & general cases `pattern=OK`, `‖LU−A‖ ≈ 1e-15`,
fixed point converging in 2–8 iterations.

## Remaining for in-tree production

- Wire C API + `*_buffer_size` + CMake + template instantiations.
- Handle rows longer than the largest hash bucket (reuse `csrgemm` multipass).
- Optional: implement `symbolic_fillin_symmetrize` (currently a stub) for
  half-stored / non-symmetric input; bin-by-nnz load balancing; Gauss-Seidel
  sweep (converges in ~1 pass).
- Add a gtest mirroring `test_fillin_cpu` into `clients/`.
