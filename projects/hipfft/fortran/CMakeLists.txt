# Fortran bindings for hipFFT and hipFFTW.
#
# hipfft.F90 and hipfftw.F90 are GENERATED from the hipFFT and hipFFTW C headers
# by the rocm-fortran generator and committed here, so that a header change and
# its binding travel in the same pull request. Do not edit them by hand:
# regenerate them instead.
#
# Each is one self-contained Fortran module: `hipfft` the interfaces, enum
# constants and hipfftCheck helper, `hipfftw` the FFTW compatibility surface, the
# FFTW_* flags and the fftw_iodim/fftw_iodim64 derived types.
#
# TWO targets in ONE directory, because projects/hipfft/library compiles both
# libhipfft and libhipfftw from one source tree and ships both in the hipfft
# package. The modules stay independent all the same: hipfftw.F90 contains no
# `use hipfft`, so the archives have no compile-time relationship and no ordering
# constraint, and a program on the FFTW API links libhipfftw alone.
#
# The directory builds with no dependency on the hipFFT C targets, because a
# Fortran binding compiles against nothing; libhipfft/libhipfftw are needed only
# to LINK a final executable.

cmake_minimum_required(VERSION 3.19)

# Standalone, this directory is its own project, so the bindings can be built and
# packaged on their own.
#
# LANGUAGES NONE, not C: nothing here is written in C. Locating the installed
# hipFFT does need C or CXX -- find_package(hipfft) find_dependency(hip)s, and
# hip-config-amd.cmake reaches FindThreads, which hard-errors with neither
# enabled -- but that is a property of the lookup, so C is enabled at each lookup
# site instead. Enabling it here would make a C compiler a hard requirement of a
# build that compiles no C.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(hipfft-fortran LANGUAGES NONE)
endif()

# GUARDED, so a site with no Fortran compiler still builds the C libraries.
# "${UNIX}" rather than ON because ROCm ships no Fortran compiler on Windows at
# all (TheRock adds flang only in the non-WIN32 branch of
# compiler/pre_hook_amd-llvm.cmake), so the probe falls through to a stray
# gfortran and enable_language(Fortran) then fails to identify it.
#
# OFF on the CUDA backend, and that is about the GENERATED SOURCE, not the
# compiler: hipfft.F90 and hipfftw.F90 are produced from the ROCm code path and
# no CI job builds or runs them against a cuFFT-backed libhipfft, so defaulting
# ON there would publish an untested artefact. BUILD_WITH_LIB is hipFFT's own
# backend switch, which the root declares, normalises and forces to CUDA for a
# HIP-NVCC compiler, all before this directory is added. Standalone it is
# undefined and the default falls back to ${UNIX}: a standalone build of the
# bindings has no backend of its own.
#
# One option for both modules: the per-library overrides below still single
# either out, but the reason to default off is the same for both.
if(BUILD_WITH_LIB STREQUAL "CUDA")
  set(_hipfft_fortran_default OFF)
else()
  set(_hipfft_fortran_default "${UNIX}")
endif()
# The ONLY option(BUILD_FORTRAN_BINDINGS) in hipFFT, deliberately: the first
# option() call in a run fixes the cache entry, so a second one in the root with
# a different default would be silently ignored.
option(BUILD_FORTRAN_BINDINGS "Build the hipFFT and hipFFTW Fortran bindings"
       "${_hipfft_fortran_default}")
unset(_hipfft_fortran_default)

# The binding tests. Asking for them when the bindings were not built is a skip
# rather than an error, and ON is safe because the in-tree gate further down is a
# floor this cannot rise above.
option(BUILD_FORTRAN_CLIENTS "Build the hipFFT and hipFFTW Fortran clients and binding tests" ON)

# Both options are ONE cache entry shared by every library in the monorepo, so
# -DBUILD_FORTRAN_BINDINGS=OFF from the top turns the whole Fortran track off in
# one flag; each library also honours a name of its own, which overrides it.
#
# That override is the last word for the BINDINGS only. For the CLIENTS the gate
# further down is a floor an in-tree build cannot rise above, however
# HIPFFT_BUILD_FORTRAN_CLIENTS is set; the per-library name still picks the two
# test sets apart, and still decides on its own standalone.
#
# TWO libraries' bindings here, so two pairs are honoured: HIPFFT_* selects the
# hipfft target, HIPFFTW_* the hipfftw one. Either can be switched off without
# the other, since a site that wants only the FFTW surface should not have to
# build both.
set(_hipfft_bindings  ${BUILD_FORTRAN_BINDINGS})
set(_hipfftw_bindings ${BUILD_FORTRAN_BINDINGS})
set(_hipfft_tests     ${BUILD_FORTRAN_CLIENTS})
set(_hipfftw_tests    ${BUILD_FORTRAN_CLIENTS})
if(DEFINED HIPFFT_BUILD_FORTRAN_BINDINGS)
  set(_hipfft_bindings ${HIPFFT_BUILD_FORTRAN_BINDINGS})
endif()
if(DEFINED HIPFFTW_BUILD_FORTRAN_BINDINGS)
  set(_hipfftw_bindings ${HIPFFTW_BUILD_FORTRAN_BINDINGS})
endif()
if(DEFINED HIPFFT_BUILD_FORTRAN_CLIENTS)
  set(_hipfft_tests ${HIPFFT_BUILD_FORTRAN_CLIENTS})
endif()
if(DEFINED HIPFFTW_BUILD_FORTRAN_CLIENTS)
  set(_hipfftw_tests ${HIPFFTW_BUILD_FORTRAN_CLIENTS})
endif()

# Which array-argument overloads the modules expose. One tri-state rather than a
# boolean per tier, because the tiers are mutually exclusive: an assumed-rank
# dummy is not distinguishable by rank from the per-rank specifics, so
# assumed-rank REPLACES the per-rank variants rather than adding to them.
#
#   none           the raw type(c_ptr) surface only (Fortran 2003)
#   assumed-shape  + the per-rank array overloads (Fortran 2008): the default,
#                  and what the bindings shipped in ROCm are built with
#   assumed-rank   + one dimension(..) overload in their place (Fortran 2018)
#
# Either upper tier reaches only the subset of routines the generator emits
# overloads for; every other routine exposes the same plain type(c_ptr) interface
# at all three tiers.
#
# hipFFTW is the one binding where the three tiers currently coincide: the FFTW
# compatibility API takes bare pointers to already-allocated buffers, so the
# generator emitted no array overloads and no preprocessor guards in hipfftw.F90.
# The option is still applied to both targets, so that a regenerated, guarded
# hipfftw.F90 needs no CMake change.
set(FORTRAN_ARRAY_INTERFACES "assumed-shape" CACHE STRING
    "Array-argument overloads to expose: none, assumed-shape, or assumed-rank")
set_property(CACHE FORTRAN_ARRAY_INTERFACES PROPERTY STRINGS
             none assumed-shape assumed-rank)

# Report to the parent whether each binding was really built; ROCM_HAVE_FORTRAN
# says only that a compiler exists. Guarded because standalone this is the
# top-level scope, where PARENT_SCOPE warns under -Wdev.
if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(HIPFFT_HAVE_FORTRAN_BINDINGS  FALSE PARENT_SCOPE)
  set(HIPFFTW_HAVE_FORTRAN_BINDINGS FALSE PARENT_SCOPE)
endif()

if(NOT _hipfft_bindings AND NOT _hipfftw_bindings)
  return()
endif()

# Reached on the CUDA backend only when the default above was overridden by hand.
# Warn rather than fail: the bindings DO build and link there -- the public
# headers carry no CUDART_VERSION guard and library/src/nvidia_detail/ defines
# every symbol the modules reference -- so what is missing is evidence, not
# symbols, and an explicit request is honoured.
if(BUILD_WITH_LIB STREQUAL "CUDA")
  message(WARNING
    "hipFFT: the Fortran bindings were requested on the CUDA backend. "
    "hipfft.F90 and hipfftw.F90 are generated from the ROCm code path and are "
    "not tested against a cuFFT-backed libhipfft, which is why they are off by "
    "default there. Building them is supported; vouching for them is not.")
endif()

# The monorepo root probes for a Fortran compiler ONCE and publishes
# ROCM_HAVE_FORTRAN. The name is deliberately neither rocm-libraries' nor
# rocm-systems': a library from one monorepo is regularly built inside the other,
# and a repo-specific name would read a variable the other never sets.
# ROCM_LIBS_HAVE_FORTRAN is a legacy spelling, honoured until the rocm-libraries
# root adopts the new one. The local probe is the standalone path.
if(DEFINED ROCM_HAVE_FORTRAN)
  set(_have_fortran ${ROCM_HAVE_FORTRAN})
elseif(DEFINED ROCM_LIBS_HAVE_FORTRAN)
  set(_have_fortran ${ROCM_LIBS_HAVE_FORTRAN})
else()
  include(CheckLanguage)

  # check_language() writes the CACHE entry CMAKE_Fortran_COMPILER, which
  # rocm-cmake's ROCMChecks arms a variable_watch on; a legitimate compiler probe
  # is indistinguishable there from a toolchain violation, so it warns, and
  # hard-fails under -DROCM_ERROR_TOOLCHAIN_VAR=ON, which CI turns on. Silence it
  # across the probe and restore it, as rocRAND does in cmake/Dependencies.cmake.
  # Plain variables shadow the cache for this scope only, which is what the
  # callback reads. Both names must be cleared: the callback escalates on
  # ROCM_ERROR_TOOLCHAIN_VAR before it ever looks at the warn flag.
  set(_rocm_warn_toolchain_var "${ROCM_WARN_TOOLCHAIN_VAR}")
  set(_rocm_error_toolchain_var "${ROCM_ERROR_TOOLCHAIN_VAR}")
  set(ROCM_WARN_TOOLCHAIN_VAR OFF)
  set(ROCM_ERROR_TOOLCHAIN_VAR OFF)

  # Prefer ROCm's own amdflang. check_language(Fortran) takes the first compiler
  # on the search path, which on a ROCm CI image is the system gfortran, and that
  # is how the bindings ended up installed under fortran/gfortran/ while the
  # documentation promises fortran/amdflang/. amdflang sits beside the clang
  # already compiling this project, so look next to CMAKE_CXX_COMPILER; ROCM_PATH
  # covers a standalone configure; NO_DEFAULT_PATH so a stray amdflang on PATH
  # cannot win. An explicit CMAKE_Fortran_COMPILER, or FC, always wins: a .mod is
  # compiler-specific, so whoever named a compiler meant it.
  #
  # MUST run before check_language(), which is a no-op once the variable is set.
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
      # Confirm it runs: a ROCm tree can carry a dangling amdflang symlink, and
      # check_language() does nothing once the variable is set, so the failure
      # would surface far later as a broken enable_language.
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
  message(STATUS "hipFFT: no Fortran compiler found, skipping the Fortran bindings")
  return()
endif()

enable_language(Fortran)

# Here rather than beside the install() rules, because the install layout is also
# baked into each target's INSTALL_INTERFACE include directory, set well before
# them. Nothing must hardcode lib/ or include/: a distro installing to lib64 gets
# CMAKE_INSTALL_LIBDIR=lib64, and a literal lib/fortran then names nothing.
include(GNUInstallDirs)

if(NOT FORTRAN_ARRAY_INTERFACES MATCHES "^(none|assumed-shape|assumed-rank)$")
  message(FATAL_ERROR
    "FORTRAN_ARRAY_INTERFACES must be none, assumed-shape or assumed-rank, "
    "not '${FORTRAN_ARRAY_INTERFACES}'.")
endif()

# A .mod is not portable across compilers, so artifacts install under a
# per-compiler directory and two compilers can coexist in one prefix. The name is
# the compiler a user types (amdflang, gfortran), not CMake's compiler ID, so the
# -I/-L paths in the documentation match a raw link line. Derived from the ID
# rather than from the basename of CMAKE_Fortran_COMPILER, which would be wrong
# for an MPI wrapper (mpif90), a versioned binary (gfortran-13) or Cray's ftn.
#
# THIS MAPPING MUST MIRROR the one in both config templates exactly. The three
# cannot share code -- the configs run in a consumer's build long after this one
# is gone -- so they are kept in step by hand, and a drift makes the configs
# reject every consumer with a compiler mismatch that does not exist.
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
  # Unknown compiler: the lowercased basename still keeps two from colliding.
  get_filename_component(_fc_name "${CMAKE_Fortran_COMPILER}" NAME_WE)
  string(TOLOWER "${_fc_name}" _fc_name)
endif()

# Deliberately lossy: a .mod is tied to the compiler VERSION too, so gfortran 13
# and 14 share a directory and the second overwrites the first. That is the
# documented layout, and right for the common one-compiler prefix. A site that
# installs several versions of the SAME compiler overrides this, e.g. with
# gfortran-13.3.0.
set(HIPFFT_FORTRAN_COMPILER_DIR "${_fc_name}" CACHE STRING
    "Per-compiler subdirectory the .mod files and archives install under")

# hipFFTW installs into the same layout, so it gets no cache entry of its own:
# two overridable names for one directory could only disagree, scattering one
# compiler's .mod files across two directories. The mirror exists only so that
# hipfftw-fortran-config.cmake.in can substitute a name of its own.
set(HIPFFTW_FORTRAN_COMPILER_DIR "${HIPFFT_FORTRAN_COMPILER_DIR}")

# One module directory for both targets: the names are distinct and neither
# module `use`s the other, so there is no clash and no ordering constraint, and
# the single install(DIRECTORY) below is correct whichever halves were built.
set(_moddir "${CMAKE_CURRENT_BINARY_DIR}/include")

# Locate the C libraries ONCE for both halves: the installed
# hipfft-targets.cmake defines hip::hipfft and hip::hipfftw together. Testing for
# the targets without ever calling find_package would export no link interface at
# all, and every consumer would hit undefined references despite linking
# hip::hipfft_fortran. C is enabled first because hipfft-config.cmake
# find_dependency(hip)s and hip-config-amd.cmake reaches FindThreads, which
# hard-errors unless C or CXX is enabled -- QUIET does not suppress a
# FATAL_ERROR. QUIET all the same, because the bindings compile against nothing.
if(NOT TARGET hip::hipfft AND NOT TARGET hip::hipfftw)
  if(NOT CMAKE_C_COMPILER_LOADED AND NOT CMAKE_CXX_COMPILER_LOADED)
    enable_language(C)
  endif()
  find_package(hipfft QUIET)
endif()

if(NOT TARGET hip::hipfft AND NOT TARGET hip::hipfftw)
  message(STATUS
    "hipFFT: neither hip::hipfft nor hip::hipfftw was found, so the Fortran "
    "bindings carry no link interface; a consumer will have to link the C "
    "library (-lhipfft / -lhipfftw) itself. Point CMAKE_PREFIX_PATH at an "
    "installed ROCm to have it recorded.")
endif()

# --------------------------------------------------------------------------
# The hipFFT binding: module `hipfft`, binding libhipfft.
# --------------------------------------------------------------------------
if(_hipfft_bindings)
  add_library(hipfft_fortran STATIC hipfft.F90)
  # hip::, not roc::, because that is the namespace hipFFT already exports its C
  # targets under.
  add_library(hip::hipfft_fortran ALIAS hipfft_fortran)

  set_target_properties(hipfft_fortran PROPERTIES
    Fortran_MODULE_DIRECTORY "${_moddir}"
    # The array overloads sit behind preprocessor guards.
    Fortran_PREPROCESS ON
    # The .F90 default, but stating it lets CMake emit the flag the compiler in
    # use expects rather than relying on the extension.
    Fortran_FORMAT FREE
    POSITION_INDEPENDENT_CODE ON)

  # The tri-state as a macro; the option value and the macro name are the same
  # word, and none defines neither. Defining both is not an error -- hipfft.F90
  # tests USE_ASSUMED_RANK first in both guarded regions -- but at most one is
  # emitted here, so the tier in force is the one named rather than one that fell
  # out of a precedence rule inside the generated source.
  if(FORTRAN_ARRAY_INTERFACES STREQUAL "assumed-shape")
    target_compile_definitions(hipfft_fortran PRIVATE USE_ASSUMED_SHAPE=1)
  elseif(FORTRAN_ARRAY_INTERFACES STREQUAL "assumed-rank")
    target_compile_definitions(hipfft_fortran PRIVATE USE_ASSUMED_RANK=1)
  endif()

  # No line-length flag: every line of both generated modules fits within the
  # 132 columns the free-form standard guarantees.

  target_include_directories(hipfft_fortran PUBLIC
    "$<BUILD_INTERFACE:${_moddir}>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/fortran/${HIPFFT_FORTRAN_COMPILER_DIR}>")

  # Link interface only: needed to link an executable, not to build this archive.
  if(TARGET hip::hipfft)
    target_link_libraries(hipfft_fortran INTERFACE hip::hipfft)
    # Recorded so the config package knows whether it has to
    # find_dependency(hipfft) before including the targets file: a bindings-only
    # install made with no hipFFT in sight must not demand one at consume time.
    set(HIPFFT_FORTRAN_NEEDS_HIPFFT_PACKAGE TRUE)
  else()
    set(HIPFFT_FORTRAN_NEEDS_HIPFFT_PACKAGE FALSE)
  endif()

  if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(HIPFFT_HAVE_FORTRAN_BINDINGS TRUE PARENT_SCOPE)
  endif()
endif()

# --------------------------------------------------------------------------
# The hipFFTW binding: module `hipfftw`, binding libhipfftw.
# --------------------------------------------------------------------------
if(_hipfftw_bindings)
  add_library(hipfftw_fortran STATIC hipfftw.F90)
  # hip::, not roc::, matching hip::hipfftw itself.
  add_library(hip::hipfftw_fortran ALIAS hipfftw_fortran)

  set_target_properties(hipfftw_fortran PROPERTIES
    Fortran_MODULE_DIRECTORY "${_moddir}"
    # No guards in hipfftw.F90 today, but preprocessing costs nothing and keeps
    # the file regenerable into a guarded form without a CMake change.
    Fortran_PREPROCESS ON
    Fortran_FORMAT FREE
    POSITION_INDEPENDENT_CODE ON)

  # Defined even though hipfftw.F90 references neither macro today, so that a
  # regenerated, guarded source needs no change here. The two tiers are mutually
  # exclusive, exactly as for hipfft_fortran above.
  if(FORTRAN_ARRAY_INTERFACES STREQUAL "assumed-shape")
    target_compile_definitions(hipfftw_fortran PRIVATE USE_ASSUMED_SHAPE=1)
  elseif(FORTRAN_ARRAY_INTERFACES STREQUAL "assumed-rank")
    target_compile_definitions(hipfftw_fortran PRIVATE USE_ASSUMED_RANK=1)
  endif()

  target_include_directories(hipfftw_fortran PUBLIC
    "$<BUILD_INTERFACE:${_moddir}>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/fortran/${HIPFFTW_FORTRAN_COMPILER_DIR}>")

  # hip::hipfftw and never hip::hipfft: the FFTW compatibility surface is its own
  # shared library, and a program on it links libhipfftw alone.
  if(TARGET hip::hipfftw)
    target_link_libraries(hipfftw_fortran INTERFACE hip::hipfftw)
    set(HIPFFTW_FORTRAN_NEEDS_HIPFFT_PACKAGE TRUE)
  else()
    set(HIPFFTW_FORTRAN_NEEDS_HIPFFT_PACKAGE FALSE)
  endif()

  if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(HIPFFTW_HAVE_FORTRAN_BINDINGS TRUE PARENT_SCOPE)
  endif()
endif()

# The binding tests are GPU runtime executables, so in-tree they follow the
# library's own client/test switch; the condition below is the one hipFFT guards
# add_subdirectory( clients ) with, PLUS BUILD_CLIENTS itself -- that umbrella
# turns the other three on, but BELOW add_subdirectory( fortran ), so reading
# only the three would skip the tests of a -DBUILD_CLIENTS=ON build.
#
# The local _hipfft_tests/_hipfftw_tests are cleared rather than
# BUILD_FORTRAN_CLIENTS, whose cache entry is shared monorepo-wide. Placed where
# the flags are READ, not beside the options, so that a configure that never
# reaches here does not announce a skip of tests it was never going to build.
if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR
   AND (_hipfft_tests OR _hipfftw_tests)
   AND NOT (BUILD_CLIENTS OR BUILD_CLIENTS_BENCH OR BUILD_CLIENTS_SAMPLES
            OR BUILD_CLIENTS_TESTS))
  message(STATUS
    "hipFFT: clients are not being built, skipping the Fortran binding tests.")
  set(_hipfft_tests OFF)
  set(_hipfftw_tests OFF)
endif()

if(_hipfft_tests OR _hipfftw_tests)
  # Enough standalone, but NOT in-tree: CMake writes the chain of
  # CTestTestfile.cmake that ctest walks only when testing was enabled at the TOP
  # level, and hipFFT's own enable_testing() sits inside its BUILD_CLIENTS_*
  # guard, so a bindings-only build does not get it and `ctest` from the top
  # reports "Total Tests: 0". Hence the warning.
  enable_testing()
  if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    get_property(_top_testing GLOBAL PROPERTY CTEST_TARGETS_ADDED)
    if(NOT _top_testing)
      message(STATUS
        "hipFFT: the Fortran binding tests are registered, but ctest discovers "
        "tests only from a build root where testing was enabled at the top "
        "level. If `ctest` there reports no tests, add include(CTest) or "
        "enable_testing() to the top-level CMakeLists, or run ctest from "
        "${CMAKE_CURRENT_BINARY_DIR}/test.")
    endif()
  endif()
  add_subdirectory(test)
endif()

include(CMakePackageConfigHelpers)

# The version both Fortran packages are stamped with. ONE version for both:
# hipFFTW has none of its own, being built from the hipFFT source tree.
#
# Read from THIS tree's top-level CMakeLists, not from hipfft_VERSION: that
# variable is set by the find_package(hipfft) above and reports whatever hipFFT
# is INSTALLED, not the version the bindings were generated from. hipFFT does not
# spell the version as a literal -- VERSION_STRING is assembled from three
# components -- so the three are matched and joined here.
set(_fortran_version "")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../CMakeLists.txt")
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../CMakeLists.txt" _hipfft_root)
  set(_v_parts "")
  foreach(_v_comp MAJOR MINOR PATCH)
    # The whole match, not CMAKE_MATCH_1, decides whether this component was
    # found: a failed string(REGEX MATCH) leaves CMAKE_MATCH_1 holding whatever
    # the last SUCCESSFUL match put there, so reading it unguarded would reuse
    # the previous component's number.
    string(REGEX MATCH "set[ \t]*\\([ \t]*HIPFFT_VERSION_${_v_comp}[ \t]+\"([0-9]+)\""
           _v_whole "${_hipfft_root}")
    if(_v_whole)
      list(APPEND _v_parts "${CMAKE_MATCH_1}")
    endif()
  endforeach()
  list(LENGTH _v_parts _v_n)
  if(_v_n EQUAL 3)
    list(JOIN _v_parts "." _fortran_version)
  endif()
  unset(_hipfft_root)
endif()

if(NOT _fortran_version)
  # Better no version file than a wrong one: an unversioned find_package still
  # succeeds, whereas a wrong number makes a versioned one link the wrong .mod.
  message(STATUS
    "hipFFT: could not determine the version for the Fortran packages, so no "
    "hipfft-fortran-config-version.cmake or hipfftw-fortran-config-version.cmake "
    "is installed; find_package(hipfft-fortran <version>) and "
    "find_package(hipfftw-fortran <version>) will not match.")
endif()

# COMPONENT devel on every rule below: everything installed here is a development
# artifact, and devel is what rocm-cmake's install helpers default to -- without
# it these land in the runtime component, and the hipfft-devel package a Fortran
# consumer installs would not carry the bindings at all. Plain install(), not
# rocm_install(): this directory must also configure standalone, without
# rocm-cmake.
#
# One install(DIRECTORY) covers both modules: only the .mod files of the targets
# that were really compiled exist in _moddir.
install(DIRECTORY "${_moddir}/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/fortran/${HIPFFT_FORTRAN_COMPILER_DIR}"
        COMPONENT devel
        FILES_MATCHING PATTERN "*.mod")

# Kept out of the templates so neither side spells lib/ or include/ literally.
# HIPFFTW_* mirrors HIPFFT_* for the same reason HIPFFTW_FORTRAN_COMPILER_DIR
# does: both halves install into one layout, and the mirror exists only so that
# hipfftw-fortran-config.cmake.in can substitute names of its own.
set(HIPFFT_FORTRAN_LIBDIR      "${CMAKE_INSTALL_LIBDIR}/fortran")
set(HIPFFT_FORTRAN_INCLUDEDIR  "${CMAKE_INSTALL_INCLUDEDIR}/fortran")
set(HIPFFTW_FORTRAN_LIBDIR     "${HIPFFT_FORTRAN_LIBDIR}")
set(HIPFFTW_FORTRAN_INCLUDEDIR "${HIPFFT_FORTRAN_INCLUDEDIR}")

# Two export sets and two config packages, not one of each: the bindings are
# found independently, each with its own compiler check and hip*_FORTRAN_FOUND
# flag, and a consumer of the FFTW API must not be made to import a hipfft
# binding it never asked for. Both land in lib/cmake/hipfft -- hipFFTW has no
# config-package directory of its own -- which is also what lets
# hipfft-config.cmake pick either up with a bare include(<name> OPTIONAL).
if(_hipfft_bindings)
  install(TARGETS hipfft_fortran
          EXPORT hipfft-fortran-targets
          ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}/fortran/${HIPFFT_FORTRAN_COMPILER_DIR}"
                  COMPONENT devel)
  install(EXPORT hipfft-fortran-targets
          FILE hipfft-fortran-targets.cmake
          NAMESPACE hip::
          DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/hipfft"
          COMPONENT devel)

  # The generated source, so that a user on a compiler ROCm ships no .mod for
  # can rebuild the binding without fetching a monorepo, from source that cannot
  # drift from the installed libhipfft. Under share/, not include/: every entry
  # under include/fortran/ is read as naming a compiler -- the config enumerates
  # that directory to report which are installed, and would list hipfft.F90.
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/hipfft.F90"
          DESTINATION "${CMAKE_INSTALL_DATADIR}/hipfft/fortran"
          COMPONENT devel)

  configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/hipfft-fortran-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/hipfft-fortran-config.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/hipfft"
    # PATH_VARS: @PACKAGE_INIT@ rewrites each into an absolute path computed
    # from where the config was FOUND, so the package is relocatable and gets
    # lib64 right. HIPFFT_FORTRAN_COMPILER_DIR stays a plain @-substitution: it
    # is a bare directory NAME, and PATH_VARS would resolve it under the prefix.
    PATH_VARS HIPFFT_FORTRAN_LIBDIR HIPFFT_FORTRAN_INCLUDEDIR)
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/hipfft-fortran-config.cmake"
          DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/hipfft"
          COMPONENT devel)

  # find_package(hipfft-fortran), the documented spelling, searches
  # lib/cmake/hipfft-fortran*, which lib/cmake/hipfft does not match. A forwarder
  # gives that search somewhere to land without duplicating the real config and
  # its targets file.
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/forward/hipfft-fortran-config.cmake"
"# Generated forwarder. The real config lives beside the hipFFT C package, in
# lib/cmake/hipfft, so that hipfft-config.cmake can include it as a sibling.
# This file only exists so that find_package(hipfft-fortran) resolves to the
# same place.
include(\"\${CMAKE_CURRENT_LIST_DIR}/../hipfft/hipfft-fortran-config.cmake\")
")
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/forward/hipfft-fortran-config.cmake"
          DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/hipfft-fortran"
          COMPONENT devel)

  # SameMajorVersion because the module's bind(C) surface tracks the C API's
  # compatibility promise.
  if(_fortran_version)
    write_basic_package_version_file(
      "${CMAKE_CURRENT_BINARY_DIR}/hipfft-fortran-config-version.cmake"
      VERSION "${_fortran_version}"
      COMPATIBILITY SameMajorVersion)
    # BOTH directories: CMake looks for <pkg>-config-version.cmake NEXT TO the
    # config it actually resolved, and a versioned find_package(hipfft-fortran)
    # resolves the forwarder.
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/hipfft-fortran-config-version.cmake"
            DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/hipfft"
            COMPONENT devel)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/hipfft-fortran-config-version.cmake"
            DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/hipfft-fortran"
            COMPONENT devel)
  endif()
endif()

if(_hipfftw_bindings)
  install(TARGETS hipfftw_fortran
          EXPORT hipfftw-fortran-targets
          ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}/fortran/${HIPFFTW_FORTRAN_COMPILER_DIR}"
                  COMPONENT devel)
  install(EXPORT hipfftw-fortran-targets
          FILE hipfftw-fortran-targets.cmake
          NAMESPACE hip::
          DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/hipfft"
          COMPONENT devel)

  # The generated source, for the same reason as hipfft.F90 above, and under the
  # same share/hipfft/fortran: hipFFTW has no directory of its own anywhere else
  # in the layout.
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/hipfftw.F90"
          DESTINATION "${CMAKE_INSTALL_DATADIR}/hipfft/fortran"
          COMPONENT devel)

  configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/hipfftw-fortran-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/hipfftw-fortran-config.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/hipfft"
    # PATH_VARS, for the same reason as hipfft above.
    PATH_VARS HIPFFTW_FORTRAN_LIBDIR HIPFFTW_FORTRAN_INCLUDEDIR)
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/hipfftw-fortran-config.cmake"
          DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/hipfft"
          COMPONENT devel)

  # Same forwarder, for the same reason: lib/cmake/hipfft matches neither
  # hipfft-fortran* nor hipfftw-fortran*.
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/forward/hipfftw-fortran-config.cmake"
"# Generated forwarder. hipFFTW ships inside the hipFFT package, so the real
# config lives in lib/cmake/hipfft beside hipfft-config.cmake. This file only
# exists so that find_package(hipfftw-fortran) resolves to the same place.
include(\"\${CMAKE_CURRENT_LIST_DIR}/../hipfft/hipfftw-fortran-config.cmake\")
")
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/forward/hipfftw-fortran-config.cmake"
          DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/hipfftw-fortran"
          COMPONENT devel)

  # A version file of its own, carrying hipFFT's number, in both of ITS package
  # directories for the same reason as hipfft's above.
  if(_fortran_version)
    write_basic_package_version_file(
      "${CMAKE_CURRENT_BINARY_DIR}/hipfftw-fortran-config-version.cmake"
      VERSION "${_fortran_version}"
      COMPATIBILITY SameMajorVersion)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/hipfftw-fortran-config-version.cmake"
            DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/hipfft"
            COMPONENT devel)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/hipfftw-fortran-config-version.cmake"
            DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/hipfftw-fortran"
            COMPONENT devel)
  endif()
endif()
