# Tests for the rocSOLVER Fortran bindings: one executable per test, driven by
# CTest. They factor or diagonalize a small matrix on a device, so they build
# anywhere but only pass on a machine with a GPU. Reached only from the parent
# CMakeLists, under BUILD_FORTRAN_CLIENTS.

# The Intel compilers (ifx/ifort) do not recognize the .f03 extension and skip
# such files ("no action performed"). Inserting -Tf before the source forces
# Fortran compilation; the LANGUAGE source property alone is not enough.
if(CMAKE_Fortran_COMPILER_ID MATCHES "Intel")
  string(REPLACE "<SOURCE>" "-Tf <SOURCE>"
         CMAKE_Fortran_COMPILE_OBJECT "${CMAKE_Fortran_COMPILE_OBJECT}")
endif()

# The tests allocate and copy device memory, so each says `use hip`. That makes
# the HIP Fortran binding a COMPILE-time dependency: its .mod must exist, where
# librocsolver is only a link-time one. In-tree hip::hip_fortran comes from
# projects/hip/fortran; standalone it has to be found in an installed ROCm, built
# with this same compiler.
if(NOT TARGET hip::hip_fortran)
  find_package(hip-fortran QUIET)
endif()
set(_have_hip_binding TRUE)
if(NOT TARGET hip::hip_fortran)
  set(_have_hip_binding FALSE)
  message(STATUS
    "rocSOLVER: no hip::hip_fortran target, skipping the Fortran binding tests "
    "(they `use hip`). Point CMAKE_PREFIX_PATH at a ROCm whose HIP Fortran "
    "bindings were built with ${CMAKE_Fortran_COMPILER}.")
endif()

# librocsolver itself, needed to link but not to compile. In-tree it arrives
# through the rocsolver_fortran link interface; standalone it has to be found,
# and C is enabled first for the same FindThreads reason as in the parent
# CMakeLists. It compiles none of the sources below.
if(NOT TARGET roc::rocsolver)
  if(NOT CMAKE_C_COMPILER_LOADED AND NOT CMAKE_CXX_COMPILER_LOADED)
    enable_language(C)
  endif()
  find_package(rocsolver QUIET)
endif()
if(NOT TARGET roc::rocsolver)
  message(STATUS
    "rocSOLVER: no roc::rocsolver target, skipping the Fortran binding tests "
    "(they link librocsolver). Point CMAKE_PREFIX_PATH at an installed ROCm to "
    "build them standalone.")
  return()
endif()

# Build <name>.f03 into one executable and register it with CTest as
# rocsolver_fortran_<name>. No explicit rocblas_fortran: rocsolver_fortran
# carries it PUBLIC, so the rocblas.mod the tests need comes with it.
function(rocsolver_fortran_add_test name)
  set(src "${CMAKE_CURRENT_SOURCE_DIR}/${name}.f03")
  if(NOT EXISTS "${src}")
    message(FATAL_ERROR "Missing test source: ${src}")
  endif()
  # Force language detection for .f03 in toolchains where CMake misses it.
  set_source_files_properties("${src}" PROPERTIES LANGUAGE Fortran)
  add_executable(rocsolver_fortran_test_${name} "${src}")
  set_target_properties(rocsolver_fortran_test_${name} PROPERTIES LINKER_LANGUAGE Fortran)
  target_link_libraries(rocsolver_fortran_test_${name} PRIVATE
    rocsolver_fortran hip::hip_fortran roc::rocsolver)
  # The tests call EXIT, a GNU extension in gfortran.
  target_compile_options(rocsolver_fortran_test_${name} PRIVATE
    $<$<COMPILE_LANG_AND_ID:Fortran,GNU>:-std=gnu>)
  add_test(NAME rocsolver_fortran_${name} COMMAND rocsolver_fortran_test_${name})
  # These fail without a GPU, which is not the binding being broken, so the
  # label lets a GPU-less machine run the suite with ctest -LE gpu.
  set_tests_properties(rocsolver_fortran_${name} PROPERTIES LABELS "gpu")
endfunction()

# One per routine family and precision, carried over from hipFORT's rocSOLVER
# suite: the factorizations, the solvers, the orthogonal factor routines, the
# symmetric and Hermitian eigensolvers and the SVDs, each across the s/d/c/z
# types the API defines them for.
set(ROCSOLVER_FORTRAN_TESTS
    rocsolver_alg_mode
    rocsolver_cgebrd
    rocsolver_cgels
    rocsolver_cgeqrf
    rocsolver_cgesvd
    rocsolver_cgesvdj
    rocsolver_cgetrf
    rocsolver_cgetrs
    rocsolver_cheevd
    rocsolver_cheevdx
    rocsolver_cheevj
    rocsolver_chegvd
    rocsolver_chegvdx
    rocsolver_cpotrf
    rocsolver_cpotrs
    rocsolver_csytrf
    rocsolver_csytrs
    rocsolver_cungqr
    rocsolver_cunmqr
    rocsolver_dgebrd
    rocsolver_dgels
    rocsolver_dgeqrf
    rocsolver_dgesv
    rocsolver_dgesvd
    rocsolver_dgesvdj
    rocsolver_dgetrf
    rocsolver_dgetrf_strided_batched
    rocsolver_dgetri
    rocsolver_dgetrs
    rocsolver_dlarft
    rocsolver_dlatrd
    rocsolver_dorgqr
    rocsolver_dormqr
    rocsolver_dposv
    rocsolver_dpotrf
    rocsolver_dpotrs
    rocsolver_dsterf
    rocsolver_dsyev
    rocsolver_dsyevd
    rocsolver_dsyevdx
    rocsolver_dsyevj
    rocsolver_dsygvd
    rocsolver_dsygvdx
    rocsolver_dsytrd
    rocsolver_dsytrf
    rocsolver_dsytrs
    rocsolver_dtrtri
    rocsolver_sgebrd
    rocsolver_sgels
    rocsolver_sgeqrf
    rocsolver_sgesvd
    rocsolver_sgesvdj
    rocsolver_sgetrf
    rocsolver_sgetrs
    rocsolver_sorgqr
    rocsolver_sormqr
    rocsolver_spotrf
    rocsolver_spotrs
    rocsolver_ssyevd
    rocsolver_ssyevdx
    rocsolver_ssyevj
    rocsolver_ssygvd
    rocsolver_ssygvdx
    rocsolver_ssytrf
    rocsolver_ssytrs
    rocsolver_version
    rocsolver_zgebrd
    rocsolver_zgels
    rocsolver_zgeqrf
    rocsolver_zgesvd
    rocsolver_zgesvdj
    rocsolver_zgetrf
    rocsolver_zgetrs
    rocsolver_zheevd
    rocsolver_zheevdx
    rocsolver_zheevj
    rocsolver_zhegvd
    rocsolver_zhegvdx
    rocsolver_zpotrf
    rocsolver_zpotrs
    rocsolver_zsytrf
    rocsolver_zsytrs
    rocsolver_zungqr
    rocsolver_zunmqr)

if(_have_hip_binding)
  foreach(_t IN LISTS ROCSOLVER_FORTRAN_TESTS)
    rocsolver_fortran_add_test(${_t})
  endforeach()
endif()
