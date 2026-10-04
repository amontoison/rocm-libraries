# Tests for the hipSOLVER Fortran bindings: one executable per test, driven by
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
# libhipsolver is only a link-time one. hip::hip_fortran comes from
# find_package(hip-fortran) -- an installed ROCm whose HIP binding was built with
# this same compiler -- unless an enclosing project already defined the target.
if(NOT TARGET hip::hip_fortran)
  find_package(hip-fortran QUIET)
endif()
set(_have_hip_binding TRUE)
if(NOT TARGET hip::hip_fortran)
  set(_have_hip_binding FALSE)
  message(STATUS
    "hipSOLVER: no hip::hip_fortran target, skipping the Fortran binding tests "
    "(they `use hip`). Point CMAKE_PREFIX_PATH at a ROCm whose HIP Fortran "
    "bindings were built with ${CMAKE_Fortran_COMPILER}.")
endif()

# libhipsolver itself, needed to link but not to compile. In-tree it arrives
# through the hipsolver_fortran link interface; standalone it has to be found,
# and C is enabled first for the same FindThreads reason as in the parent
# CMakeLists. It compiles none of the sources below.
if(NOT TARGET roc::hipsolver)
  if(NOT CMAKE_C_COMPILER_LOADED AND NOT CMAKE_CXX_COMPILER_LOADED)
    enable_language(C)
  endif()
  find_package(hipsolver QUIET)
endif()
if(NOT TARGET roc::hipsolver)
  message(STATUS
    "hipSOLVER: no roc::hipsolver target, skipping the Fortran binding tests "
    "(they link libhipsolver). Point CMAKE_PREFIX_PATH at an installed ROCm to "
    "build them standalone.")
  return()
endif()

# Build <name>.f03 into one executable and register it with CTest as
# hipsolver_fortran_<name>.
function(hipsolver_fortran_add_test name)
  set(src "${CMAKE_CURRENT_SOURCE_DIR}/${name}.f03")
  if(NOT EXISTS "${src}")
    message(FATAL_ERROR "Missing test source: ${src}")
  endif()
  # Force language detection for .f03 in toolchains where CMake misses it.
  set_source_files_properties("${src}" PROPERTIES LANGUAGE Fortran)
  add_executable(hipsolver_fortran_test_${name} "${src}")
  set_target_properties(hipsolver_fortran_test_${name} PROPERTIES LINKER_LANGUAGE Fortran)
  target_link_libraries(hipsolver_fortran_test_${name} PRIVATE
    hipsolver_fortran hip::hip_fortran roc::hipsolver)
  # The tests call EXIT, a GNU extension in gfortran.
  target_compile_options(hipsolver_fortran_test_${name} PRIVATE
    $<$<COMPILE_LANG_AND_ID:Fortran,GNU>:-std=gnu>)
  add_test(NAME hipsolver_fortran_${name} COMMAND hipsolver_fortran_test_${name})
  # These fail without a GPU, which is not the binding being broken, so the
  # label lets a GPU-less machine run the suite with ctest -LE gpu.
  set_tests_properties(hipsolver_fortran_${name} PROPERTIES LABELS "gpu")
endfunction()

# One per routine family and precision, carried over from hipFORT's hipSOLVER
# suite: the factorizations, the orthogonal factor routines, the symmetric and
# Hermitian eigensolvers and the SVDs, each across the s/d/c/z types the API
# defines them for. Plus a few covering the query routines, whose outputs the C
# API fills by reference: deterministic mode, the Rf parameters, the syevj info.
set(HIPSOLVER_FORTRAN_TESTS
    hipsolver_cgeqrf
    hipsolver_cgesvd
    hipsolver_cgesvdj
    hipsolver_cgetrf
    hipsolver_cheevd
    hipsolver_cheevdx
    hipsolver_chegvd
    hipsolver_chegvdx
    hipsolver_cpotrfbatched
    hipsolver_cpotrs
    hipsolver_cungqr
    hipsolver_cunmqr
    hipsolver_deterministic_mode
    hipsolver_dgeqrf
    hipsolver_dgesvdj
    hipsolver_dgetrf
    hipsolver_dorgqr
    hipsolver_dormqr
    hipsolver_dpotrfbatched
    hipsolver_dpotrs
    hipsolver_dsyevdx
    hipsolver_dsyevj
    hipsolver_dsygvd
    hipsolver_dsygvdx
    hipsolver_rf_params
    hipsolver_sgeqrf
    hipsolver_sgesvd
    hipsolver_sgesvdj
    hipsolver_sgetrf
    hipsolver_sorgqr
    hipsolver_sormqr
    hipsolver_spotrfbatched
    hipsolver_spotrs
    hipsolver_ssyevd
    hipsolver_ssyevdx
    hipsolver_ssyevj
    hipsolver_ssygvd
    hipsolver_ssygvdx
    hipsolver_syevj_info
    hipsolver_zgeqrf
    hipsolver_zgesvd
    hipsolver_zgesvdj
    hipsolver_zgetrf
    hipsolver_zheevd
    hipsolver_zheevdx
    hipsolver_zhegvd
    hipsolver_zhegvdx
    hipsolver_zpotrfbatched
    hipsolver_zpotrs
    hipsolver_zungqr
    hipsolver_zunmqr)

if(_have_hip_binding)
  foreach(_t IN LISTS HIPSOLVER_FORTRAN_TESTS)
    hipsolver_fortran_add_test(${_t})
  endforeach()
endif()
