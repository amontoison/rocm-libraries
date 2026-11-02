# CSC column solve benchmark (not for merge)

Compares, inside one `rocsparse_sptrsv` build, the transposed CSC solve (`default_*` columns,
`ROCSPARSE_SPTRSV_CSC_COLUMNS=0`) with the column solve (`push_*` columns). Requires rocSPARSE
built from this branch with `BUILD_WITH_SPTRSV_CSC=ON`.

    make ROCM_PATH=$ROCM_PATH ROCSPARSE_DIR=<build dir> GPU_ARCH=gfx942
    ./run_bench.sh > synth.csv && ./summarize.py synth.csv          # synthetic cases

SuiteSparse subset (square, n >= 100k, nnz <= 60M, at most 3 per group, 80 matrices):

    curl -sSLO https://sparse.tamu.edu/files/ssstats.csv
    ./select_suitesparse.py ssstats.csv > list.txt                   # N_MIN, NNZ_MAX, COUNT, PER_GROUP
    LIST=list.txt DIR=$WORK/ss ./get_suitesparse.sh                  # JOBS=4 parallel downloads
    DIR=$WORK/ss ./run_suitesparse.sh > ss.csv 2> ss.err             # FILLS="L U", TIMEOUT, BENCH_OPTS
    ./summarize.py ss.csv
