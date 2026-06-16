# rocm-23624 — `csritilu0` (sync_split) failure → `hipMallocAsync` pool memory corruption

**Status: CONFIRMED below rocSPARSE — `hipMallocAsync` stream-ordered pool / HIP runtime**
(ROCm 7.14.0 / UB26, gfx950 / MI350). Reproduced with the **public** `rocsparse_dcsr2csc` API,
no `csritilu0` involved. The discriminator settles it:

| Allocator (repro `USE_POOL`) | Result |
|------------------------------|--------|
| `hipMallocAsync` (`USE_POOL=1`) | **CORRUPTS** at n=187 once the pool is recycled |
| `hipMalloc`      (`USE_POOL=0`) | **clean, every size, every rep** |

Same `csr2csc` code, only the allocator differs ⇒ rocSPARSE is correct; the pool/runtime is the
defect.

---

## 1. Symptom

```
[ RUN      ] quick/csritilu0.precond/f32_r_187_0b_3_rand   (itilu0_alg = sync_split)
ASSERT_NEAR(0, -0.12976758182) failed ... exceeds permissive range
[  FAILED  ]
```

Key observations:
- Fails **only when run after another test** in the same process (passes in isolation).
- **Size-dependent**: `f32_r_50` passes, `f32_r_187` fails.
- `itilu0_alg = sync_split` specific.
- The computed ILU has entries that are **exactly 0** where the reference (`rocsparse_csrilu0`) is non-zero.

---

## 2. Investigation timeline (what was ruled in / out)

All evidence was gathered with temporary instrumentation (since removed).

| Step | Finding |
|------|---------|
| Test zeroes `ilu0` before compute (`testing_csritilu0.cpp:462`) | An exact-`0` output = an entry **never written** → corrupted permutation, not sweep math. |
| `perm` validation after preprocess | M=187: `perm` is **not a bijection** (`DUP value 0 … (U)`, `lnnz=265 unnz=247 m=187`). M=50: valid. |
| `sync_split` vs `sync_split_fusion` | Both share the same `csxsldu` preprocess; differ only in compute. The corruption is in the **U partition of `perm`**, produced by the `csxsldu` U-transpose via `csr2csc`. |
| `csxsldu` U values around `csr2csc` | **pre**: `unnz=247 zeros=0 min=1 max=687` (valid). **post**: `zeros=247` (entire output zeroed). |
| Inside `csr2csc_core` (numeric) | After radix sort the permutation `map` is **valid** (`oob=0, [0,246]`) and `csr_val` is **valid** (`[1,687]`). |
| Checkpoints between sort and gather | After `coo2csr_core`: `csr_val_zeros=247` — the live, const input `csr_val` (`tmp_val`) was **wiped to zero**. `coo2csr` only writes `csc_col_ptr`. |
| Pointer-range dump | `csr_val` (pool) `0x71a2_70a0_1300…`, `csc_col_ptr` (user) `0x71ab_1272_de94…` — **disjoint, ~36 GB apart**. |

### Ruled OUT (not the cause)
- User working buffer (already zeroed by a preprocess `memset` — confirmed irrelevant).
- `csr2csc`'s own scratch `buffer_conversion` (explicitly zeroing it changed nothing).
- The radix sort (`endbit = clz(n)` is correct; produced a valid permutation).
- The `layout_x_next` double buffer in the sweep (a correct, defensive pre-copy was tried — no effect).
- Buffer sizing/layout in `csxsldu` / `csr2csc` (all allocations correct and disjoint).

### Ruled IN
A correctly-bounded kernel write to one VA range (`csc_col_ptr`, 188 ints at `0x71ab…`)
**zeroes a 247-element buffer in an unrelated VA range** (`csr_val` at `0x71a2…`). This cannot
result from rocSPARSE source logic → the defect is in the layer below.

---

## 3. Mechanism (chain of effects)

```
hipMallocAsync pool recycled (after a prior test)
        │
        ▼
csxsldu U-transpose calls csr2csc (action=numeric, integer "identity" values)
        │
        ▼
coo2csr_core (inside csr2csc) runs   ──►  live, DISJOINT csr_val/tmp_val gets zeroed
        │                                  (writes only csc_col_ptr, 36 GB away)
        ▼
csc_val (= permuted csr_val) becomes all-zero
        │
        ▼
U partition of `perm` is no longer a bijection (duplicate / missing indices)
        │
        ▼
set_permuted_array leaves ilu0 output slots at their pre-memset 0
        │
        ▼
csritilu0 returns a wrong factorization → unit-check fails
```

---

## 4. Standalone reproducer

`projects/rocsparse/csr2csc_pool_repro.cpp` — **public API only** (`rocsparse_dcsr2csc`).

It (1) dirties the async pool, (2) allocates `csr2csc` inputs + temp_buffer from the pool and
outputs in a separate regular allocation, (3) calls `rocsparse_dcsr2csc`, (4) checks whether the
`const` input `csr_val` survived.

Build:
```bash
hipcc -O3 csr2csc_pool_repro.cpp -o csr2csc_pool_repro \
    -I build/release/include -L build/release/library -lrocsparse
LD_LIBRARY_PATH=build/release/library ./csr2csc_pool_repro
```

Observed (ROCm 7.14.0 / UB26, gfx950 / MI350):
```
n=187 nnz=371 rep=6 : csr_val changed=371 zeros=371   <<< CORRUPTED
REPRODUCED: rocsparse_dcsr2csc modified/zeroed its const csr_val input (disjoint-buffer corruption).
```
Note it triggers at **n=187** (the original failing size) once the pool reaches the right
recycled state.

### `USE_POOL` toggle (decisive discriminator)
- `-DUSE_POOL=1` (default): inputs/temp from `hipMallocAsync` + dirty-pool step → **reproduces**.
- `-DUSE_POOL=0`: everything via plain `hipMalloc`, no pool → if it **stops** reproducing, the
  fault is the async pool / runtime (below rocSPARSE); if it **still** reproduces, `csr2csc`
  writes out of bounds (a rocSPARSE bug).

```bash
hipcc -O3 -DUSE_POOL=0 csr2csc_pool_repro.cpp -o csr2csc_pool_repro_nopool \
    -I build/release/include -L build/release/library -lrocsparse
```

---

## 5. Discriminator result (DONE) and next actions

`-DUSE_POOL=0` (plain `hipMalloc`, no pool, no dirty step) → **no corruption across all sizes /
reps**. `-DUSE_POOL=1` (async pool) → corrupts at n=187. This **confirms the fault is the
`hipMallocAsync` pool / HIP runtime**, not a `csr2csc` out-of-bounds write.

Remaining:
1. Run the reproducer on a **non-UB26 / stock ROCm** toolchain. If clean there, it nails the
   regression to the UB26 runtime.
2. **File against the HIP runtime / `hipMallocAsync` stream-ordered pool** (gfx950) with: the
   reproducer (`USE_POOL=1` corrupts / `USE_POOL=0` clean), the pointer-range dump (a write to
   `0x71ab…` zeroing a disjoint allocation at `0x71a2…`), and the order/size-dependence.
3. Optional rocSPARSE mitigation while the runtime is fixed: in the `csxsldu` U/L transpose,
   allocate the `csr2csc` temporaries (`buffer_conversion`, `tmp_ind`, `tmp_val`) with plain
   `hipMalloc`/`hipFree` instead of `rocsparse_hipMallocAsync` — confirmed to avoid the
   corruption. (Workaround only; the real fix belongs in the runtime.)

---

## 6. Reproduction environment

- ROCm 7.14.0 (`/opt/rocm-7.14.0`), UB26 toolchain.
- GPU: AMD Instinct MI350P (gfx950), wavefront 64.
- rocSPARSE 4.6.0 (branch `rocm-23624`).
- Failing test: `quick/csritilu0.precond/f32_r_187_0b_3_rand`, `itilu0_alg = sync_split`.

---

## 7. Note on the rocSPARSE branch

The in-source experiments tried during the investigation (a full-buffer `memset` in
`csritilu0_preprocess`, a `layout_x_next` pre-copy in `csritilu0x_sync`, and all
`[TEMP DIAGNOSTIC]` instrumentation) were **reverted** — the rocSPARSE source is back to its
`develop` state, because the root cause is external. If a defensive change is still wanted in
rocSPARSE (e.g. the `layout_x_next` pre-copy mirroring `sync_split_fusion`), it should be a
separate, deliberate PR rather than a fix for this issue.
