# Tests for the hipRAND Fortran bindings: one executable per test, driven by
# CTest -- 23 single-source programs that each check one (generator,
# distribution) pair, plus the FRUIT suite. All of them create a generator and
# use a device, so they build anywhere but only pass on a machine with a GPU.
# Reached only from the parent, under BUILD_FORTRAN_CLIENTS.

# The Intel compilers do not recognize the .f03 source extension and skip such
# files ("no action performed"). Force Fortran compilation with -Tf immediately
# before the source; the LANGUAGE source property alone is not enough.
if(CMAKE_Fortran_COMPILER_ID MATCHES "Intel")
  string(REPLACE "<SOURCE>" "-Tf <SOURCE>"
         CMAKE_Fortran_COMPILE_OBJECT "${CMAKE_Fortran_COMPILE_OBJECT}")
endif()

# Unlike the binding itself, the RUNTIME tests are not self-contained: they
# allocate and copy device memory, so each says `use hip` as well as
# `use hiprand`. That makes the HIP Fortran binding a COMPILE-time dependency --
# its .mod must exist, where libhiprand is only a link-time one. Standalone it
# has to be found in an installed ROCm, built with this same compiler.
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
# hiprand-config.cmake does find_dependency(hip), and hip-config.cmake reaches
# FindThreads, which hard-errors unless C or CXX is enabled, so C is enabled
# here and compiles none of the sources below.
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

# Build one test executable and register it with CTest. Sources are the
# arguments after `name`, relative to this directory; with none given the single
# source is `${name}.f03`, which the runtime tests below rely on. The FRUIT
# driver is multi-source and passes its five files explicitly so that it goes
# through this same link line: the three link dependencies are the same for
# both, and two copies would have to be kept in step by hand.
function(hiprand_fortran_add_test name)
  set(srcs ${ARGN})
  if(NOT srcs)
    set(srcs "${name}.f03")
  endif()
  set(abs_srcs "")
  foreach(src IN LISTS srcs)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${src}")
      message(FATAL_ERROR "Missing test source: ${CMAKE_CURRENT_SOURCE_DIR}/${src}")
    endif()
    list(APPEND abs_srcs "${CMAKE_CURRENT_SOURCE_DIR}/${src}")
  endforeach()
  # Force language detection for .f03 in toolchains where CMake misses it.
  set_source_files_properties(${abs_srcs} PROPERTIES LANGUAGE Fortran)
  add_executable(hiprand_fortran_test_${name} ${abs_srcs})
  # A multi-source test defines modules of its own, so each target needs a
  # directory for them rather than colliding in the shared build directory.
  set_target_properties(hiprand_fortran_test_${name} PROPERTIES
    LINKER_LANGUAGE Fortran
    Fortran_MODULE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/modules/${name}")
  target_link_libraries(hiprand_fortran_test_${name} PRIVATE
    hiprand_fortran hip::hip_fortran hip::hiprand)
  # The tests call EXIT, a GNU extension in gfortran.
  target_compile_options(hiprand_fortran_test_${name} PRIVATE
    $<$<COMPILE_LANG_AND_ID:Fortran,GNU>:-std=gnu>)
  add_test(NAME hiprand_fortran_${name} COMMAND hiprand_fortran_test_${name})
  # Every one of these draws numbers on a device, so without a GPU it fails for
  # a reason that is not the binding being broken. The label lets a build
  # machine with no GPU still run the suite meaningfully: ctest -LE gpu.
  set_tests_properties(hiprand_fortran_${name} PROPERTIES LABELS "gpu")
endfunction()

# One per (generator, distribution) pair: five generators against uniform,
# normal and log-normal in both precisions, plus Poisson. Between them they
# cover every hiprandGenerate* entry point the module exposes that returns data
# a host can check the distribution of.
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
    hiprand_version
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

  # The FRUIT suite, which moved here with the binding it now exercises. It
  # asserts on the status code of every call it makes, which is how it reaches
  # entry points none of the tests above names: hiprandGenerate,
  # hiprandGenerateSeeds, hiprandGenerateLongLong, hiprandCreateGeneratorHost,
  # hiprandSetGeneratorOffset, hiprandCreatePoissonDistribution and
  # hiprandDestroyDistribution. ONE executable over five sources: the two files
  # of the vendored harness, the setup/teardown helper, the test module and the
  # driver program.
  hiprand_fortran_add_test(fruit
      fruit/fruit_util.f90
      fruit/fruit.f90
      fruit_helpers.f90
      test_hiprand.f90
      fruit_test_driver_hiprand.f90)
endif()
