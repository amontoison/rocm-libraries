# Tests for the rocRAND Fortran bindings: one executable per test, driven by
# CTest. All but the version query create a generator and draw numbers on a
# device, so they build anywhere but only pass on a machine with a GPU.
#
# Two suites: the .f03 sources, mostly one test per (generator, distribution)
# pair, and the FRUIT-based suite (fruit/ plus the .f90 sources beside it), which
# moved here with the binding it now exercises. Reached only from the parent,
# under BUILD_FORTRAN_CLIENTS.

# The Intel compilers do not recognize the .f03 source extension and skip such
# files ("no action performed"). Force Fortran compilation with -Tf immediately
# before the source; the LANGUAGE source property alone is not enough.
if(CMAKE_Fortran_COMPILER_ID MATCHES "Intel")
  string(REPLACE "<SOURCE>" "-Tf <SOURCE>"
         CMAKE_Fortran_COMPILE_OBJECT "${CMAKE_Fortran_COMPILE_OBJECT}")
endif()

# Unlike the binding itself, the RUNTIME tests are not self-contained: they
# allocate and copy device memory, so each says `use hip` as well as
# `use rocrand`. That makes the HIP Fortran binding a COMPILE-time dependency --
# its .mod must exist, where librocrand is only a link-time one. Standalone it
# has to be found in an installed ROCm, built with this same compiler.
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
# rocrand-config.cmake does find_dependency(hip), and hip-config.cmake reaches
# FindThreads, which hard-errors unless C or CXX is enabled, so C is enabled
# here and compiles none of the sources below.
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
# rocrand_fortran_<name>, labelled gpu. Sources are relative to this directory;
# with none given the source is <name>.f03, which every single-source test below
# relies on. One function and not a second link path: every test needs the same
# three libraries.
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
    # writes four), so each test needs a directory of its own rather than
    # sharing this one and racing over the names.
    Fortran_MODULE_DIRECTORY
      "${CMAKE_CURRENT_BINARY_DIR}/rocrand_fortran_test_${name}.mod.dir")
  target_link_libraries(rocrand_fortran_test_${name} PRIVATE
    rocrand_fortran hip::hip_fortran roc::rocrand)
  # Each .f03 test ends in a call to EXIT, a GNU extension. Applied to every
  # target here rather than to those alone: the flag only widens what gfortran
  # accepts, so it costs the FRUIT sources nothing.
  target_compile_options(rocrand_fortran_test_${name} PRIVATE
    $<$<COMPILE_LANG_AND_ID:Fortran,GNU>:-std=gnu>)
  add_test(NAME rocrand_fortran_${name} COMMAND rocrand_fortran_test_${name})
  # Almost all of these draw numbers on a device, so without a GPU they fail for
  # a reason that is not the binding being broken. The label lets a build
  # machine with no GPU still run the suite meaningfully: ctest -LE gpu.
  set_tests_properties(rocrand_fortran_${name} PROPERTIES LABELS "gpu")
endfunction()

# One source per (generator, distribution) pair, named for the pair. Every
# generator below is exercised against uniform and normal in both precisions;
# log-normal and Poisson go through philox only, as those paths do not vary
# with the generator. rocrand_version is the exception: it binds no generator
# and covers the version query alone.
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
  # its own LICENSE.txt, left unmodified), the setup/teardown helper, the test
  # module and its driver. ONE executable and so one CTest test, because FRUIT
  # reports its own per-case results to stdout and to an XML file rather than
  # through CTest. Listed in compile order for the reader only.
  rocrand_fortran_add_test(fruit
      fruit/fruit_util.f90
      fruit/fruit.f90
      fruit_helpers.f90
      test_rocrand.f90
      fruit_test_driver_rocrand.f90)
endif()
