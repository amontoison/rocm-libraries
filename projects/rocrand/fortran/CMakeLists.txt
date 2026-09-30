# Fortran bindings for rocRAND.
#
# rocrand.F90 is GENERATED from the rocRAND C headers by the rocm-fortran
# generator and committed here, next to the library it binds, so that a header
# change and its binding travel in the same pull request. Do not edit it by
# hand: regenerate it instead.
#
# The bindings are a single self-contained Fortran module, `rocrand`, carrying
# the bind(C) interfaces, the enum constants, the two C-layout structs the API
# passes directly (uint4 and rocrand_discrete_distribution_st) and the
# rocrandCheck status helper. A consumer writes one `use rocrand`.
#
# Opaque handles have no Fortran wrapper type. A generator and a discrete
# distribution are type(c_ptr) at every interface, passed straight to C rather
# than through a struct the caller has to unpack, so there is nothing for the
# Fortran side to keep in step with the C side when a handle changes shape.
#
# This directory is self-contained: it builds with no dependency on the rocRAND
# C target, because a Fortran binding compiles against nothing (it only declares
# bind(C) interfaces). librocrand is required to LINK a final executable, and is
# carried as the target's link interface for that reason.
#
# This module REPLACES the hand-written wrapper that used to live in
# library/src/fortran (rocrand_m.f90), removed in the same commit that added
# this directory. That wrapper was deprecated in favour of hipfort in ROCm 7.0
# and was never shipped enabled: ROCm built it with BUILD_FORTRAN_WRAPPER=OFF,
# so rocrand_FORTRAN_FOUND was NOTFOUND and its sources were not installed.

cmake_minimum_required(VERSION 3.19)

# Configured directly (not as a subdirectory of the rocRAND build), this
# directory is its own project, so that the bindings can be built and packaged
# on their own, which is the supported path for anyone whose Fortran compiler
# is not the amdflang the shipped bindings were built with. In-tree, rocRAND's
# own project() is already in effect.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(rocrand-fortran LANGUAGES NONE)
endif()

# ON by default but GUARDED: a site with no Fortran compiler must still
# configure and build the C library. The monorepo root already probes once and
# publishes ROCM_HAVE_FORTRAN; a standalone rocRAND build falls back to its own
# probe.
option(BUILD_FORTRAN_BINDINGS "Build the rocRAND Fortran bindings" ON)

# The bindings' own test suite, and the Fortran clients where a library has
# them: one switch rather than two neighbouring ones. ON by default, and
# guarded the same way as the bindings, so asking for them when the bindings
# were not built is a skip rather than an error.
option(BUILD_FORTRAN_CLIENTS "Build the rocRAND Fortran clients and binding tests" ON)

# BUILD_FORTRAN_BINDINGS and BUILD_FORTRAN_CLIENTS are ONE cache entry shared by
# every library in the monorepo, which is what you want for the common case:
# -DBUILD_FORTRAN_BINDINGS=OFF from the top turns the whole Fortran track off in
# a single flag. It is not what you want when one library has to be singled out,
# so each library also honours a name of its own, which overrides the shared
# default.
#
# That override is the last word for the BINDINGS only. For the CLIENTS it is a
# ceiling and not a floor: in-tree the tests are GPU runtime executables and
# belong to rocRAND's own test track, so BUILD_TEST bounds them from below and
# ROCRAND_BUILD_FORTRAN_CLIENTS=ON cannot raise them above it. The gate that
# enforces that runs further down, past the early returns.
if(DEFINED ROCRAND_BUILD_FORTRAN_BINDINGS)
  set(BUILD_FORTRAN_BINDINGS ${ROCRAND_BUILD_FORTRAN_BINDINGS})
endif()
if(DEFINED ROCRAND_BUILD_FORTRAN_CLIENTS)
  set(BUILD_FORTRAN_CLIENTS ${ROCRAND_BUILD_FORTRAN_CLIENTS})
endif()

# Which array-argument overloads the module exposes. One tri-state drives the
# two macros the generated source is guarded with, USE_ASSUMED_SHAPE and
# USE_ASSUMED_RANK, which are mutually exclusive: an assumed-rank dummy is not
# distinguishable by rank from the per-rank specifics, so the two cannot legally
# coexist in one generic and assumed-rank REPLACES the per-rank variants rather
# than adding to them. The translation below therefore defines exactly one.
#
# Array overloads are generated for the subset of routines the generator
# supports -- grep the source for `interface rocrand_` to see which. Every
# other routine is a plain interface under the name it binds, identical in all
# three tiers, and the raw type(c_ptr) surface stays available in every tier
# even for the routines that do carry overloads.
#
#   none           the raw type(c_ptr) surface only (Fortran 2003)
#   assumed-shape  + rank-0 and rank-1 overloads (Fortran 2008): the default,
#                  and what the bindings shipped in ROCm are built with
#   assumed-rank   + one dimension(..) overload (Fortran 2018)
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
  set(ROCRAND_HAVE_FORTRAN_BINDINGS FALSE PARENT_SCOPE)
endif()

if(NOT BUILD_FORTRAN_BINDINGS)
  return()
endif()

# Probing for a Fortran compiler is not free, and every library in a monorepo
# build would otherwise repeat it, so the monorepo root probes ONCE and publishes
# the answer as ROCM_HAVE_FORTRAN. That name is deliberately neither
# rocm-libraries' nor rocm-systems': rocRAND is regularly built inside a
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
  message(STATUS "rocRAND: no Fortran compiler found, skipping the Fortran bindings")
  return()
endif()

enable_language(Fortran)

# Pulled in here rather than beside the install() rules below because the
# install layout is also baked into the target's INSTALL_INTERFACE include
# directory, which is set well before them. Nothing must hardcode lib/ or
# include/: a distro that installs to lib64 gets CMAKE_INSTALL_LIBDIR=lib64,
# and a literal lib/fortran then names a directory that does not exist.
include(GNUInstallDirs)

if(NOT FORTRAN_ARRAY_INTERFACES MATCHES "^(none|assumed-shape|assumed-rank)$")
  message(FATAL_ERROR
    "FORTRAN_ARRAY_INTERFACES must be none, assumed-shape or assumed-rank, "
    "not '${FORTRAN_ARRAY_INTERFACES}'.")
endif()

add_library(rocrand_fortran STATIC rocrand.F90)

# roc::, matching the namespace rocRAND exports its C library under (see
# library/CMakeLists.txt), so that a consumer is not made to remember that two
# halves of one package sit in different namespaces.
add_library(roc::rocrand_fortran ALIAS rocrand_fortran)

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
# ROCRAND_FORTRAN_COMPILER_DIR itself, for example to gfortran-13.3.0.
set(ROCRAND_FORTRAN_COMPILER_DIR "${_fc_name}" CACHE STRING
    "Per-compiler subdirectory the .mod and the archive install under")

set(_moddir "${CMAKE_CURRENT_BINARY_DIR}/include")
set_target_properties(rocrand_fortran PROPERTIES
  Fortran_MODULE_DIRECTORY "${_moddir}"
  # rocrand.F90 is uppercase: the array overloads sit behind preprocessor
  # guards, so the source must go through the C preprocessor.
  Fortran_PREPROCESS ON
  # Free form is the .F90 default, but stating it lets CMake emit whichever flag
  # the compiler in use expects rather than relying on the extension.
  Fortran_FORMAT FREE
  POSITION_INDEPENDENT_CODE ON)

# Translate the tri-state into the macro the generated source is guarded with.
# The option value and the macro name are the same word. The source asks for
# USE_ASSUMED_RANK first and falls through its #else to USE_ASSUMED_SHAPE, so
# each tier selects itself. `none` defines neither and leaves the raw
# type(c_ptr) surface, which is present in every tier.
#
# The branches below define exactly one macro, so the both-defined case is out
# of reach from here. It is well defined in the source all the same: the
# interface block and the contains section both test USE_ASSUMED_RANK first, so
# assumed-rank wins and assumed-shape is skipped. Preprocessing rocrand.F90
# with both macros yields byte-identical text to preprocessing it with
# USE_ASSUMED_RANK alone, and amdflang -fsyntax-only accepts the module under
# each of the four combinations.
if(FORTRAN_ARRAY_INTERFACES STREQUAL "assumed-shape")
  target_compile_definitions(rocrand_fortran PRIVATE USE_ASSUMED_SHAPE=1)
elseif(FORTRAN_ARRAY_INTERFACES STREQUAL "assumed-rank")
  target_compile_definitions(rocrand_fortran PRIVATE USE_ASSUMED_RANK=1)
endif()

# No line-length flag: the generator wraps the emitted source at 112 columns,
# inside the 132 that free-form Fortran guarantees, so every compiler reads it
# without a vendor-specific option.

target_include_directories(rocrand_fortran PUBLIC
  "$<BUILD_INTERFACE:${_moddir}>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/fortran/${ROCRAND_FORTRAN_COMPILER_DIR}>")

# Link interface only: needed to link an executable, not to build this archive.
# roc::rocrand is defined by rocRAND's library/, and brings the HIP runtime with
# it. Standalone it has to be looked for, and testing for the target without
# ever calling find_package would mean it is never found: the archive would then
# export no link interface at all, and every consumer would hit undefined
# references to rocrand_create_generator and friends despite linking
# roc::rocrand_fortran exactly as documented.
#
# rocrand-config.cmake does find_dependency(hip), and hip-config.cmake reaches
# FindThreads, which hard-errors unless C or CXX is enabled. This project
# declares LANGUAGES NONE and then enables only Fortran, so C is enabled first;
# it compiles none of the sources here. QUIET, because a site that has no
# installed rocRAND can still legitimately build the bindings: they compile
# against nothing.
if(NOT TARGET roc::rocrand)
  if(NOT CMAKE_C_COMPILER_LOADED AND NOT CMAKE_CXX_COMPILER_LOADED)
    enable_language(C)
  endif()
  find_package(rocrand QUIET)
endif()

# The same condition decides whether the installed config package goes looking
# for the C one. When the link interface is empty the consumer depends on no
# rocRAND package at all, so running find_dependency(rocrand) in their configure
# would fail it over something this package does not use. The value reaches the
# template as a literal TRUE or FALSE, which is why both branches write it: an
# unset variable would leave a bare if() in the installed config and break every
# consumer's find_package.
if(TARGET roc::rocrand)
  target_link_libraries(rocrand_fortran INTERFACE roc::rocrand)
  set(ROCRAND_FORTRAN_NEEDS_C_PACKAGE TRUE)
else()
  set(ROCRAND_FORTRAN_NEEDS_C_PACKAGE FALSE)
  message(STATUS
    "rocRAND: the C library was not found, so the Fortran bindings carry no "
    "link interface; a consumer will have to link it (-lrocrand) itself.")
endif()

if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(ROCRAND_HAVE_FORTRAN_BINDINGS TRUE PARENT_SCOPE)
endif()

# The binding tests are GPU runtime executables, so in-tree they belong to the
# test track and have to follow the library's own switch: a library-only build
# has no business producing test binaries, and would be made to link a GPU
# runtime it never asked for. The condition is the one rocRAND guards
# add_subdirectory(test) with, so the two cannot disagree.
#
# Standalone, this directory IS the build and the tests are the point, so the
# library's switch does not exist and BUILD_FORTRAN_CLIENTS stands on its own.
# Setting it as a plain variable is deliberate: the cache entry is one switch
# shared by every library in the monorepo, so turning it off here has to be
# scoped to this directory and must not be written back.
#
# This sits here, past both early returns, rather than beside the option block:
# a configure with BUILD_FORTRAN_BINDINGS=OFF, or one on a host with no Fortran
# compiler, leaves this directory before any of it matters, and announcing a
# skipped test suite there would name a reason that is not the one that applied.
if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR
   AND BUILD_FORTRAN_CLIENTS
   AND NOT BUILD_TEST)
  message(STATUS
    "rocRAND: tests are not being built, skipping the Fortran binding tests.")
  set(BUILD_FORTRAN_CLIENTS OFF)
endif()

if(BUILD_FORTRAN_CLIENTS)
  # enable_testing() here is enough for a STANDALONE build of this directory, but
  # NOT for an in-tree one: CMake only writes the chain of CTestTestfile.cmake
  # that ctest walks down from a build root when testing was enabled at the TOP
  # level. Enabled only here, the tests are built and registered, and `ctest` run
  # from the top of the rocRAND build reports "Total Tests: 0": a silent
  # nothing, which is the worst way to find out. So say it.
  enable_testing()
  if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    get_property(_top_testing GLOBAL PROPERTY CTEST_TARGETS_ADDED)
    if(NOT _top_testing)
      message(STATUS
        "rocRAND: the Fortran binding tests are registered, but ctest discovers "
        "tests only from a build root where testing was enabled at the top "
        "level. If `ctest` there reports no tests, add include(CTest) or "
        "enable_testing() to the top-level CMakeLists, or run ctest from "
        "${CMAKE_CURRENT_BINARY_DIR}/test.")
    endif()
  endif()
  add_subdirectory(test)
endif()

include(CMakePackageConfigHelpers)

# lib/cmake/rocrand is where rocrand's own rocm_export_targets() puts
# rocrand-config.cmake, so the Fortran package lands beside the C one and a
# single CMAKE_PREFIX_PATH entry finds both.
set(_fortran_pkgdir "${CMAKE_INSTALL_LIBDIR}/cmake/rocrand")

# COMPONENT devel on every rule below, because everything this directory
# installs -- a static archive, a set of .mod files, the generated source and
# the CMake package -- is a development artifact: nothing here is needed to RUN
# a program, only to build one against the bindings. devel is also what
# rocm-cmake would have chosen: rocm_install_targets and rocm_install default to
# it. Without it these files land in the runtime component instead.
#
# Plain install(), not rocm_install(): this directory must also configure
# standalone, where rocm-cmake was never loaded and those commands do not exist.
install(TARGETS rocrand_fortran
        EXPORT rocrand-fortran-targets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}/fortran/${ROCRAND_FORTRAN_COMPILER_DIR}"
                COMPONENT devel)
install(DIRECTORY "${_moddir}/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/fortran/${ROCRAND_FORTRAN_COMPILER_DIR}"
        COMPONENT devel
        FILES_MATCHING PATTERN "*.mod")

# The generated source, so that a user on a compiler ROCm ships no .mod for can
# rebuild the binding without fetching a monorepo to get one file. Source
# obtained separately can also be generated from a different ROCm than the
# installed librocrand, which surfaces as an undefined symbol at link time or as
# an interface that silently differs from the one the caller meant; source that
# ships with the library cannot drift from it.
#
# Under share/, not include/. The .mod is compiler-specific and this is not, and
# every entry under include/fortran/ is partitioned by compiler, so a source
# file there would be read as naming one: the config below enumerates that
# directory to report which compilers are installed, and would list rocrand.F90
# among them.
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/rocrand.F90"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/rocrand/fortran"
        COMPONENT devel)
install(EXPORT rocrand-fortran-targets
        FILE rocrand-fortran-targets.cmake
        NAMESPACE roc::
        DESTINATION "${_fortran_pkgdir}"
        COMPONENT devel)

# The two directories the config package has to name, kept out of the template
# so that neither side spells lib/ or include/ literally. A literal lib/fortran
# is wrong the moment CMAKE_INSTALL_LIBDIR is lib64, and the config then hands
# the consumer an IMPORTED_LOCATION that does not exist.
set(ROCRAND_FORTRAN_LIBDIR     "${CMAKE_INSTALL_LIBDIR}/fortran")
set(ROCRAND_FORTRAN_INCLUDEDIR "${CMAKE_INSTALL_INCLUDEDIR}/fortran")

configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/rocrand-fortran-config.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/rocrand-fortran-config.cmake"
  INSTALL_DESTINATION "${_fortran_pkgdir}"
  # PATH_VARS is exactly what these two want: @PACKAGE_INIT@ rewrites each into
  # an absolute path computed from where the config file was FOUND, so the
  # package survives being relocated and gets lib64 right without the template
  # knowing which layout it was built for. ROCRAND_FORTRAN_COMPILER_DIR stays a
  # plain @-substitution on purpose -- it is a bare directory NAME, not a path,
  # and PATH_VARS would resolve it as one, under the install prefix.
  PATH_VARS ROCRAND_FORTRAN_LIBDIR ROCRAND_FORTRAN_INCLUDEDIR)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/rocrand-fortran-config.cmake"
        DESTINATION "${_fortran_pkgdir}"
        COMPONENT devel)

# find_package(rocrand-fortran), the documented spelling, searches
# lib/cmake/rocrand-fortran*, which lib/cmake/rocrand does not match. A forwarder
# gives that search somewhere to land without duplicating the real config.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/forward/rocrand-fortran-config.cmake"
"# Generated forwarder. The real config lives beside the rocrand C package, in
# lib/cmake/rocrand. This file only exists so that find_package(rocrand-fortran)
# resolves to the same place.
include(\"\${CMAKE_CURRENT_LIST_DIR}/../rocrand/rocrand-fortran-config.cmake\")
")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/forward/rocrand-fortran-config.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/rocrand-fortran"
        COMPONENT devel)

# A version file, so that find_package(rocrand-fortran 5.1) resolves rather than
# failing with "could not find a configuration file compatible with requested
# version". SameMajorVersion because the module's bind(C) surface tracks the C
# API's compatibility promise.
#
# The version is read from THIS tree's top-level CMakeLists, not from
# rocrand_VERSION. That variable looks like the obvious source and is a trap: in
# a standalone build it is set by the find_package(rocrand) above, so it reports
# whatever rocRAND happens to be INSTALLED (4.5.0 from a stock /opt/rocm) rather
# than the version of the headers these bindings were generated from. The
# bindings ship with their source tree, so the source tree is the truth.
set(_fortran_version "")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../CMakeLists.txt")
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../CMakeLists.txt" _rocrand_root)
  string(REGEX MATCH "rocm_setup_version\\([ \t]*VERSION[ \t]+\"?([0-9]+\\.[0-9]+\\.[0-9]+)"
         _m "${_rocrand_root}")
  if(CMAKE_MATCH_1)
    set(_fortran_version "${CMAKE_MATCH_1}")
  endif()
  unset(_rocrand_root)
endif()

if(_fortran_version)
  write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/rocrand-fortran-config-version.cmake"
    VERSION "${_fortran_version}"
    COMPATIBILITY SameMajorVersion)
  # Both package directories: CMake looks for the version file next to the
  # <pkg>-config.cmake it actually resolved, and find_package(rocrand-fortran <ver>)
  # resolves the forwarder.
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/rocrand-fortran-config-version.cmake"
          DESTINATION "${_fortran_pkgdir}"
          COMPONENT devel)
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/rocrand-fortran-config-version.cmake"
          DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/rocrand-fortran"
          COMPONENT devel)
else()
  # Better no version file than a wrong one: without it an unversioned
  # find_package still succeeds, whereas a version file claiming the wrong
  # number makes a versioned one succeed and link the wrong .mod.
  message(STATUS
    "rocRAND: could not determine the version for the Fortran package, so no "
    "rocrand-fortran-config-version.cmake is installed; "
    "find_package(rocrand-fortran <version>) will not match.")
endif()
