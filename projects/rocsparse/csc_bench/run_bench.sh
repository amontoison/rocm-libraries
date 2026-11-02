#!/bin/bash
# Sweep of rocsparse_sptrsv on CSC without transpose: transposed vs column solve.
# Writes CSV to stdout.
#   ./run_bench.sh                       generated matrices
#   ./run_bench.sh matrices/*.mtx        plus Matrix Market files (see get_matrices.sh)
# Extra options for every run can be passed through BENCH_OPTS, e.g. BENCH_OPTS=--chrono under WSL.
set -e
B="$(dirname "$0")/bench_sptrsv_csc"
O="${BENCH_OPTS:-}"
first=1
run() { if [ $first = 1 ]; then "$B" $O "$@"; first=0; else "$B" $O --no-header "$@"; fi; }

# Random lower triangles, three densities, two sizes, f64 and f32.
for m in 100000 1000000; do for k in 5 15 30; do
  for t in f64 f32; do run --matrix random --m $m --k $k --type $t; done
done; done
# Complex, upper triangle, unit diagonal.
run --matrix random --m 1000000 --k 15 --type c64
run --matrix random --m 1000000 --k 15 --type f64 --fill U
run --matrix random --m 1000000 --k 15 --type f64 --diag U
# Latency-bound structures.
run --matrix banded --m 1000000 --k 10 --type f64
run --matrix chain  --m 200000          --type f64
# Independent chains / blocks numbered one after the other (natural order stress).
for nb in 1 16 64 256 1024 4096 16384; do run --matrix chains --m 1000000 --nb $nb --type f64; done
for nb in 16 256 4096; do run --matrix blockrand --m 1000000 --nb $nb --k 5 --type f64; done
# Batches: shared matrix and one matrix per batch.
run --matrix random --m 100000 --k 15 --type f64 --batch 16
run --matrix random --m 100000 --k 15 --type f64 --batch 16 --batch-matrix
# Matrix Market files given on the command line, lower and upper triangles.
for f in "$@"; do
  run --matrix "$f" --type f64
  run --matrix "$f" --type f64 --fill U
done
