# Tests for the hipFFT and hipFFTW Fortran bindings: one executable per test,
# driven by CTest. They create plans, run transforms and check against an
# analytic answer, so they build anywhere but only pass on a machine with a GPU.
#
# Reached only from the parent CMakeLists, when either half's tests were asked
# for. Each half then guards itself on its own binding target and its own C
# library, so -DHIPFFTW_BUILD_FORTRAN_CLIENTS=OFF leaves hipFFT's suite intact.

# The Intel compilers (ifx/ifort) do not recognize the .f03 extension and skip
# such files ("no action performed"). Inserting -Tf before the source forces
# Fortran compilation; the LANGUAGE source property alone is not enough.
if(CMAKE_Fortran_COMPILER_ID MATCHES "Intel")
  string(REPLACE "<SOURCE>" "-Tf <SOURCE>"
         CMAKE_Fortran_COMPILE_OBJECT "${CMAKE_Fortran_COMPILE_OBJECT}")
endif()

# libhipfft and libhipfftw are link-time dependencies of the two suites. One
# lookup settles both, because the installed hipfft-targets.cmake defines
# hip::hipfft and hip::hipfftw together; the parent has already attempted it, and
# it is retried here so this directory is correct if reached by a path that
# skipped the parent's lookup. C is enabled first for the same FindThreads reason
# as in the parent CMakeLists; it compiles none of the sources below.
if(NOT TARGET hip::hipfft AND NOT TARGET hip::hipfftw)
  if(NOT CMAKE_C_COMPILER_LOADED AND NOT CMAKE_CXX_COMPILER_LOADED)
    enable_language(C)
  endif()
  find_package(hipfft QUIET)
endif()

# Every test in both suites also writes `use hip`, to allocate and copy device
# memory before handing pointers to a plan. That binding is a separate package in
# rocm-systems, never a target inside a rocm-libraries build, and must have been
# built with the SAME Fortran compiler for its .mod to be readable here. Always
# found rather than assumed, and its absence skips both suites.
if(NOT TARGET hip::hip_fortran)
  find_package(hip-fortran QUIET)
endif()
# Retried beside hip's own config if that missed: an install predating the
# lib/cmake/<lib>-fortran forwarder is reachable only through hip_DIR, which the
# find_package(hipfft) above set through its hip dependency.
if(NOT TARGET hip::hip_fortran AND hip_DIR)
  find_package(hip-fortran QUIET PATHS "${hip_DIR}" NO_DEFAULT_PATH)
endif()
if(NOT TARGET hip::hip_fortran)
  message(STATUS
    "hipFFT: no hip::hip_fortran target, skipping the Fortran runtime tests "
    "of both bindings (they `use hip`). Install the HIP Fortran bindings for "
    "this compiler to build them.")
endif()

# --------------------------------------------------------------------------
# hipFFT
# --------------------------------------------------------------------------
if(NOT _hipfft_tests)
  # Deliberately silent: the parent was told not to build this half.
elseif(NOT TARGET hipfft_fortran)
  message(STATUS
    "hipFFT: the Fortran binding target was not built, skipping its tests.")
elseif(NOT TARGET hip::hipfft)
  message(STATUS
    "hipFFT: no hip::hipfft target, skipping the hipFFT Fortran binding tests "
    "(they link libhipfft). Point CMAKE_PREFIX_PATH at an installed ROCm to "
    "build them standalone.")
else()
  function(hipfft_fortran_add_test name)
    set(src "${CMAKE_CURRENT_SOURCE_DIR}/${name}.f03")
    if(NOT EXISTS "${src}")
      message(FATAL_ERROR "Missing test source: ${src}")
    endif()
    # Force language detection for .f03 in toolchains where CMake misses it.
    set_source_files_properties("${src}" PROPERTIES LANGUAGE Fortran)
    add_executable(hipfft_fortran_test_${name} "${src}")
    set_target_properties(hipfft_fortran_test_${name} PROPERTIES LINKER_LANGUAGE Fortran)
    target_link_libraries(hipfft_fortran_test_${name}
                          PRIVATE hipfft_fortran hip::hip_fortran hip::hipfft)
    # The tests call EXIT, a GNU extension in gfortran.
    target_compile_options(hipfft_fortran_test_${name} PRIVATE
      $<$<COMPILE_LANG_AND_ID:Fortran,GNU>:-std=gnu>)
    add_test(NAME hipfft_fortran_${name} COMMAND hipfft_fortran_test_${name})
    # These fail without a GPU, which is not the binding being broken, so the
    # label lets a GPU-less machine run the suite with ctest -LE gpu.
    set_tests_properties(hipfft_fortran_${name} PROPERTIES LABELS "gpu")
  endfunction()

  if(TARGET hip::hip_fortran)
    set(HIPFFT_FORTRAN_TESTS
        hipfft
        hipfft_c2c_1d_batched_z
        hipfft_c2c_1d_c
        hipfft_c2c_1d_z
        hipfft_c2c_2d_z
        hipfft_c2c_3d_z
        hipfft_estimate_getsize_d
        hipfft_makeplanmany_z
        hipfft_planmany_2d_z2z
        hipfft_r2c_c2r_1d_d
        hipfft_r2c_c2r_1d_s
        hipfft_setstream_z)

    # hipfft_version passes output scalars straight to hipfftGetProperty, which
    # resolves only through the assumed-shape/assumed-rank module procedures:
    # under `none` the interface is the raw bind(C) specific, whose dummy is
    # type(c_ptr),value, so the call matches nothing and the test does not
    # compile. Register it only for the tiers that provide the generic.
    if(NOT FORTRAN_ARRAY_INTERFACES STREQUAL "none")
      list(APPEND HIPFFT_FORTRAN_TESTS hipfft_version)
    endif()

    foreach(_t IN LISTS HIPFFT_FORTRAN_TESTS)
      hipfft_fortran_add_test(${_t})
    endforeach()
  endif()
endif()

# --------------------------------------------------------------------------
# hipFFTW
# --------------------------------------------------------------------------
if(NOT _hipfftw_tests)
  # Deliberately silent: the parent was told not to build this half.
elseif(NOT TARGET hipfftw_fortran)
  message(STATUS
    "hipFFTW: the Fortran binding target was not built, skipping its tests.")
elseif(NOT TARGET hip::hipfftw)
  message(STATUS
    "hipFFTW: no hip::hipfftw target, skipping the hipFFTW Fortran binding "
    "tests (they link libhipfftw). Point CMAKE_PREFIX_PATH at an installed "
    "ROCm to build them standalone.")
else()
  function(hipfftw_fortran_add_test name)
    set(src "${CMAKE_CURRENT_SOURCE_DIR}/${name}.f03")
    if(NOT EXISTS "${src}")
      message(FATAL_ERROR "Missing test source: ${src}")
    endif()
    set_source_files_properties("${src}" PROPERTIES LANGUAGE Fortran)
    add_executable(hipfftw_fortran_test_${name} "${src}")
    set_target_properties(hipfftw_fortran_test_${name} PROPERTIES
                          LINKER_LANGUAGE Fortran)
    # hip::hipfftw and never hip::hipfft: hipfftw.F90 contains no `use hipfft`
    # and nothing here links against hipFFT's own surface.
    target_link_libraries(hipfftw_fortran_test_${name} PRIVATE
                          hipfftw_fortran hip::hipfftw hip::hip_fortran)
    # The tests call EXIT, a GNU extension in gfortran.
    target_compile_options(hipfftw_fortran_test_${name} PRIVATE
      $<$<COMPILE_LANG_AND_ID:Fortran,GNU>:-std=gnu>)
    add_test(NAME hipfftw_fortran_${name} COMMAND hipfftw_fortran_test_${name})
    # These fail without a GPU, which is not the binding being broken.
    set_tests_properties(hipfftw_fortran_${name} PROPERTIES LABELS "gpu")
  endfunction()

  if(TARGET hip::hip_fortran)
    set(HIPFFTW_FORTRAN_TESTS
        hipfftw_alloc
        hipfftw_c2c
        hipfftw_dft_2d
        hipfftw_dft_3d
        hipfftw_guru
        hipfftw_many
        hipfftw_r2c_c2r)

    foreach(_t IN LISTS HIPFFTW_FORTRAN_TESTS)
      hipfftw_fortran_add_test(${_t})
    endforeach()
  endif()
endif()
