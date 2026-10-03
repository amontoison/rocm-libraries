# Tests for the hipSPARSE Fortran bindings.
#
# One executable per test, driven by CTest. Each drives one hipSPARSE routine
# through the binding on a device, so they build anywhere but only pass on a
# machine with a GPU. Reached only from the parent CMakeLists, under
# BUILD_FORTRAN_CLIENTS.

# The Intel compilers (ifx/ifort) do not recognize the .f03 source extension and
# skip such files ("no action performed"). Force Fortran compilation by inserting
# -Tf immediately before the source in the compile rule; the LANGUAGE Fortran
# source property alone is not enough.
if(CMAKE_Fortran_COMPILER_ID MATCHES "Intel")
  string(REPLACE "<SOURCE>" "-Tf <SOURCE>"
         CMAKE_Fortran_COMPILE_OBJECT "${CMAKE_Fortran_COMPILE_OBJECT}")
endif()

# The tests allocate and copy device memory, so each says `use hip` as well as
# `use hipsparse`. That makes the HIP Fortran binding a COMPILE-time dependency
# -- its .mod must exist -- where libhipsparse is only a link-time one. In-tree
# hip::hip_fortran comes from projects/hip/fortran; standalone it has to be
# found in an installed ROCm, built with this same compiler.
if(NOT TARGET hip::hip_fortran)
  find_package(hip-fortran QUIET)
endif()
set(_have_hip_binding TRUE)
if(NOT TARGET hip::hip_fortran)
  set(_have_hip_binding FALSE)
  message(STATUS
    "hipSPARSE: no hip::hip_fortran target, skipping the Fortran binding tests "
    "(they `use hip`). Point CMAKE_PREFIX_PATH at a ROCm whose HIP Fortran "
    "bindings were built with ${CMAKE_Fortran_COMPILER}.")
endif()

# libhipsparse itself, needed to link but not to compile. In-tree it arrives
# through the hipsparse_fortran link interface; standalone it has to be found.
# hipsparse-config.cmake does find_dependency(hip), whose hip-config.cmake
# reaches FindThreads and hard-errors unless C or CXX is enabled, so C is
# enabled here purely to satisfy it (it compiles none of the sources below).
if(NOT TARGET roc::hipsparse)
  if(NOT CMAKE_C_COMPILER_LOADED AND NOT CMAKE_CXX_COMPILER_LOADED)
    enable_language(C)
  endif()
  find_package(hipsparse QUIET)
endif()
if(NOT TARGET roc::hipsparse)
  message(STATUS
    "hipSPARSE: no roc::hipsparse target, skipping the Fortran binding tests "
    "(they link libhipsparse). Point CMAKE_PREFIX_PATH at an installed ROCm to "
    "build them standalone.")
  return()
endif()

# Build <name>.f03 into one executable and register it with CTest as
# hipsparse_fortran_<name>, labelled gpu.
function(hipsparse_fortran_add_test name)
  set(src "${CMAKE_CURRENT_SOURCE_DIR}/${name}.f03")
  if(NOT EXISTS "${src}")
    message(FATAL_ERROR "Missing test source: ${src}")
  endif()
  # Force language detection for .f03 in toolchains where CMake misses it.
  set_source_files_properties("${src}" PROPERTIES LANGUAGE Fortran)
  add_executable(hipsparse_fortran_test_${name} "${src}")
  set_target_properties(hipsparse_fortran_test_${name} PROPERTIES LINKER_LANGUAGE Fortran)
  target_link_libraries(hipsparse_fortran_test_${name} PRIVATE
    hipsparse_fortran hip::hip_fortran roc::hipsparse)
  # The tests call EXIT, a GNU extension in gfortran.
  target_compile_options(hipsparse_fortran_test_${name} PRIVATE
    $<$<COMPILE_LANG_AND_ID:Fortran,GNU>:-std=gnu>)
  add_test(NAME hipsparse_fortran_${name} COMMAND hipsparse_fortran_test_${name})
  # These open a hipSPARSE handle and allocate device memory, so they fail
  # without a GPU, which is not the binding being broken. The label lets a
  # GPU-less build machine still run the suite: ctest -LE gpu.
  set_tests_properties(hipsparse_fortran_${name} PROPERTIES LABELS "gpu")
endfunction()

# One per routine family and precision, carried over from hipFORT's hipSPARSE
# suite: the SpMV/SpMM kernels, the conversion routines, the incomplete
# factorizations and the triangular solves, each across the s/d/c/z types.
set(HIPSPARSE_FORTRAN_TESTS
    hipsparse_ccsr2csc
    hipsparse_ccsrgemm
    hipsparse_ccsrilu02
    hipsparse_ccsrsv2
    hipsparse_cgemvi
    hipsparse_cgthr
    hipsparse_csctr
    hipsparse_csddmm
    hipsparse_cspmm
    hipsparse_cspmv
    hipsparse_csptrsm
    hipsparse_csptrsv
    hipsparse_dcsr2csc
    hipsparse_dcsrgemm
    hipsparse_dcsrilu02
    hipsparse_dcsrsv2
    hipsparse_descr_get
    hipsparse_dgemvi
    hipsparse_dgthr
    hipsparse_dsctr
    hipsparse_dsddmm
    hipsparse_dspmm
    hipsparse_dspmv
    hipsparse_dsptrsm
    hipsparse_dsptrsv
    hipsparse_pointer_mode
    hipsparse_scsrgemm
    hipsparse_scsrilu02
    hipsparse_scsrsv2
    hipsparse_sgemvi
    hipsparse_sgthr
    hipsparse_ssctr
    hipsparse_ssddmm
    hipsparse_sspmm
    hipsparse_sspmv
    hipsparse_ssptrsm
    hipsparse_ssptrsv
    hipsparse_version
    hipsparse_xcoo2csr
    hipsparse_xcsr2coo
    hipsparse_zcsr2csc
    hipsparse_zcsrgemm
    hipsparse_zcsrilu02
    hipsparse_zcsrsv2
    hipsparse_zgemvi
    hipsparse_zgthr
    hipsparse_zsctr
    hipsparse_zsddmm
    hipsparse_zspmm
    hipsparse_zspmv
    hipsparse_zsptrsm
    hipsparse_zsptrsv)

if(_have_hip_binding)
  foreach(_t IN LISTS HIPSPARSE_FORTRAN_TESTS)
    hipsparse_fortran_add_test(${_t})
  endforeach()
endif()
