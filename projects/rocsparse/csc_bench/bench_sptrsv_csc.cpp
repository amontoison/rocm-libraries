// Benchmark of rocsparse_sptrsv on a CSC matrix without transpose: the transposed solve
// ("default" columns, ROCSPARSE_SPTRSV_CSC_COLUMNS=0) against the column solve ("push" columns).
// Requires a rocSPARSE built with BUILD_WITH_SPTRSV_CSC=ON and the benchmark hook.
//
// For each algorithm it reports the analysis time (median, fresh descriptor each time), the
// solve time (median), both buffer sizes, the relative residual ||op(A) y - alpha x|| / ||alpha x||
// computed on the host, and the relative difference with the solution of the default algorithm.

#include <hip/hip_runtime.h>
#include <rocsparse/rocsparse.h>

#if !defined(ROCSPARSE_WITH_SPTRSV_CSC)
#error "rocSPARSE was built without BUILD_WITH_SPTRSV_CSC"
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#define CHECK_HIP(x)                                                                    \
    do                                                                                  \
    {                                                                                   \
        hipError_t e_ = (x);                                                            \
        if(e_ != hipSuccess)                                                            \
        {                                                                               \
            fprintf(stderr, "HIP error %s at %s:%d\n", hipGetErrorString(e_), __FILE__, \
                    __LINE__);                                                          \
            exit(1);                                                                    \
        }                                                                               \
    } while(0)
#define CHECK_ROCSPARSE(x)                                                             \
    do                                                                                 \
    {                                                                                  \
        rocsparse_status s_ = (x);                                                     \
        if(s_ != rocsparse_status_success)                                             \
        {                                                                              \
            fprintf(stderr, "rocSPARSE error %d at %s:%d\n", (int)s_, __FILE__, __LINE__); \
            exit(1);                                                                   \
        }                                                                              \
    } while(0)

// ---------------------------------------------------------------------------
// Options
// ---------------------------------------------------------------------------
struct options
{
    std::string matrix = "random"; // random | banded | chain | chains | blockrand | path to a .mtx file
    int64_t     m      = 100000;
    int         k      = 10; // off-diagonal entries per row (random, blockrand) or bandwidth (banded)
    int64_t     nb     = 64; // independent chains (chains) or diagonal blocks (blockrand)
    std::string type   = "f64"; // f32 | f64 | c32 | c64
    std::string format = "csc"; // csr | coo | csc
    std::string op     = "auto"; // N | T | CT | auto (the transpose case of the format)
    char        fill   = 'L';
    char        diag   = 'N';
    int64_t     batch  = 1;
    bool        batch_matrix = false; // one matrix per batch instead of a shared matrix
    int         reps_analysis = 10;
    int         reps_solve    = 50;
    bool        chrono        = false; // std::chrono + sync instead of hipEvent (WSL)
    bool        header        = true;
    unsigned    seed          = 1;
};

static void usage()
{
    printf("bench_sptrsv_csc [options]\n"
           "  --matrix random|banded|chain|chains|blockrand|<file.mtx>  (default random)\n"
           "  --m <rows>            rows for generated matrices (default 100000)\n"
           "  --k <n>               off-diagonals per row (random, blockrand) or bandwidth (banded), default 10\n"
           "  --nb <n>              independent chains (chains) or blocks (blockrand), default 64\n"
           "  --type f32|f64|c32|c64 (default f64)\n"
           "  --format csr|coo|csc  (default csc; csr/coo only run the default path)\n"
           "  --op N|T|CT|auto      (default auto: T for csr/coo, N for csc)\n"
           "  --fill L|U            triangle of A that is used (default L)\n"
           "  --diag N|U            non-unit or unit diagonal (default N)\n"
           "  --batch <b>           batched solve with b right-hand sides (default 1)\n"
           "  --batch-matrix        one matrix per batch (default: one shared matrix)\n"
           "  --reps-analysis <n>   (default 10)   --reps-solve <n> (default 50)\n"
           "  --chrono              time with std::chrono + hipDeviceSynchronize (WSL)\n"
           "  --no-header           do not print the CSV header\n");
}

static options parse(int argc, char** argv)
{
    options o;
    for(int i = 1; i < argc; ++i)
    {
        std::string a    = argv[i];
        auto        next = [&]() -> std::string {
            if(i + 1 >= argc)
            {
                usage();
                exit(1);
            }
            return argv[++i];
        };
        if(a == "--matrix") o.matrix = next();
        else if(a == "--m") o.m = std::stoll(next());
        else if(a == "--k") o.k = std::stoi(next());
        else if(a == "--nb") o.nb = std::stoll(next());
        else if(a == "--type") o.type = next();
        else if(a == "--format") o.format = next();
        else if(a == "--op") o.op = next();
        else if(a == "--fill") o.fill = next()[0];
        else if(a == "--diag") o.diag = next()[0];
        else if(a == "--batch") o.batch = std::stoll(next());
        else if(a == "--batch-matrix") o.batch_matrix = true;
        else if(a == "--reps-analysis") o.reps_analysis = std::stoi(next());
        else if(a == "--reps-solve") o.reps_solve = std::stoi(next());
        else if(a == "--chrono") o.chrono = true;
        else if(a == "--no-header") o.header = false;
        else if(a == "--seed") o.seed = std::stoul(next());
        else
        {
            usage();
            exit(a == "--help" || a == "-h" ? 0 : 1);
        }
    }
    if(o.op == "auto")
        o.op = (o.format == "csc") ? "N" : "T";
    return o;
}

// ---------------------------------------------------------------------------
// Host triangular matrix in CSR (0-based, sorted columns, diagonal present)
// ---------------------------------------------------------------------------
struct host_csr
{
    int64_t                           m = 0;
    std::vector<int32_t>              ptr, col;
    std::vector<std::complex<double>> val;
};

struct entry
{
    int64_t              i, j;
    std::complex<double> v;
};

// Builds the CSR of the triangle from (i, j, v) triplets: duplicates are summed, and a missing
// or zero diagonal is replaced by the sum of the absolute values of the row plus one.
static host_csr from_entries(int64_t m, std::vector<entry>& e)
{
    std::sort(e.begin(), e.end(), [](const entry& a, const entry& b) {
        return a.i < b.i || (a.i == b.i && a.j < b.j);
    });
    std::vector<entry> u;
    u.reserve(e.size() + m);
    for(const entry& x : e)
    {
        if(!u.empty() && u.back().i == x.i && u.back().j == x.j)
            u.back().v += x.v;
        else
            u.push_back(x);
    }
    std::vector<entry>().swap(e);

    host_csr A;
    A.m = m;
    A.ptr.assign(m + 1, 0);
    A.col.reserve(u.size() + m);
    A.val.reserve(u.size() + m);
    size_t k = 0;
    for(int64_t i = 0; i < m; ++i)
    {
        const size_t begin = k;
        while(k < u.size() && u[k].i == i)
            ++k;
        double               rowsum = 0.0;
        std::complex<double> diag   = 0.0;
        for(size_t q = begin; q < k; ++q)
        {
            if(u[q].j == i)
                diag = u[q].v;
            else
                rowsum += std::abs(u[q].v);
        }
        if(std::abs(diag) == 0.0)
            diag = rowsum + 1.0;
        for(size_t q = begin; q < k; ++q)
            if(u[q].j < i)
            {
                A.col.push_back((int32_t)u[q].j);
                A.val.push_back(u[q].v);
            }
        A.col.push_back((int32_t)i);
        A.val.push_back(diag);
        for(size_t q = begin; q < k; ++q)
            if(u[q].j > i)
            {
                A.col.push_back((int32_t)u[q].j);
                A.val.push_back(u[q].v);
            }
        A.ptr[i + 1] = (int32_t)A.col.size();
    }
    return A;
}

static bool in_triangle(char fill, int64_t i, int64_t j)
{
    return fill == 'L' ? j <= i : j >= i;
}

static host_csr generate(const options& o, bool complex_values)
{
    std::mt19937_64                        rng(o.seed);
    std::uniform_real_distribution<double> u(-1.0, 1.0);
    auto rand_val = [&]() {
        return std::complex<double>(u(rng), complex_values ? u(rng) : 0.0);
    };
    std::vector<entry> e;
    const int64_t      m = o.m;
    if(o.matrix == "random")
    {
        e.reserve(m * o.k);
        for(int64_t i = 0; i < m; ++i)
        {
            const int64_t lo = (o.fill == 'L') ? 0 : i + 1;
            const int64_t hi = (o.fill == 'L') ? i - 1 : m - 1;
            if(hi < lo)
                continue;
            std::uniform_int_distribution<int64_t> c(lo, hi);
            for(int t = 0; t < o.k; ++t)
                e.push_back({i, c(rng), rand_val()});
        }
    }
    else if(o.matrix == "chains")
    {
        // nb independent chains numbered one after the other: the natural order walks one
        // chain at a time.
        const int64_t len = (m + o.nb - 1) / o.nb;
        for(int64_t i = 0; i < m; ++i)
        {
            const int64_t j = (o.fill == 'L') ? i - 1 : i + 1;
            if(j >= 0 && j < m && j / len == i / len)
                e.push_back({i, j, rand_val()});
        }
    }
    else if(o.matrix == "blockrand")
    {
        // nb independent diagonal blocks, k random entries per row inside its block.
        const int64_t len = (m + o.nb - 1) / o.nb;
        e.reserve(m * o.k);
        for(int64_t i = 0; i < m; ++i)
        {
            const int64_t b0 = (i / len) * len, b1 = std::min(m, b0 + len) - 1;
            const int64_t lo = (o.fill == 'L') ? b0 : i + 1;
            const int64_t hi = (o.fill == 'L') ? i - 1 : b1;
            if(hi < lo)
                continue;
            std::uniform_int_distribution<int64_t> c(lo, hi);
            for(int t = 0; t < o.k; ++t)
                e.push_back({i, c(rng), rand_val()});
        }
    }
    else if(o.matrix == "banded" || o.matrix == "chain")
    {
        const int bw = (o.matrix == "chain") ? 1 : o.k;
        e.reserve(m * bw);
        for(int64_t i = 0; i < m; ++i)
            for(int d = 1; d <= bw; ++d)
            {
                const int64_t j = (o.fill == 'L') ? i - d : i + d;
                if(j >= 0 && j < m)
                    e.push_back({i, j, rand_val()});
            }
    }
    return from_entries(m, e);
}

// Matrix Market: coordinate real/complex/integer/pattern, general/symmetric/hermitian/skew.
// Only the requested triangle is kept (symmetric files are expanded first).
static host_csr read_mtx(const options& o)
{
    FILE* f = fopen(o.matrix.c_str(), "r");
    if(!f)
    {
        fprintf(stderr, "cannot open %s\n", o.matrix.c_str());
        exit(1);
    }
    std::vector<char> line(1 << 16);
    if(!fgets(line.data(), (int)line.size(), f))
    {
        fprintf(stderr, "empty file %s\n", o.matrix.c_str());
        exit(1);
    }
    std::string header = line.data();
    std::transform(header.begin(), header.end(), header.begin(), ::tolower);
    const bool is_complex = header.find("complex") != std::string::npos;
    const bool is_pattern = header.find("pattern") != std::string::npos;
    const bool is_sym     = header.find("symmetric") != std::string::npos
                        || header.find("hermitian") != std::string::npos
                        || header.find("skew") != std::string::npos;
    while(fgets(line.data(), (int)line.size(), f) && line[0] == '%')
    {
    }
    long long rows = 0, cols = 0, nnz = 0;
    sscanf(line.data(), "%lld %lld %lld", &rows, &cols, &nnz);
    if(rows != cols)
    {
        fprintf(stderr, "matrix %s is not square\n", o.matrix.c_str());
        exit(1);
    }
    std::vector<entry> e;
    e.reserve(is_sym ? nnz : nnz / 2 + rows);
    for(long long t = 0; t < nnz && fgets(line.data(), (int)line.size(), f); ++t)
    {
        char*         p  = line.data();
        const int64_t i  = strtoll(p, &p, 10) - 1;
        const int64_t j  = strtoll(p, &p, 10) - 1;
        double        re = 1.0, im = 0.0;
        if(!is_pattern)
            re = strtod(p, &p);
        if(is_complex)
            im = strtod(p, &p);
        const std::complex<double> v(re, im);
        if(in_triangle(o.fill, i, j))
            e.push_back({i, j, v});
        else if(is_sym && in_triangle(o.fill, j, i))
            e.push_back({j, i, v});
    }
    fclose(f);
    return from_entries(rows, e);
}

// ---------------------------------------------------------------------------
// Typed helpers
// ---------------------------------------------------------------------------
template <typename T>
struct type_info;
template <>
struct type_info<float>
{
    static constexpr rocsparse_datatype dt = rocsparse_datatype_f32_r;
    static float                        from(std::complex<double> v) { return (float)v.real(); }
    static std::complex<double>         to(float v) { return v; }
};
template <>
struct type_info<double>
{
    static constexpr rocsparse_datatype dt = rocsparse_datatype_f64_r;
    static double                       from(std::complex<double> v) { return v.real(); }
    static std::complex<double>         to(double v) { return v; }
};
template <>
struct type_info<rocsparse_float_complex>
{
    static constexpr rocsparse_datatype dt = rocsparse_datatype_f32_c;
    static rocsparse_float_complex      from(std::complex<double> v)
    {
        return rocsparse_float_complex((float)v.real(), (float)v.imag());
    }
    static std::complex<double> to(rocsparse_float_complex v)
    {
        return {std::real(v), std::imag(v)};
    }
};
template <>
struct type_info<rocsparse_double_complex>
{
    static constexpr rocsparse_datatype dt = rocsparse_datatype_f64_c;
    static rocsparse_double_complex     from(std::complex<double> v)
    {
        return rocsparse_double_complex(v.real(), v.imag());
    }
    static std::complex<double> to(rocsparse_double_complex v)
    {
        return {std::real(v), std::imag(v)};
    }
};

struct timer
{
    bool       chrono;
    hipEvent_t a, b;
    std::chrono::high_resolution_clock::time_point t0;
    explicit timer(bool c)
        : chrono(c)
    {
        if(!chrono)
        {
            CHECK_HIP(hipEventCreate(&a));
            CHECK_HIP(hipEventCreate(&b));
        }
    }
    ~timer()
    {
        if(!chrono)
        {
            (void)hipEventDestroy(a);
            (void)hipEventDestroy(b);
        }
    }
    void start(hipStream_t s)
    {
        if(chrono)
        {
            CHECK_HIP(hipDeviceSynchronize());
            t0 = std::chrono::high_resolution_clock::now();
        }
        else
            CHECK_HIP(hipEventRecord(a, s));
    }
    double stop_ms(hipStream_t s)
    {
        if(chrono)
        {
            CHECK_HIP(hipDeviceSynchronize());
            return std::chrono::duration<double, std::milli>(
                       std::chrono::high_resolution_clock::now() - t0)
                .count();
        }
        CHECK_HIP(hipEventRecord(b, s));
        CHECK_HIP(hipEventSynchronize(b));
        float ms = 0;
        CHECK_HIP(hipEventElapsedTime(&ms, a, b));
        return ms;
    }
};

static double median(std::vector<double> v)
{
    std::sort(v.begin(), v.end());
    return v.empty() ? 0.0 : v[v.size() / 2];
}

struct result
{
    double              analysis_ms = 0, solve_ms = 0;
    size_t              analysis_bytes = 0, solve_bytes = 0;
    std::vector<std::complex<double>> y;
};

// ---------------------------------------------------------------------------
// One algorithm
// ---------------------------------------------------------------------------
template <typename T>
static result run(const options&        o,
                  rocsparse_handle      handle,
                  const host_csr&       A,
                  bool                  columns,
                  rocsparse_operation   op,
                  const std::vector<T>& hx)
{
    setenv("ROCSPARSE_SPTRSV_CSC_COLUMNS", columns ? "1" : "0", 1);
    const rocsparse_sptrsv_alg alg = rocsparse_sptrsv_alg_default;
    hipStream_t stream;
    CHECK_ROCSPARSE(rocsparse_get_stream(handle, &stream));

    const int64_t m   = A.m;
    const int64_t nnz = A.ptr[m];
    const int64_t nb_mat = o.batch_matrix ? o.batch : 1;

    // Arrays of the stored matrix in the requested format.
    std::vector<int32_t> h_ptr, h_ind, h_row;
    std::vector<T>       h_val(nnz * nb_mat);
    std::vector<int32_t> perm(nnz);
    if(o.format == "csc")
    {
        // CSC arrays of A = CSR arrays of A^T.
        h_ptr.assign(m + 1, 0);
        for(int64_t p = 0; p < nnz; ++p)
            h_ptr[A.col[p] + 1]++;
        for(int64_t j = 0; j < m; ++j)
            h_ptr[j + 1] += h_ptr[j];
        h_ind.resize(nnz);
        std::vector<int32_t> next(h_ptr.begin(), h_ptr.end() - 1);
        for(int64_t i = 0; i < m; ++i)
            for(int32_t p = A.ptr[i]; p < A.ptr[i + 1]; ++p)
            {
                const int32_t q = next[A.col[p]]++;
                h_ind[q]        = (int32_t)i;
                perm[q]         = p;
            }
    }
    else
    {
        h_ptr = A.ptr;
        h_ind = A.col;
        for(int64_t p = 0; p < nnz; ++p)
            perm[p] = (int32_t)p;
        if(o.format == "coo")
        {
            h_row.resize(nnz);
            for(int64_t i = 0; i < m; ++i)
                for(int32_t p = A.ptr[i]; p < A.ptr[i + 1]; ++p)
                    h_row[p] = (int32_t)i;
        }
    }
    for(int64_t b = 0; b < nb_mat; ++b)
        for(int64_t p = 0; p < nnz; ++p)
            h_val[b * nnz + p] = type_info<T>::from(A.val[perm[p]]);

    int32_t *d_ptr = nullptr, *d_ind = nullptr;
    T *      d_val = nullptr, *d_x = nullptr, *d_y = nullptr;
    const std::vector<int32_t>& h_first = (o.format == "coo") ? h_row : h_ptr;
    CHECK_HIP(hipMalloc(&d_ptr, sizeof(int32_t) * h_first.size()));
    CHECK_HIP(hipMalloc(&d_ind, sizeof(int32_t) * std::max<int64_t>(nnz, 1)));
    CHECK_HIP(hipMalloc(&d_val, sizeof(T) * std::max<int64_t>(nnz * nb_mat, 1)));
    CHECK_HIP(hipMalloc(&d_x, sizeof(T) * m * o.batch));
    CHECK_HIP(hipMalloc(&d_y, sizeof(T) * m * o.batch));
    CHECK_HIP(hipMemcpy(d_ptr, h_first.data(), sizeof(int32_t) * h_first.size(), hipMemcpyHostToDevice));
    CHECK_HIP(hipMemcpy(d_ind, h_ind.data(), sizeof(int32_t) * nnz, hipMemcpyHostToDevice));
    CHECK_HIP(hipMemcpy(d_val, h_val.data(), sizeof(T) * nnz * nb_mat, hipMemcpyHostToDevice));
    CHECK_HIP(hipMemcpy(d_x, hx.data(), sizeof(T) * m * o.batch, hipMemcpyHostToDevice));

    const rocsparse_datatype dt = type_info<T>::dt;
    rocsparse_spmat_descr    mat;
    if(o.format == "csr")
        CHECK_ROCSPARSE(rocsparse_create_csr_descr(&mat, m, m, nnz, d_ptr, d_ind, d_val,
                                                   rocsparse_indextype_i32, rocsparse_indextype_i32,
                                                   rocsparse_index_base_zero, dt));
    else if(o.format == "csc")
        CHECK_ROCSPARSE(rocsparse_create_csc_descr(&mat, m, m, nnz, d_ptr, d_ind, d_val,
                                                   rocsparse_indextype_i32, rocsparse_indextype_i32,
                                                   rocsparse_index_base_zero, dt));
    else
        CHECK_ROCSPARSE(rocsparse_create_coo_descr(&mat, m, m, nnz, d_ptr, d_ind, d_val,
                                                   rocsparse_indextype_i32,
                                                   rocsparse_index_base_zero, dt));
    if(o.batch_matrix && o.batch > 1)
    {
        if(o.format == "csr")
            CHECK_ROCSPARSE(rocsparse_csr_set_strided_batch(mat, o.batch, 0, nnz));
        else if(o.format == "csc")
            CHECK_ROCSPARSE(rocsparse_csc_set_strided_batch(mat, o.batch, 0, nnz));
        else
            CHECK_ROCSPARSE(rocsparse_coo_set_strided_batch(mat, o.batch, nnz));
    }
    rocsparse_fill_mode fill = (o.fill == 'L') ? rocsparse_fill_mode_lower : rocsparse_fill_mode_upper;
    rocsparse_diag_type diag = (o.diag == 'U') ? rocsparse_diag_type_unit : rocsparse_diag_type_non_unit;
    CHECK_ROCSPARSE(rocsparse_spmat_set_attribute(mat, rocsparse_spmat_fill_mode, &fill, sizeof(fill)));
    CHECK_ROCSPARSE(rocsparse_spmat_set_attribute(mat, rocsparse_spmat_diag_type, &diag, sizeof(diag)));

    rocsparse_dnvec_descr vx, vy;
    CHECK_ROCSPARSE(rocsparse_create_dnvec_descr(&vx, m, d_x, dt));
    CHECK_ROCSPARSE(rocsparse_create_dnvec_descr(&vy, m, d_y, dt));
    if(o.batch > 1)
    {
        CHECK_ROCSPARSE(rocsparse_dnvec_set_strided_batch(vx, o.batch, m));
        CHECK_ROCSPARSE(rocsparse_dnvec_set_strided_batch(vy, o.batch, m));
    }

    const T alpha = type_info<T>::from(std::complex<double>(1.0, 0.0));
    timer   tm(o.chrono);
    result  r;

    auto make_descr = [&](rocsparse_sptrsv_descr* d) {
        CHECK_ROCSPARSE(rocsparse_create_sptrsv_descr(d));
        rocsparse_analysis_policy pol = rocsparse_analysis_policy_force;
        CHECK_ROCSPARSE(rocsparse_sptrsv_set_input(handle, *d, rocsparse_sptrsv_input_alg, &alg, sizeof(alg), nullptr));
        CHECK_ROCSPARSE(rocsparse_sptrsv_set_input(handle, *d, rocsparse_sptrsv_input_operation, &op, sizeof(op), nullptr));
        CHECK_ROCSPARSE(rocsparse_sptrsv_set_input(handle, *d, rocsparse_sptrsv_input_scalar_datatype, &dt, sizeof(dt), nullptr));
        CHECK_ROCSPARSE(rocsparse_sptrsv_set_input(handle, *d, rocsparse_sptrsv_input_compute_datatype, &dt, sizeof(dt), nullptr));
        CHECK_ROCSPARSE(rocsparse_sptrsv_set_input(handle, *d, rocsparse_sptrsv_input_analysis_policy, &pol, sizeof(pol), nullptr));
    };

    // Analysis: a fresh descriptor every time; the buffer is allocated outside the timing.
    std::vector<double>    t_analysis;
    rocsparse_sptrsv_descr d   = nullptr;
    void*                  buf = nullptr;
    for(int rep = 0; rep < std::max(1, o.reps_analysis); ++rep)
    {
        if(d)
            CHECK_ROCSPARSE(rocsparse_destroy_sptrsv_descr(d));
        make_descr(&d);
        CHECK_ROCSPARSE(rocsparse_sptrsv_buffer_size(handle, d, mat, vx, vy, rocsparse_sptrsv_stage_analysis, &r.analysis_bytes, nullptr));
        CHECK_HIP(hipMalloc(&buf, std::max<size_t>(r.analysis_bytes, 1)));
        tm.start(stream);
        CHECK_ROCSPARSE(rocsparse_sptrsv(handle, d, mat, vx, vy, rocsparse_sptrsv_stage_analysis, r.analysis_bytes, buf, nullptr));
        t_analysis.push_back(tm.stop_ms(stream));
        CHECK_HIP(hipFree(buf));
    }
    r.analysis_ms = median(t_analysis);

    // Solve.
    CHECK_ROCSPARSE(rocsparse_sptrsv_set_input(handle, d, rocsparse_sptrsv_input_scalar_alpha, &alpha, sizeof(&alpha), nullptr));
    CHECK_ROCSPARSE(rocsparse_sptrsv_buffer_size(handle, d, mat, vx, vy, rocsparse_sptrsv_stage_compute, &r.solve_bytes, nullptr));
    CHECK_HIP(hipMalloc(&buf, std::max<size_t>(r.solve_bytes, 1)));
    std::vector<double> t_solve;
    for(int rep = 0; rep < o.reps_solve + 2; ++rep)
    {
        tm.start(stream);
        CHECK_ROCSPARSE(rocsparse_sptrsv(handle, d, mat, vx, vy, rocsparse_sptrsv_stage_compute, r.solve_bytes, buf, nullptr));
        const double ms = tm.stop_ms(stream);
        if(rep >= 2) // warm-up
            t_solve.push_back(ms);
    }
    r.solve_ms = median(t_solve);

    std::vector<T> hy(m * o.batch);
    CHECK_HIP(hipMemcpy(hy.data(), d_y, sizeof(T) * m * o.batch, hipMemcpyDeviceToHost));
    r.y.resize(hy.size());
    for(size_t i = 0; i < hy.size(); ++i)
        r.y[i] = type_info<T>::to(hy[i]);

    CHECK_HIP(hipFree(buf));
    CHECK_ROCSPARSE(rocsparse_destroy_sptrsv_descr(d));
    CHECK_ROCSPARSE(rocsparse_destroy_dnvec_descr(vx));
    CHECK_ROCSPARSE(rocsparse_destroy_dnvec_descr(vy));
    CHECK_ROCSPARSE(rocsparse_destroy_spmat_descr(mat));
    CHECK_HIP(hipFree(d_ptr));
    CHECK_HIP(hipFree(d_ind));
    CHECK_HIP(hipFree(d_val));
    CHECK_HIP(hipFree(d_x));
    CHECK_HIP(hipFree(d_y));
    return r;
}

// ||op(A) y - x|| / ||x|| over all batches, with op applied to the triangle in A.
static double residual(const options& o, const host_csr& A, rocsparse_operation op,
                       const std::vector<std::complex<double>>& x,
                       const std::vector<std::complex<double>>& y, bool single)
{
    const int64_t m = A.m;
    double        num = 0, den = 0;
    for(int64_t b = 0; b < o.batch; ++b)
    {
        std::vector<std::complex<double>> r(m, 0.0);
        for(int64_t i = 0; i < m; ++i)
            for(int32_t p = A.ptr[i]; p < A.ptr[i + 1]; ++p)
            {
                const int64_t        j = A.col[p];
                std::complex<double> a = A.val[p];
                if(single)
                    a = std::complex<double>((float)a.real(), (float)a.imag());
                if(i == j && o.diag == 'U')
                    a = 1.0;
                if(op == rocsparse_operation_none)
                    r[i] += a * y[b * m + j];
                else
                    r[j] += (op == rocsparse_operation_conjugate_transpose ? std::conj(a) : a)
                            * y[b * m + i];
            }
        for(int64_t i = 0; i < m; ++i)
        {
            num += std::norm(r[i] - x[b * m + i]);
            den += std::norm(x[b * m + i]);
        }
    }
    return std::sqrt(num / std::max(den, 1e-300));
}

template <typename T>
static void bench(const options& o, rocsparse_handle handle)
{
    const bool is_complex = (o.type == "c32" || o.type == "c64");
    const bool single     = (o.type == "f32" || o.type == "c32");
    host_csr   A          = (o.matrix == "random" || o.matrix == "banded" || o.matrix == "chain"
                    || o.matrix == "chains" || o.matrix == "blockrand")
                     ? generate(o, is_complex)
                     : read_mtx(o);
    const rocsparse_operation op = (o.op == "N")    ? rocsparse_operation_none
                                   : (o.op == "CT") ? rocsparse_operation_conjugate_transpose
                                                    : rocsparse_operation_transpose;

    std::mt19937_64                        rng(o.seed + 7);
    std::uniform_real_distribution<double> u(-1.0, 1.0);
    std::vector<T>                         hx(A.m * o.batch);
    std::vector<std::complex<double>>      x(hx.size());
    for(size_t i = 0; i < hx.size(); ++i)
    {
        hx[i] = type_info<T>::from(std::complex<double>(u(rng), is_complex ? u(rng) : 0.0));
        x[i]  = type_info<T>::to(hx[i]);
    }

    result rd = run<T>(o, handle, A, false, op, hx);
    result rp = run<T>(o, handle, A, true, op, hx);

    auto rel_diff = [&](const result& r) {
        double num = 0, den = 0;
        for(size_t i = 0; i < rd.y.size(); ++i)
        {
            num += std::norm(rd.y[i] - r.y[i]);
            den += std::norm(rd.y[i]);
        }
        return std::sqrt(num / std::max(den, 1e-300));
    };

    std::string name = o.matrix;
    if(name.find('/') != std::string::npos)
        name = name.substr(name.find_last_of('/') + 1);
    if(o.matrix == "chains" || o.matrix == "blockrand")
        name += "_nb" + std::to_string(o.nb);
    if(o.header)
        printf("matrix,m,nnz,format,op,fill,diag,type,batch,batch_matrix,"
               "default_analysis_ms,push_analysis_ms,default_solve_ms,push_solve_ms,"
               "default_analysis_bytes,push_analysis_bytes,default_solve_bytes,push_solve_bytes,"
               "default_residual,push_residual,push_rel_diff\n");
    printf("%s,%lld,%d,%s,%s,%c,%c,%s,%lld,%d,%.4f,%.4f,%.4f,%.4f,%zu,%zu,%zu,%zu,%.2e,%.2e,%.2e\n",
           name.c_str(), (long long)A.m, A.ptr[A.m], o.format.c_str(), o.op.c_str(), o.fill, o.diag,
           o.type.c_str(), (long long)o.batch, (int)o.batch_matrix, rd.analysis_ms, rp.analysis_ms,
           rd.solve_ms, rp.solve_ms, rd.analysis_bytes, rp.analysis_bytes, rd.solve_bytes,
           rp.solve_bytes, residual(o, A, op, x, rd.y, single), residual(o, A, op, x, rp.y, single),
           rel_diff(rp));
}

int main(int argc, char** argv)
{
    const options o = parse(argc, argv);
    rocsparse_handle handle;
    CHECK_ROCSPARSE(rocsparse_create_handle(&handle));
    CHECK_ROCSPARSE(rocsparse_set_pointer_mode(handle, rocsparse_pointer_mode_host));
    if(o.type == "f32") bench<float>(o, handle);
    else if(o.type == "f64") bench<double>(o, handle);
    else if(o.type == "c32") bench<rocsparse_float_complex>(o, handle);
    else if(o.type == "c64") bench<rocsparse_double_complex>(o, handle);
    else
    {
        usage();
        return 1;
    }
    CHECK_ROCSPARSE(rocsparse_destroy_handle(handle));
    return 0;
}
