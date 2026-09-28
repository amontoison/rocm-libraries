# Tests for the hipRAND Fortran bindings.
#
# One executable per test, driven by CTest. The runtime tests create a generator
# and draw numbers on a device, so they build anywhere but only pass on a machine
# with a GPU.
#
# Reached only from the parent CMakeLists, under BUILD_FORTRAN_CLIENTS.

# The Intel compilers (ifx/ifort) do not recognize the .f03 source extension and
# skip such files ("no action performed"). Force Fortran compilation by inserting
# -Tf immediately before the source in the compile rule; the LANGUAGE Fortran
# source property alone is not enough.
if(CMAKE_Fortran_COMPILER_ID MATCHES "Intel")
  string(REPLACE "<SOURCE>" "-Tf <SOURCE>"
         CMAKE_Fortran_COMPILE_OBJECT "${CMAKE_Fortran_COMPILE_OBJECT}")
endif()

# Unlike the binding itself, the RUNTIME tests are not self-contained: they
# allocate and copy device memory, so each says `use hip` as well as
# `use hiprand`. That makes the HIP Fortran binding a COMPILE-time dependency of
# those tests: its .mod must exist, where libhiprand is only a link-time one.
# In a monorepo build hip::hip_fortran comes from projects/hip/fortran; a
# standalone build has to find it in an installed ROCm, built with this same
# compiler.
if(NOT TARGET hip::hip_fortran)
  find_package(hip-fortran QUIET)
endif()
set(_have_hip_binding TRUE)
if(NOT TARGET hip::hip_fortran)
  set(_have_hip_binding FALSE)
  message(STATUS
    "hipRAND: no hip::hip_fortran target, skipping the Fortran binding tests "
    "(they `use hip`). Point CMAKE_PREFIX_PATH at a ROCm whose HIP Fortran "
    "bindings were built with ${CMAKE_Fortran_COMPILER}.")
endif()

# libhiprand itself, needed to link but not to compile. In-tree it arrives
# through the hiprand_fortran link interface; standalone it has to be found.
#
# hiprand-config.cmake does find_dependency(hip), and hip-config.cmake reaches
# FindThreads, which hard-errors unless C or CXX is enabled. The bindings are
# Fortran-only and standalone this project declares LANGUAGES NONE, so C is
# enabled here purely to satisfy that dependency: it compiles none of the
# sources below. Without it, a standalone test build stops at "FindThreads only
# works if either C or CXX language is enabled", which says nothing about what
# is actually wrong.
if(NOT TARGET hip::hiprand)
  if(NOT CMAKE_C_COMPILER_LOADED AND NOT CMAKE_CXX_COMPILER_LOADED)
    enable_language(C)
  endif()
  find_package(hiprand QUIET)
endif()
if(NOT TARGET hip::hiprand)
  message(STATUS
    "hipRAND: no hip::hiprand target, skipping the Fortran binding tests "
    "(they link libhiprand). Point CMAKE_PREFIX_PATH at an installed ROCm to "
    "build them standalone.")
  return()
endif()

function(hiprand_fortran_add_test name)
  set(src "${CMAKE_CURRENT_SOURCE_DIR}/${name}.f03")
  if(NOT EXISTS "${src}")
    message(FATAL_ERROR "Missing test source: ${src}")
  endif()
  # Force language detection for .f03 in toolchains where CMake misses it.
  set_source_files_properties("${src}" PROPERTIES LANGUAGE Fortran)
  add_executable(hiprand_fortran_test_${name} "${src}")
  set_target_properties(hiprand_fortran_test_${name} PROPERTIES LINKER_LANGUAGE Fortran)
  target_link_libraries(hiprand_fortran_test_${name} PRIVATE
    hiprand_fortran hip::hip_fortran hip::hiprand)
  # The tests call EXIT, a GNU extension in gfortran.
  target_compile_options(hiprand_fortran_test_${name} PRIVATE
    $<$<COMPILE_LANG_AND_ID:Fortran,GNU>:-std=gnu>)
  add_test(NAME hiprand_fortran_${name} COMMAND hiprand_fortran_test_${name})
  # Every one of these creates a generator and draws numbers on a device, so it
  # fails without a GPU, which is not the same thing as the binding being
  # broken. The label lets a build machine with no GPU still run the suite
  # meaningfully: ctest -LE gpu.
  set_tests_properties(hiprand_fortran_${name} PROPERTIES LABELS "gpu")
endfunction()

# One per (generator, distribution) pair: five generators against uniform,
# normal and log-normal in both precisions, plus Poisson. Between them they
# cover every hiprandGenerate* entry point the module exposes that returns
# data a host can check the distribution of.
set(HIPRAND_FORTRAN_TESTS
    mrg32k3a_normal
    mrg32k3a_normal_double
    mrg32k3a_uniform
    mrg32k3a_uniform_double
    mtgp32_normal
    mtgp32_normal_double
    mtgp32_uniform
    mtgp32_uniform_double
    philox_log_normal
    philox_log_normal_double
    philox_normal
    philox_normal_double
    philox_poisson
    philox_uniform
    philox_uniform_double
    sobol32_normal
    sobol32_normal_double
    sobol32_uniform
    sobol32_uniform_double
    xorwow_normal
    xorwow_normal_double
    xorwow_uniform
    xorwow_uniform_double)

if(_have_hip_binding)
  foreach(_t IN LISTS HIPRAND_FORTRAN_TESTS)
    hiprand_fortran_add_test(${_t})
  endforeach()
endif()
