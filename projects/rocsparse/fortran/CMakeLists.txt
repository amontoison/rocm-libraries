# Fortran bindings for rocSPARSE.
#
# rocsparse.F90 is GENERATED from the rocSPARSE C headers by the rocm-fortran
# generator and committed here, next to the headers it binds, so that a header
# change and its binding travel in the same pull request. Do not edit it by hand:
# regenerate it instead.
#
# The bindings are a single self-contained Fortran module, `rocsparse`, carrying
# the interfaces, the enum constants, the derived types, the opaque handle types
# and the rocsparseCheck status helper. A consumer writes one `use rocsparse`.
#
# This directory is self-contained: it builds with no dependency on the rocSPARSE
# C target, because a Fortran binding compiles against nothing (it only declares
# bind(C) interfaces). librocsparse is required to LINK a final executable, and is
# carried as the target's link interface for that reason.

cmake_minimum_required(VERSION 3.19)

# Configured directly (not as a subdirectory of the rocSPARSE build), this
# directory is its own project, so that the bindings can be built and packaged
# on their own. In-tree, rocSPARSE's own project() is already in effect.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(rocsparse-fortran LANGUAGES NONE)
endif()

# ON by default but GUARDED: a site with no Fortran compiler must still configure
# and build the C library. The monorepo root already probes once and publishes
# ROCM_LIBS_HAVE_FORTRAN; a standalone rocSPARSE build falls back to its own probe.
option(BUILD_FORTRAN_BINDINGS "Build the rocSPARSE Fortran bindings" ON)

if(NOT BUILD_FORTRAN_BINDINGS)
  return()
endif()

if(DEFINED ROCM_LIBS_HAVE_FORTRAN)
  set(_have_fortran ${ROCM_LIBS_HAVE_FORTRAN})
else()
  include(CheckLanguage)
  check_language(Fortran)
  if(CMAKE_Fortran_COMPILER)
    set(_have_fortran TRUE)
  else()
    set(_have_fortran FALSE)
  endif()
endif()

if(NOT _have_fortran)
  message(STATUS "rocSPARSE: no Fortran compiler found, skipping the Fortran bindings")
  return()
endif()

enable_language(Fortran)

add_library(rocsparse_fortran STATIC rocsparse.F90)
add_library(roc::rocsparse_fortran ALIAS rocsparse_fortran)

# A .mod is specific to the compiler AND to its version, so the artifacts are
# installed under a per-compiler directory rather than a shared one. Two compilers
# can then be installed side by side from the same package.
string(TOLOWER "${CMAKE_Fortran_COMPILER_ID}" _fc_id)
set(ROCSPARSE_FORTRAN_COMPILER_DIR "${_fc_id}-${CMAKE_Fortran_COMPILER_VERSION}")

set(_moddir "${CMAKE_CURRENT_BINARY_DIR}/include")
set_target_properties(rocsparse_fortran PROPERTIES
  Fortran_MODULE_DIRECTORY "${_moddir}"
  # rocsparse.F90 is uppercase: the array-ergonomics wrappers sit behind
  # USE_FPOINTER_INTERFACES, so the source must go through the C preprocessor.
  Fortran_PREPROCESS ON
  # Free form is the .F90 default, but stating it lets CMake emit whichever flag
  # the compiler in use expects rather than relying on the extension.
  Fortran_FORMAT FREE
  POSITION_INDEPENDENT_CODE ON)

# The convenience wrappers (assumed-rank arrays, c_loc on arrays) are F2008/F2018
# and are enabled here; without this define only the raw bind(C) surface is built.
target_compile_definitions(rocsparse_fortran PRIVATE USE_FPOINTER_INTERFACES=1)

# No line-length flag: the generator wraps the emitted source at 112 columns,
# inside the 132 that free-form Fortran guarantees, so every compiler reads it
# without a vendor-specific option.

target_include_directories(rocsparse_fortran PUBLIC
  "$<BUILD_INTERFACE:${_moddir}>"
  "$<INSTALL_INTERFACE:include/fortran/${ROCSPARSE_FORTRAN_COMPILER_DIR}>")

# Link interface only: needed to link an executable, not to build this archive.
if(TARGET roc::rocsparse)
  target_link_libraries(rocsparse_fortran INTERFACE roc::rocsparse)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS rocsparse_fortran
        EXPORT rocsparse-fortran-targets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}/fortran/${ROCSPARSE_FORTRAN_COMPILER_DIR}")
install(DIRECTORY "${_moddir}/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/fortran/${ROCSPARSE_FORTRAN_COMPILER_DIR}"
        FILES_MATCHING PATTERN "*.mod")
install(EXPORT rocsparse-fortran-targets
        FILE rocsparse-fortran-targets.cmake
        NAMESPACE roc::
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/rocsparse")

configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/rocsparse-fortran-config.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/rocsparse-fortran-config.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/rocsparse"
  PATH_VARS ROCSPARSE_FORTRAN_COMPILER_DIR)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/rocsparse-fortran-config.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/rocsparse")
