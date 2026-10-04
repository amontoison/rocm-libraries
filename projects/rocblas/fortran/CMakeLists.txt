# ########################################################################
# Copyright (C) 2026 Advanced Micro Devices, Inc. All rights reserved.
#
# SPDX-License-Identifier: MIT
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell cop-
# ies of the Software, and to permit persons to whom the Software is furnished
# to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IM-
# PLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
# FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
# COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
# IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNE-
# CTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
#
# ########################################################################

# Fortran bindings for rocBLAS.
#
# rocblas.F90 is GENERATED from the rocBLAS C headers by the rocm-fortran
# generator and committed next to the library it binds, so a header change and
# its binding travel in the same pull request. Do not edit it: regenerate it.
#
# It is one self-contained module, `rocblas`, carrying the bind(C) interfaces,
# the enum constants and the rocblasCheck status helper. Every opaque C object
# is a plain type(c_ptr): no derived types, no _typed, _dptr or _cstr overloads.
# Array overloads cover only the subset of routines the generator supports;
# every other routine is reached through type(c_ptr) under every tier.
#
# A Fortran binding compiles against nothing, so this directory needs no rocBLAS
# C target to build. librocblas is required to LINK an executable, and is
# carried as the target's link interface.
#
# This REPLACES library/include/rocblas_module.f90, removed in the same commit.
# That file defined two modules, rocblas and rocblas_enums; this one defines
# only rocblas, enums folded in, so `use rocblas_enums` becomes `use rocblas`.

cmake_minimum_required(VERSION 3.19)

# Standalone (not a subdirectory of the rocBLAS build) this is its own project,
# so the bindings can be built and packaged alone -- the supported path for a
# Fortran compiler other than the amdflang ROCm ships them built with.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(rocblas-fortran LANGUAGES NONE)
endif()

# Guarded, so a site with no Fortran compiler can still build the C library.
# "${UNIX}" rather than ON because ROCm ships no Fortran compiler on Windows at
# all: TheRock adds flang to LLVM_ENABLE_PROJECTS only in the non-WIN32 branch
# of compiler/pre_hook_amd-llvm.cmake, so the amdflang preference below finds
# nothing, the probe falls through to a stray gfortran, and
# enable_language(Fortran) fails to identify it. rocBLAS gates its hand-written
# Fortran clients the same way, in toolchain-windows.cmake.
option(BUILD_FORTRAN_BINDINGS "Build the rocBLAS Fortran bindings" "${UNIX}")

# One switch for the binding tests and a library's Fortran clients. ON by
# default, but guarded like the bindings: asking for them when the bindings were
# not built is a skip, not an error.
option(BUILD_FORTRAN_CLIENTS "Build the rocBLAS Fortran clients and binding tests" ON)

# BUILD_FORTRAN_BINDINGS and BUILD_FORTRAN_CLIENTS are ONE cache entry shared by
# every library in the monorepo, so -DBUILD_FORTRAN_BINDINGS=OFF from the top
# turns the whole Fortran track off. ROCBLAS_BUILD_FORTRAN_BINDINGS singles this
# library out: it decides this directory outright.
if(DEFINED ROCBLAS_BUILD_FORTRAN_BINDINGS)
  set(BUILD_FORTRAN_BINDINGS ${ROCBLAS_BUILD_FORTRAN_BINDINGS})
endif()

# Which array-argument overloads the module exposes. One tri-state rather than a
# boolean per tier, because the two upper tiers are alternatives, not additions:
# assumed-rank REPLACES the per-rank specifics (see the translation below).
# Array overloads exist only for the subset of routines the generator supports;
# every other routine keeps the plain type(c_ptr) interface under all three.
#
#   none           the raw type(c_ptr) surface only (Fortran 2003)
#   assumed-shape  + per-rank dimension(:) specifics (Fortran 2008): the default,
#                  and what the bindings shipped in ROCm are built with
#   assumed-rank   + dimension(..) specifics under the same generics (F2018)
set(FORTRAN_ARRAY_INTERFACES "assumed-shape" CACHE STRING
    "Array-argument overloads to expose: none, assumed-shape, or assumed-rank")
set_property(CACHE FORTRAN_ARRAY_INTERFACES PROPERTY STRINGS
             none assumed-shape assumed-rank)

# Reports whether the bindings were really BUILT, unlike ROCM_HAVE_FORTRAN,
# which says only that a compiler exists. Guarded because standalone this is the
# top-level scope, where PARENT_SCOPE warns under -Wdev and therefore fails
# under -Werror=dev, and standalone has to configure cleanly.
if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(ROCBLAS_HAVE_FORTRAN_BINDINGS FALSE PARENT_SCOPE)
endif()

if(NOT BUILD_FORTRAN_BINDINGS)
  return()
endif()

# The monorepo root probes for a Fortran compiler ONCE and publishes
# ROCM_HAVE_FORTRAN. That name is deliberately neither rocm-libraries' nor
# rocm-systems': rocBLAS is regularly built inside a rocm-systems tree, and a
# repo-specific name would read the wrong variable, or none. The rocm-libraries
# root still publishes ROCM_LIBS_HAVE_FORTRAN, honoured as a legacy spelling.
# The local probe is the standalone path, where there is no root to ask.
if(DEFINED ROCM_HAVE_FORTRAN)
  set(_have_fortran ${ROCM_HAVE_FORTRAN})
elseif(DEFINED ROCM_LIBS_HAVE_FORTRAN)
  set(_have_fortran ${ROCM_LIBS_HAVE_FORTRAN})
else()
  include(CheckLanguage)

  # check_language() writes the CACHE entry CMAKE_Fortran_COMPILER, which
  # rocm-cmake's ROCMChecks watches to catch a project setting a toolchain
  # variable behind the toolchain file's back; it cannot tell a legitimate probe
  # from a violation, and hard-fails under -DROCM_ERROR_TOOLCHAIN_VAR=ON, which
  # CI turns on. Silence it across the probe and restore it after. Plain
  # variables, not cache ones: they shadow the cache for this scope only, which
  # is what the callback reads. Both names must be cleared -- the callback
  # escalates on ROCM_ERROR_TOOLCHAIN_VAR before it looks at the warn flag.
  set(_rocm_warn_toolchain_var "${ROCM_WARN_TOOLCHAIN_VAR}")
  set(_rocm_error_toolchain_var "${ROCM_ERROR_TOOLCHAIN_VAR}")
  set(ROCM_WARN_TOOLCHAIN_VAR OFF)
  set(ROCM_ERROR_TOOLCHAIN_VAR OFF)

  # Prefer ROCm's own amdflang. check_language(Fortran) takes the first compiler
  # on the search path, which on a ROCm CI image is the system gfortran: that is
  # how the bindings ended up installed under fortran/gfortran/ while the
  # documentation promises fortran/amdflang/. Look beside the clang already
  # compiling this project, plus ROCM_PATH when standalone, NO_DEFAULT_PATH so a
  # stray amdflang cannot win. An explicit CMAKE_Fortran_COMPILER or FC always
  # wins: a .mod is compiler-specific, so whoever asked meant it. This MUST run
  # before check_language(), a no-op once CMAKE_Fortran_COMPILER is defined.
  if(NOT CMAKE_Fortran_COMPILER AND NOT DEFINED ENV{FC})
    set(_rocm_fc_hints)
    foreach(_rocm_fc_cc "${CMAKE_CXX_COMPILER}" "${CMAKE_C_COMPILER}"
                        "${CMAKE_HIP_COMPILER}")
      if(_rocm_fc_cc)
        get_filename_component(_rocm_fc_dir "${_rocm_fc_cc}" DIRECTORY)
        list(APPEND _rocm_fc_hints "${_rocm_fc_dir}")
      endif()
    endforeach()
    if(ROCM_PATH)
      list(APPEND _rocm_fc_hints "${ROCM_PATH}/lib/llvm/bin" "${ROCM_PATH}/bin")
    endif()
    if(_rocm_fc_hints)
      find_program(_rocm_amdflang NAMES amdflang HINTS ${_rocm_fc_hints}
                   NO_DEFAULT_PATH)
      # Confirm it runs before committing to it: a ROCm tree can carry a dangling
      # amdflang symlink, and check_language() does nothing once the variable is
      # set, so the failure would surface far later as a broken enable_language.
      if(_rocm_amdflang)
        execute_process(COMMAND "${_rocm_amdflang}" --version
                        RESULT_VARIABLE _rocm_fc_rc
                        OUTPUT_QUIET ERROR_QUIET)
        if(_rocm_fc_rc EQUAL 0)
          set(CMAKE_Fortran_COMPILER "${_rocm_amdflang}" CACHE FILEPATH
              "Fortran compiler used for the ROCm Fortran bindings" FORCE)
        endif()
        unset(_rocm_fc_rc)
      endif()
    endif()
    unset(_rocm_fc_hints)
    unset(_rocm_fc_cc)
    unset(_rocm_fc_dir)
  endif()

  check_language(Fortran)
  set(ROCM_WARN_TOOLCHAIN_VAR "${_rocm_warn_toolchain_var}")
  set(ROCM_ERROR_TOOLCHAIN_VAR "${_rocm_error_toolchain_var}")
  unset(_rocm_warn_toolchain_var)
  unset(_rocm_error_toolchain_var)

  if(CMAKE_Fortran_COMPILER)
    set(_have_fortran TRUE)
  else()
    set(_have_fortran FALSE)
  endif()
endif()

if(NOT _have_fortran)
  message(STATUS "rocBLAS: no Fortran compiler found, skipping the Fortran bindings")
  return()
endif()

enable_language(Fortran)

# Here rather than beside the install() rules, because the install layout is
# also baked into the target's INSTALL_INTERFACE include directory, set well
# before them. Nothing may hardcode lib/: a distro gets
# CMAKE_INSTALL_LIBDIR=lib64, and a literal lib/fortran then names nothing.
include(GNUInstallDirs)

if(NOT FORTRAN_ARRAY_INTERFACES MATCHES "^(none|assumed-shape|assumed-rank)$")
  message(FATAL_ERROR
    "FORTRAN_ARRAY_INTERFACES must be none, assumed-shape or assumed-rank, "
    "not '${FORTRAN_ARRAY_INTERFACES}'.")
endif()

add_library(rocblas_fortran STATIC rocblas.F90)

# roc::, the namespace rocBLAS exports its C library under, so a consumer is not
# made to remember two namespaces for two halves of one package.
add_library(roc::rocblas_fortran ALIAS rocblas_fortran)

# A .mod is not portable across compilers, so the artifacts install under a
# per-compiler directory and two compilers can live side by side in one prefix.
# Named after the compiler a user actually types (amdflang, gfortran), not after
# CMake's ID, so the -I/-L paths in the documentation work on a raw link line.
# Derived from the ID rather than the basename of CMAKE_Fortran_COMPILER, which
# would be wrong for an MPI wrapper (mpif90), a versioned binary (gfortran-13),
# an absolute path or the Cray wrapper (ftn).
if(CMAKE_Fortran_COMPILER_ID STREQUAL "GNU")
  set(_fc_name "gfortran")
elseif(CMAKE_Fortran_COMPILER_ID MATCHES "Flang")     # Flang, LLVMFlang
  set(_fc_name "amdflang")
elseif(CMAKE_Fortran_COMPILER_ID STREQUAL "Cray")
  set(_fc_name "ftn")
elseif(CMAKE_Fortran_COMPILER_ID STREQUAL "IntelLLVM")
  set(_fc_name "ifx")
elseif(CMAKE_Fortran_COMPILER_ID STREQUAL "Intel")
  set(_fc_name "ifort")
elseif(CMAKE_Fortran_COMPILER_ID STREQUAL "NVHPC")
  set(_fc_name "nvfortran")
else()
  # Unknown compiler: the lowercased basename is the best guess available.
  get_filename_component(_fc_name "${CMAKE_Fortran_COMPILER}" NAME_WE)
  string(TOLOWER "${_fc_name}" _fc_name)
endif()

# Deliberately lossy: a .mod is tied to the compiler VERSION too, so gfortran 13
# and 14 install to the same directory and the second overwrites the first. That
# is the documented layout (include/fortran/gfortran) and the right default for
# the usual one-compiler-per-prefix case; a site installing several versions of
# the SAME compiler sets ROCBLAS_FORTRAN_COMPILER_DIR itself, e.g. gfortran-13.
set(ROCBLAS_FORTRAN_COMPILER_DIR "${_fc_name}" CACHE STRING
    "Per-compiler subdirectory the .mod and the archive install under")

set(_moddir "${CMAKE_CURRENT_BINARY_DIR}/include")
set_target_properties(rocblas_fortran PROPERTIES
  Fortran_MODULE_DIRECTORY "${_moddir}"
  # Uppercase .F90: the array overloads sit behind preprocessor guards.
  Fortran_PREPROCESS ON
  # Stated explicitly so CMake emits the compiler's own flag for it rather than
  # relying on the extension.
  Fortran_FORMAT FREE
  POSITION_INDEPENDENT_CODE ON)

# Translate the tri-state into the macro the generated source is guarded with.
# Exactly one macro is defined: the tiers are mutually exclusive, and none
# defines neither, leaving the raw type(c_ptr) surface the generics fall back to.
# Compiling rocblas.F90 by hand with both set is harmless but yields
# assumed-rank -- both regions of the source test USE_ASSUMED_RANK ahead of
# USE_ASSUMED_SHAPE (checked clean with amdflang -cpp -fsyntax-only).
if(FORTRAN_ARRAY_INTERFACES STREQUAL "assumed-shape")
  target_compile_definitions(rocblas_fortran PRIVATE USE_ASSUMED_SHAPE=1)
elseif(FORTRAN_ARRAY_INTERFACES STREQUAL "assumed-rank")
  target_compile_definitions(rocblas_fortran PRIVATE USE_ASSUMED_RANK=1)
endif()

# No line-length flag: every line of the emitted source fits within the 132
# columns free-form Fortran guarantees, so every compiler reads it unaided.

target_include_directories(rocblas_fortran PUBLIC
  "$<BUILD_INTERFACE:${_moddir}>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/fortran/${ROCBLAS_FORTRAN_COMPILER_DIR}>")

# Link interface only: roc::rocblas is needed to link an executable, not to
# build this archive. Standalone it has to be looked for -- testing for the
# target without ever calling find_package would mean it is never found, the
# archive would export no link interface at all, and every consumer would hit
# undefined references despite linking roc::rocblas_fortran as documented.
#
# rocblas-config.cmake does find_dependency(hip), whose hip-config.cmake reaches
# FindThreads and hard-errors unless C or CXX is enabled, so C is enabled here
# (it compiles nothing). QUIET: the bindings build with no rocBLAS installed.
if(NOT TARGET roc::rocblas)
  if(NOT CMAKE_C_COMPILER_LOADED AND NOT CMAKE_CXX_COMPILER_LOADED)
    enable_language(C)
  endif()
  find_package(rocblas QUIET)
endif()

# Record which of the two happened: the template's find_dependency(rocblas) is
# guarded on ROCBLAS_FORTRAN_NEEDS_C_PACKAGE. Spelled TRUE/FALSE and never left
# empty -- the guard is @-substituted into the installed config, and an empty
# value would install a bare if() that fails to parse.
if(TARGET roc::rocblas)
  target_link_libraries(rocblas_fortran INTERFACE roc::rocblas)
  set(ROCBLAS_FORTRAN_NEEDS_C_PACKAGE TRUE)
else()
  message(STATUS
    "rocBLAS: the C library was not found, so the Fortran bindings carry no "
    "link interface; a consumer will have to link it (-lrocblas) itself.")
  set(ROCBLAS_FORTRAN_NEEDS_C_PACKAGE FALSE)
endif()

if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(ROCBLAS_HAVE_FORTRAN_BINDINGS TRUE PARENT_SCOPE)
endif()

# No fortran/test/ here: rocBLAS already drives this binding from clients/,
# where rocblas_fortran.f90 `use rocblas`s the generated module for the gtest
# suite. That is broader coverage than the hipFORT-derived tests would add, and
# already in CI. The nine libraries with no such path keep theirs.

include(CMakePackageConfigHelpers)

# Beside rocblas-config.cmake, so one CMAKE_PREFIX_PATH entry finds both halves.
# rocRAND and hipRAND cannot do this and install to lib/cmake/<lib>-fortran
# alone: their shipped <lib>-config.cmake carries a bare unguarded include() of a
# sibling <lib>-fortran-config.cmake, so a file here would recurse.
# rocblas-config.cmake mentions Fortran nowhere, checked against an installed
# ROCm, so it uses the shared layout, like hipBLAS and the FFT libraries.
set(_fortran_pkgdir "${CMAKE_INSTALL_LIBDIR}/cmake/rocblas")

# COMPONENT devel on every rule below: an archive, .mod files, the generated
# source and the CMake package are all development artifacts. It is also what
# rocm-cmake defaults to, and what the rocblas_module.f90 these replace shipped
# as, so omitting it would be a packaging regression. Plain install(), not
# rocm_install(): standalone, rocm-cmake was never loaded.
install(TARGETS rocblas_fortran
        EXPORT rocblas-fortran-targets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}/fortran/${ROCBLAS_FORTRAN_COMPILER_DIR}"
                COMPONENT devel)
install(DIRECTORY "${_moddir}/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/fortran/${ROCBLAS_FORTRAN_COMPILER_DIR}"
        COMPONENT devel
        FILES_MATCHING PATTERN "*.mod")

# The generated source, so a user on a compiler ROCm ships no .mod for can
# rebuild the binding, from source that cannot drift from the installed
# librocblas. Under share/, not include/: it is not compiler-specific, and the
# config below enumerates include/fortran/ to report which compilers are
# installed, so a source file among them would be listed as one.
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/rocblas.F90"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/rocblas/fortran"
        COMPONENT devel)
install(EXPORT rocblas-fortran-targets
        FILE rocblas-fortran-targets.cmake
        NAMESPACE roc::
        DESTINATION "${_fortran_pkgdir}"
        COMPONENT devel)

# Kept out of the template so that neither side spells lib/ or include/
# literally: a literal lib/fortran is wrong the moment CMAKE_INSTALL_LIBDIR is
# lib64, and the config would hand out an IMPORTED_LOCATION that does not exist.
set(ROCBLAS_FORTRAN_LIBDIR     "${CMAKE_INSTALL_LIBDIR}/fortran")
set(ROCBLAS_FORTRAN_INCLUDEDIR "${CMAKE_INSTALL_INCLUDEDIR}/fortran")

configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/rocblas-fortran-config.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/rocblas-fortran-config.cmake"
  INSTALL_DESTINATION "${_fortran_pkgdir}"
  # PATH_VARS: @PACKAGE_INIT@ rewrites each into an absolute path computed from
  # where the config was FOUND, so the package survives relocation and gets
  # lib64 right. ROCBLAS_FORTRAN_COMPILER_DIR stays a plain @-substitution: it
  # is a bare directory NAME, and PATH_VARS would resolve it under the prefix.
  PATH_VARS ROCBLAS_FORTRAN_LIBDIR ROCBLAS_FORTRAN_INCLUDEDIR)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/rocblas-fortran-config.cmake"
        DESTINATION "${_fortran_pkgdir}"
        COMPONENT devel)

# The real config sits in lib/cmake/rocblas so that rocblas-config.cmake could
# pick it up with a bare include(... OPTIONAL) once rocm_export_targets grows a
# hook for it. Its existing INCLUDE argument is NOT that hook: it emits a
# non-OPTIONAL include(), BEFORE the targets file, so roc::rocblas would not
# exist yet. But find_package(rocblas-fortran) searches
# lib/cmake/rocblas-fortran*, which that does not match, so the documented
# spelling needs somewhere to land. A forwarder rather than a second copy of the
# config, which would need its own targets file and drift from this one.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/forward/rocblas-fortran-config.cmake"
"# Generated forwarder. The real config lives beside the rocBLAS C package, in
# lib/cmake/rocblas, so that rocblas-config.cmake can include it as a sibling.
# This file only exists so that find_package(rocblas-fortran) resolves to the
# same place.
include(\"\${CMAKE_CURRENT_LIST_DIR}/../rocblas/rocblas-fortran-config.cmake\")
")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/forward/rocblas-fortran-config.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/rocblas-fortran"
        COMPONENT devel)

# A version file, so that find_package(rocblas-fortran 5.1) resolves rather than
# failing on a missing one. SameMajorVersion, because the module's bind(C)
# surface tracks the C API's compatibility promise. Read from THIS tree's
# top-level CMakeLists, not from rocblas_VERSION: in a standalone build that
# variable is set by the find_package(rocblas) above and reports whatever
# rocBLAS is INSTALLED, not the headers these bindings were generated from.
set(_fortran_version "")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../CMakeLists.txt")
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../CMakeLists.txt" _rocblas_root)
  # rocBLAS spells it set ( VERSION_STRING "5.8.0" ), note the spaces, and passes
  # the variable to rocm_setup_version, so match the assignment, not the call.
  string(REGEX MATCH "set[ \t]*\\([ \t]*VERSION_STRING[ \t]+\"([0-9]+\\.[0-9]+\\.[0-9]+)\""
         _m "${_rocblas_root}")
  if(CMAKE_MATCH_1)
    set(_fortran_version "${CMAKE_MATCH_1}")
  endif()
  unset(_rocblas_root)
endif()

if(_fortran_version)
  write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/rocblas-fortran-config-version.cmake"
    VERSION "${_fortran_version}"
    COMPATIBILITY SameMajorVersion)
  # In BOTH package directories, necessarily: CMake looks for
  # <pkg>-config-version.cmake NEXT TO the <pkg>-config.cmake it actually
  # resolved. find_package(rocblas-fortran 5.8) resolves the forwarder, so a
  # version file only beside the real config is never consulted and the
  # versioned call fails even though the version matches.
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/rocblas-fortran-config-version.cmake"
          DESTINATION "${_fortran_pkgdir}"
          COMPONENT devel)
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/rocblas-fortran-config-version.cmake"
          DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/rocblas-fortran"
          COMPONENT devel)
else()
  # Better no version file than a wrong one: without it an unversioned
  # find_package still succeeds, whereas a wrong number makes a versioned one
  # succeed and link the wrong .mod.
  message(STATUS
    "rocBLAS: could not determine the version for the Fortran package, so no "
    "rocblas-fortran-config-version.cmake is installed; "
    "find_package(rocblas-fortran <version>) will not match.")
endif()
