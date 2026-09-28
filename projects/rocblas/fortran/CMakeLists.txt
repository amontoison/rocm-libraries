# Fortran bindings for rocBLAS.
#
# rocblas.F90 is GENERATED from the rocBLAS C headers by the rocm-fortran
# generator and committed here, next to the library it binds, so that a header
# change and its binding travel in the same pull request. Do not edit it by
# hand: regenerate it instead.
#
# The bindings are a single self-contained Fortran module, `rocblas`, carrying
# the interfaces, the enum constants, the derived types, the opaque handle types
# and the rocblasCheck status helper. A consumer writes one `use rocblas`.
#
# This directory is self-contained: it builds with no dependency on the rocBLAS
# C target, because a Fortran binding compiles against nothing (it only declares
# bind(C) interfaces). librocblas is required to LINK a final executable, and is
# carried as the target's link interface for that reason.
#
# This module REPLACES the hand-written binding that used to live in
# library/include/rocblas_module.f90, removed in the same commit that added this
# directory. That file defined TWO modules, rocblas and rocblas_enums; this one
# defines only rocblas, with the enum constants folded in, so a consumer that
# said `use rocblas_enums` now says `use rocblas`. The clients' Fortran shims and
# samples compile against this module and link the target exported here.

cmake_minimum_required(VERSION 3.19)

# Configured directly (not as a subdirectory of the rocBLAS build), this
# directory is its own project, so that the bindings can be built and packaged
# on their own, which is the supported path for anyone whose Fortran compiler
# is not the amdflang the shipped bindings were built with. In-tree, rocBLAS's
# own project() is already in effect.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(rocblas-fortran LANGUAGES NONE)
endif()

# ON by default but GUARDED: a site with no Fortran compiler must still
# configure and build the C library. The monorepo root already probes once and
# publishes ROCM_HAVE_FORTRAN; a standalone rocBLAS build falls back to its own
# probe.
option(BUILD_FORTRAN_BINDINGS "Build the rocBLAS Fortran bindings" ON)

# The bindings' own test suite, and the Fortran clients where a library has
# them: one switch rather than two neighbouring ones. ON by default, and
# guarded the same way as the bindings, so asking for them when the bindings
# were not built is a skip rather than an error.
option(BUILD_FORTRAN_CLIENTS "Build the rocBLAS Fortran clients and binding tests" ON)

# BUILD_FORTRAN_BINDINGS and BUILD_FORTRAN_CLIENTS are ONE cache entry shared by
# every library in the monorepo, which is what you want for the common case:
# -DBUILD_FORTRAN_BINDINGS=OFF from the top turns the whole Fortran track off in
# a single flag. It is not what you want when one library has to be singled out,
# so each library also honours a name of its own, which wins when it is set.
if(DEFINED ROCBLAS_BUILD_FORTRAN_BINDINGS)
  set(BUILD_FORTRAN_BINDINGS ${ROCBLAS_BUILD_FORTRAN_BINDINGS})
endif()
if(DEFINED ROCBLAS_BUILD_FORTRAN_CLIENTS)
  set(BUILD_FORTRAN_CLIENTS ${ROCBLAS_BUILD_FORTRAN_CLIENTS})
endif()

# Which array-argument overloads the module exposes. One tri-state replaces the
# two booleans hipFORT used (USE_FPOINTER_INTERFACES / ASSUMED_RANK), because the
# two upper tiers cannot legally coexist: an assumed-rank dummy is not
# distinguishable by rank from the per-rank specifics, so assumed-rank REPLACES
# the per-rank variants rather than adding to them.
#
#   none           the raw type(c_ptr) surface only (Fortran 2003)
#   assumed-shape  + the per-rank array overloads (Fortran 2008): the default,
#                  and what the bindings shipped in ROCm are built with
#   assumed-rank   + the single dimension(..) overload per routine (Fortran 2018)
set(FORTRAN_ARRAY_INTERFACES "assumed-shape" CACHE STRING
    "Array-argument overloads to expose: none, assumed-shape, or assumed-rank")
set_property(CACHE FORTRAN_ARRAY_INTERFACES PROPERTY STRINGS
             none assumed-shape assumed-rank)

# Report to the parent scope whether the bindings were really built. Distinct
# from ROCM_HAVE_FORTRAN, which reports only that a compiler exists: the
# compiler can be present while this directory is switched off.
#
# Only when there IS a parent: standalone, this directory is the top-level scope
# and PARENT_SCOPE warns under -Wdev rather than being a silent no-op, which
# turns into a hard failure under -Werror=dev. Standalone is the documented path
# for anyone whose Fortran compiler is not the one ROCm ships the bindings for,
# so it has to configure cleanly.
if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(ROCBLAS_HAVE_FORTRAN_BINDINGS FALSE PARENT_SCOPE)
endif()

if(NOT BUILD_FORTRAN_BINDINGS)
  return()
endif()

# Probing for a Fortran compiler is not free, and every library in a monorepo
# build would otherwise repeat it, so the monorepo root probes ONCE and publishes
# the answer as ROCM_HAVE_FORTRAN. That name is deliberately neither
# rocm-libraries' nor rocm-systems': rocBLAS is regularly built inside a
# rocm-systems tree (and HIP inside this one), and a repo-specific name would
# make it read the wrong variable, or none. ROCM_LIBS_HAVE_FORTRAN is what the
# rocm-libraries root publishes today and is honoured as a legacy spelling until
# that root adopts the new name.
#
# The local probe is the standalone path, where there is no root to publish
# anything, and it is also the honest fallback: a directory that can be
# configured on its own has to be able to answer this question on its own.
if(DEFINED ROCM_HAVE_FORTRAN)
  set(_have_fortran ${ROCM_HAVE_FORTRAN})
elseif(DEFINED ROCM_LIBS_HAVE_FORTRAN)
  set(_have_fortran ${ROCM_LIBS_HAVE_FORTRAN})
else()
  include(CheckLanguage)

  # check_language() answers by writing the CACHE entry CMAKE_Fortran_COMPILER,
  # and rocm-cmake's ROCMChecks arms a variable_watch on exactly that name to
  # catch a project setting a toolchain variable behind the toolchain file's
  # back. A compiler probe is the one legitimate writer that watch cannot tell
  # apart from a violation, so it reports this one: a stray twelve-line banner
  # by default, and a hard configure failure under -DROCM_ERROR_TOOLCHAIN_VAR=ON
  # (or ROCMCHECKS_ERROR_TOOLCHAIN_VAR in the environment), which is a switch CI
  # turns on. develop has no probe here and so never tripped it, which makes
  # this directory the regression.
  #
  # So silence the checker across the probe and put it back afterwards, the same
  # save/set/restore rocRAND already uses in cmake/Dependencies.cmake. Plain
  # variables rather than cache ones: they shadow the cache for this scope only,
  # which is what the watch callback reads, and the user's cache entries are
  # left exactly as they were. Both names have to be cleared, because the
  # callback escalates to SEND_ERROR on ROCM_ERROR_TOOLCHAIN_VAR before it ever
  # looks at the warn flag.
  set(_rocm_warn_toolchain_var "${ROCM_WARN_TOOLCHAIN_VAR}")
  set(_rocm_error_toolchain_var "${ROCM_ERROR_TOOLCHAIN_VAR}")
  set(ROCM_WARN_TOOLCHAIN_VAR OFF)
  set(ROCM_ERROR_TOOLCHAIN_VAR OFF)

  # Prefer ROCm's own amdflang over whatever CMake would otherwise find first.
  # check_language(Fortran) takes the first compiler on the search path, which on
  # a ROCm CI image is the system gfortran, and that is how the bindings ended up
  # as the one component of ROCm built by a different toolchain from everything
  # else: installed under fortran/gfortran/ while the documentation promises
  # fortran/amdflang/, and exercising a compiler the product does not ship with.
  #
  # amdflang sits beside the clang already compiling this project, so the answer
  # is next to CMAKE_CXX_COMPILER when there is one; ROCM_PATH covers a
  # standalone configure. Searched with NO_DEFAULT_PATH so a stray amdflang
  # elsewhere on PATH cannot win over the ROCm being built against.
  #
  # An explicit CMAKE_Fortran_COMPILER, or FC in the environment, always wins:
  # building these bindings with gfortran is a supported thing to do, and a .mod
  # is compiler-specific, so whoever asked for a compiler meant it.
  #
  # check_language() is a no-op once CMAKE_Fortran_COMPILER is defined, so this
  # has to run before it, and the probe below never overrides what we set here.
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
      # Confirm it runs before committing to it. A ROCm tree can carry a dangling
      # amdflang symlink, and check_language() would not catch that: it does
      # nothing at all once the variable is set, so the failure would surface far
      # later as a broken enable_language with no hint of where the choice was
      # made.
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

if(NOT FORTRAN_ARRAY_INTERFACES MATCHES "^(none|assumed-shape|assumed-rank)$")
  message(FATAL_ERROR
    "FORTRAN_ARRAY_INTERFACES must be none, assumed-shape or assumed-rank, "
    "not '${FORTRAN_ARRAY_INTERFACES}'.")
endif()

add_library(rocblas_fortran STATIC rocblas.F90)

# roc::, matching the namespace rocBLAS exports its C library under (see
# library/CMakeLists.txt), so that a consumer is not made to remember that two
# halves of one package sit in different namespaces.
add_library(roc::rocblas_fortran ALIAS rocblas_fortran)

# A .mod is not portable across compilers, so the artifacts are installed under a
# per-compiler directory rather than a shared one. Two compilers can then be
# installed side by side from the same package.
#
# The directory is named after the compiler a user actually types (amdflang,
# gfortran), not after CMake's compiler ID, so that the -I/-L paths in the
# documentation are the ones a raw (non-CMake) link line needs. Deriving it from
# the ID and mapping back, rather than taking the basename of
# CMAKE_Fortran_COMPILER, is what makes that survive the cases where the two
# differ: an MPI wrapper (mpif90 around gfortran), a versioned binary
# (gfortran-13), an absolute path, or the Cray wrapper (ftn). All four would give
# the wrong directory under a plain basename, and the .mod would then be filed
# under a name no consumer looks up.
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
  # Unknown compiler: fall back to the basename, lowercased, which is the best
  # guess available and still keeps two compilers from colliding.
  get_filename_component(_fc_name "${CMAKE_Fortran_COMPILER}" NAME_WE)
  string(TOLOWER "${_fc_name}" _fc_name)
endif()

# Strictly a .mod is tied to the compiler VERSION too, not just the compiler, so
# this name is deliberately lossy: gfortran 13 and gfortran 14 install to the
# same directory and the second overwrites the first. That is the documented
# layout (include/fortran/gfortran), and it is the right default because the
# overwhelmingly common case is one Fortran compiler per prefix. A site that
# really does install several versions of the SAME compiler side by side sets
# ROCBLAS_FORTRAN_COMPILER_DIR itself, for example to gfortran-13.3.0.
set(ROCBLAS_FORTRAN_COMPILER_DIR "${_fc_name}" CACHE STRING
    "Per-compiler subdirectory the .mod and the archive install under")

set(_moddir "${CMAKE_CURRENT_BINARY_DIR}/include")
set_target_properties(rocblas_fortran PROPERTIES
  Fortran_MODULE_DIRECTORY "${_moddir}"
  # rocblas.F90 is uppercase: the array overloads sit behind preprocessor
  # guards, so the source must go through the C preprocessor.
  Fortran_PREPROCESS ON
  # Free form is the .F90 default, but stating it lets CMake emit whichever flag
  # the compiler in use expects rather than relying on the extension.
  Fortran_FORMAT FREE
  POSITION_INDEPENDENT_CODE ON)

# Translate the tri-state into the two macros the generated source is guarded
# with. assumed-rank implies the F2008 tier: in the source the assumed-rank
# specific is nested inside USE_FPOINTER_INTERFACES, and selects itself over the
# per-rank ones.
if(FORTRAN_ARRAY_INTERFACES STREQUAL "assumed-shape")
  target_compile_definitions(rocblas_fortran PRIVATE USE_FPOINTER_INTERFACES=1)
elseif(FORTRAN_ARRAY_INTERFACES STREQUAL "assumed-rank")
  target_compile_definitions(rocblas_fortran PRIVATE
    USE_FPOINTER_INTERFACES=1 USE_ASSUMED_RANK_INTERFACES=1)
endif()

# No line-length flag: the generator wraps the emitted source at 112 columns,
# inside the 132 that free-form Fortran guarantees, so every compiler reads it
# without a vendor-specific option.

target_include_directories(rocblas_fortran PUBLIC
  "$<BUILD_INTERFACE:${_moddir}>"
  "$<INSTALL_INTERFACE:include/fortran/${ROCBLAS_FORTRAN_COMPILER_DIR}>")

# Link interface only: needed to link an executable, not to build this archive.
# roc::rocblas is defined by rocBLAS's library/, and brings the HIP runtime with
# it. Standalone it has to be looked for, and testing for the target without
# ever calling find_package would mean it is never found: the archive would then
# export no link interface at all, and every consumer would hit undefined
# references to rocblas_create_handle and friends despite linking
# roc::rocblas_fortran exactly as documented.
#
# rocblas-config.cmake does find_dependency(hip), and hip-config.cmake reaches
# FindThreads, which hard-errors unless C or CXX is enabled. This project
# declares LANGUAGES NONE and then enables only Fortran, so C is enabled first;
# it compiles none of the sources here. QUIET, because a site that has no
# installed rocBLAS can still legitimately build the bindings: they compile
# against nothing.
if(NOT TARGET roc::rocblas)
  if(NOT CMAKE_C_COMPILER_LOADED AND NOT CMAKE_CXX_COMPILER_LOADED)
    enable_language(C)
  endif()
  find_package(rocblas QUIET)
endif()

if(TARGET roc::rocblas)
  target_link_libraries(rocblas_fortran INTERFACE roc::rocblas)
else()
  message(STATUS
    "rocBLAS: the C library was not found, so the Fortran bindings carry no "
    "link interface; a consumer will have to link it (-lrocblas) itself.")
endif()

if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(ROCBLAS_HAVE_FORTRAN_BINDINGS TRUE PARENT_SCOPE)
endif()

# No fortran/test/ here. rocBLAS already drives this binding from clients/:
# rocblas_fortran.f90 wraps the API for the gtest suite and `use rocblas`s the
# generated module, which is broader coverage than the hipFORT-derived tests
# would add and is already in CI. The nine libraries with no such path keep
# theirs.

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# lib/cmake/rocblas is where rocBLAS's own rocm_export_targets() puts
# rocblas-config.cmake, so the Fortran package lands beside the C one and a
# single CMAKE_PREFIX_PATH entry finds both.
#
# rocRAND and hipRAND cannot do this and install into lib/cmake/<lib>-fortran
# alone, because their shipped <lib>-config.cmake carries a bare unguarded
# include() of a sibling <lib>-fortran-config.cmake, and putting a file there
# would recurse. rocBLAS has no such include, checked against an installed
# ROCm, rocblas-config.cmake mentions Fortran nowhere, so it uses the shared
# layout, like hipBLAS and the FFT libraries.
set(_fortran_pkgdir "${CMAKE_INSTALL_LIBDIR}/cmake/rocblas")

install(TARGETS rocblas_fortran
        EXPORT rocblas-fortran-targets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}/fortran/${ROCBLAS_FORTRAN_COMPILER_DIR}")
install(DIRECTORY "${_moddir}/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/fortran/${ROCBLAS_FORTRAN_COMPILER_DIR}"
        FILES_MATCHING PATTERN "*.mod")

# The generated source, so that a user on a compiler ROCm ships no .mod for can
# rebuild the binding without fetching a monorepo to get one file. Source
# obtained separately can also be generated from a different ROCm than the
# installed librocblas, which surfaces as an undefined symbol at link time or as
# an interface that silently differs from the one the caller meant; source that
# ships with the library cannot drift from it.
#
# Under share/, not include/. The .mod is compiler-specific and this is not, and
# every entry under include/fortran/ is partitioned by compiler, so a source
# file there would be read as naming one: the config below enumerates that
# directory to report which compilers are installed, and would list rocblas.F90
# among them.
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/rocblas.F90"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/rocblas/fortran")
install(EXPORT rocblas-fortran-targets
        FILE rocblas-fortran-targets.cmake
        NAMESPACE roc::
        DESTINATION "${_fortran_pkgdir}")

configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/rocblas-fortran-config.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/rocblas-fortran-config.cmake"
  # No PATH_VARS: it only creates PACKAGE_<var>, and the template interpolates
  # @ROCBLAS_FORTRAN_COMPILER_DIR@ directly. Passing it was a dead argument, and
  # worse than dead if anyone later switched the template to the PACKAGE_ form,
  # since PATH_VARS would then resolve this relative value as a path under the
  # install prefix.
  INSTALL_DESTINATION "${_fortran_pkgdir}")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/rocblas-fortran-config.cmake"
        DESTINATION "${_fortran_pkgdir}")

# The config lands in lib/cmake/rocblas, BESIDE the C package, because that is
# what would let rocblas-config.cmake pick it up with a bare
# include(rocblas-fortran-config.cmake OPTIONAL) once rocm_export_targets grows
# a hook for it: a Fortran binding installed after the C library would then be
# found at find_package(rocblas) time, and ignored when absent.
#
# rocm_export_targets' existing INCLUDE argument is NOT that hook. It emits a
# bare, non-OPTIONAL include() and emits it BEFORE the targets file
# (ROCMInstallTargets.cmake writes INCLUDE entries first), so a Fortran config
# reached that way would run before roc::rocblas exists. That change belongs in
# rocm-cmake.
#
# But find_package(rocblas-fortran): the documented spelling, and the one the
# config's own header shows, searches lib/cmake/rocblas-fortran*, which
# lib/cmake/rocblas does not match, so on its own that placement makes the
# direct spelling fail with "could not find a package configuration file". A
# forwarder gives the search somewhere to land without duplicating the real
# config, which would also need its own copy of the targets file and would then
# be a second thing to keep in step.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/forward/rocblas-fortran-config.cmake"
"# Generated forwarder. The real config lives beside the rocBLAS C package, in
# lib/cmake/rocblas, so that rocblas-config.cmake can include it as a sibling.
# This file only exists so that find_package(rocblas-fortran) resolves to the
# same place.
include(\"\${CMAKE_CURRENT_LIST_DIR}/../rocblas/rocblas-fortran-config.cmake\")
")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/forward/rocblas-fortran-config.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/rocblas-fortran")

# A version file, so that find_package(rocblas-fortran 5.1) resolves rather than
# failing with "could not find a configuration file compatible with requested
# version". SameMajorVersion because the module's bind(C) surface tracks the C
# API's compatibility promise.
#
# The version is read from THIS tree's top-level CMakeLists, not from
# rocblas_VERSION. That variable looks like the obvious source and is a trap: in
# a standalone build it is set by the find_package(rocblas) above, so it reports
# whatever rocBLAS happens to be INSTALLED (4.5.0 from a stock /opt/rocm) rather
# than the version of the headers these bindings were generated from. The
# bindings ship with their source tree, so the source tree is the truth.
set(_fortran_version "")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../CMakeLists.txt")
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../CMakeLists.txt" _rocblas_root)
  # rocBLAS spells it set ( VERSION_STRING "5.8.0" ), note the spaces, and then
  # passes the variable
  # to rocm_setup_version, so match the literal assignment rather than the call.
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
  # Installed in BOTH package directories, and it has to be. CMake looks for
  # <pkg>-config-version.cmake NEXT TO the <pkg>-config.cmake it actually
  # resolved, not wherever the real one lives. find_package(rocblas-fortran 5.8)
  # resolves the forwarder in lib/cmake/rocblas-fortran, so a version file only
  # beside the real config in lib/cmake/rocblas is never consulted, and the call
  # fails with "could not find a configuration file compatible with requested
  # version" even though the config is right there and the version matches.
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/rocblas-fortran-config-version.cmake"
          DESTINATION "${_fortran_pkgdir}")
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/rocblas-fortran-config-version.cmake"
          DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/rocblas-fortran")
else()
  # Better no version file than a wrong one: without it an unversioned
  # find_package still succeeds, whereas a version file claiming the wrong
  # number makes a versioned one succeed and link the wrong .mod.
  message(STATUS
    "rocBLAS: could not determine the version for the Fortran package, so no "
    "rocblas-fortran-config-version.cmake is installed; "
    "find_package(rocblas-fortran <version>) will not match.")
endif()
