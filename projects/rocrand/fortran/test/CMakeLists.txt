# Tests for the rocRAND Fortran bindings.
#
# One executable per test, driven by CTest. The runtime tests create a generator
# and draw numbers on a device, so they build anywhere but only pass on a machine
# with a GPU.
#
# Two suites live here. The .f03 sources are one test per (generator,
# distribution) pair. fruit/ and the three .f90 sources beside it are the
# FRUIT-based suite that used to sit in test/fortran/ and exercise the
# hand-written wrapper; it moved here with the binding it now exercises, so that
# every Fortran test sits next to the module it tests and one hip::hip_fortran
# guard covers them all.
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
# `use rocrand`. That makes the HIP Fortran binding a COMPILE-time dependency of
# those tests: its .mod must exist, where librocrand is only a link-time one.
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
    "rocRAND: no hip::hip_fortran target, skipping the Fortran binding tests "
    "(they `use hip`). Point CMAKE_PREFIX_PATH at a ROCm whose HIP Fortran "
    "bindings were built with ${CMAKE_Fortran_COMPILER}.")
endif()

# librocrand itself, needed to link but not to compile. In-tree it arrives
# through the rocrand_fortran link interface; standalone it has to be found.
#
# rocrand-config.cmake does find_dependency(hip), and hip-config.cmake reaches
# FindThreads, which hard-errors unless C or CXX is enabled. The bindings are
# Fortran-only and standalone this project declares LANGUAGES NONE, so C is
# enabled here purely to satisfy that dependency: it compiles none of the
# sources below. Without it, a standalone test build stops at "FindThreads only
# works if either C or CXX language is enabled", which says nothing about what
# is actually wrong.
if(NOT TARGET roc::rocrand)
  if(NOT CMAKE_C_COMPILER_LOADED AND NOT CMAKE_CXX_COMPILER_LOADED)
    enable_language(C)
  endif()
  find_package(rocrand QUIET)
endif()
if(NOT TARGET roc::rocrand)
  message(STATUS
    "rocRAND: no roc::rocrand target, skipping the Fortran binding tests "
    "(they link librocrand). Point CMAKE_PREFIX_PATH at an installed ROCm to "
    "build them standalone.")
  return()
endif()

# Build one executable out of one or more sources and register it with CTest as
# rocrand_fortran_<name>, labelled gpu. Sources are named relative to this
# directory; with none given the source is taken to be <name>.f03, which is the
# shape every single-source test below uses. The FRUIT driver is a
# multi-source executable and passes its sources explicitly.
#
# One function rather than a second link path beside it: every test here needs
# the same three libraries, and two places to state that is two places to keep
# in step by hand.
function(rocrand_fortran_add_test name)
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
  add_executable(rocrand_fortran_test_${name} ${abs_srcs})
  set_target_properties(rocrand_fortran_test_${name} PROPERTIES
    LINKER_LANGUAGE Fortran
    # A multi-source test WRITES .mod files as it builds (the FRUIT driver
    # writes four), so each test gets a directory of its own to write them to
    # rather than sharing this one and racing over the names.
    Fortran_MODULE_DIRECTORY
      "${CMAKE_CURRENT_BINARY_DIR}/rocrand_fortran_test_${name}.mod.dir")
  target_link_libraries(rocrand_fortran_test_${name} PRIVATE
    rocrand_fortran hip::hip_fortran roc::rocrand)
  # Each .f03 test ends in a call to EXIT, a GNU extension in gfortran. Applied
  # to every target here rather than to those alone: the flag only widens what
  # gfortran accepts, so it costs the FRUIT sources (which do not call EXIT)
  # nothing, and one rule is one thing to keep right.
  target_compile_options(rocrand_fortran_test_${name} PRIVATE
    $<$<COMPILE_LANG_AND_ID:Fortran,GNU>:-std=gnu>)
  add_test(NAME rocrand_fortran_${name} COMMAND rocrand_fortran_test_${name})
  # Every one of these creates a generator and draws numbers on a device, so it
  # fails without a GPU, which is not the same thing as the binding being
  # broken. The label lets a build machine with no GPU still run the suite
  # meaningfully: ctest -LE gpu.
  set_tests_properties(rocrand_fortran_${name} PROPERTIES LABELS "gpu")
endfunction()

# One source per (generator, distribution) pair, named for the pair it drives.
# Every generator listed below is exercised against uniform and normal in both
# precisions; log-normal in both precisions and Poisson are driven through
# philox only, since those paths do not vary with the generator.
set(ROCRAND_FORTRAN_TESTS
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
    rocrand_version
    sobol32_normal
    sobol32_normal_double
    sobol32_uniform
    sobol32_uniform_double
    xorwow_normal
    xorwow_normal_double
    xorwow_uniform
    xorwow_uniform_double)

if(_have_hip_binding)
  foreach(_t IN LISTS ROCRAND_FORTRAN_TESTS)
    rocrand_fortran_add_test(${_t})
  endforeach()

  # The FRUIT suite: the vendored harness under fruit/ (third-party, carrying
  # its own LICENSE.txt, left unmodified), the helper that wraps a test case in
  # its setup and teardown, the test module and the program that drives them.
  # One executable and so one CTest test, because FRUIT reports its own
  # per-case results to stdout and to an XML file rather than through CTest.
  #
  # Listed in compile order for the reader; CMake scans Fortran module
  # dependencies itself and does not need it.
  rocrand_fortran_add_test(fruit
      fruit/fruit_util.f90
      fruit/fruit.f90
      fruit_helpers.f90
      test_rocrand.f90
      fruit_test_driver_rocrand.f90)
endif()
