# Fortran bindings for rocSPARSE.
#
# rocsparse.F90 is GENERATED from the rocSPARSE C headers by the rocm-fortran
# generator and committed here, next to the library it binds, so that a header
# change and its binding travel in the same pull request. Do not edit it by
# hand: regenerate it instead.
#
# The bindings are a single self-contained Fortran module, `rocsparse`, carrying
# one bind(C) interface per rocSPARSE entry point (731 distinct bind(C) names),
# the enum constants and the rocsparseCheck status helper. A consumer writes one
# `use rocsparse`.
#
# What the module deliberately does NOT carry: there are no derived-type
# wrappers, no `_typed` strongly-typed overloads and no `_dptr` device-pointer
# specifics, no `character` convenience variants, and the source ships without
# docstrings. Every rocSPARSE handle and descriptor is an opaque `type(c_ptr)`,
# the same representation the hand-written binding used, so the C header stays
# the single description of what a handle is and of every argument. None of that
# is a compatibility break: the generated bindings are new here and no release
# ever shipped a version of them that had more.
#
# This directory is self-contained: it builds with no dependency on the rocSPARSE
# C target, because a Fortran binding compiles against nothing (it only declares
# bind(C) interfaces). librocsparse is required to LINK a final executable, and is
# carried as the target's link interface for that reason.
#
# This module REPLACES the hand-written binding that used to live in
# library/src (rocsparse.f90 and rocsparse_enums.f90), removed in the same commit
# that added this directory. Unlike rocRAND's, that one was shipped and built by
# default, and the clients' Fortran samples were compiled from it; they now
# compile against this module instead and link the target exported here.

cmake_minimum_required(VERSION 3.19)

# Configured directly (not as a subdirectory of the rocSPARSE build), this
# directory is its own project, so that the bindings can be built and packaged
# on their own, which is the supported path for anyone whose Fortran compiler
# is not the amdflang the shipped bindings were built with. In-tree, rocSPARSE's
# own project() is already in effect.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(rocsparse-fortran LANGUAGES NONE)
endif()

# ON by default but GUARDED: a site with no Fortran compiler must still
# configure and build the C library. The monorepo root already probes once and
# publishes ROCM_HAVE_FORTRAN; a standalone rocSPARSE build falls back to its own
# probe.
#
# "${UNIX}" rather than ON, because Windows is not such a site: ROCm ships no
# Fortran compiler there at all. TheRock puts flang in LLVM_ENABLE_PROJECTS
# only in the non-WIN32 branch of compiler/pre_hook_amd-llvm.cmake, so the
# amdflang preference below finds nothing, the probe falls through to whatever
# gfortran happens to be on PATH, and enable_language(Fortran) then fails to
# identify it. A default and not a veto: -DBUILD_FORTRAN_BINDINGS=ON still
# works wherever a usable compiler exists.
option(BUILD_FORTRAN_BINDINGS "Build the rocSPARSE Fortran bindings" "${UNIX}")

# The bindings' own test suite, and the Fortran clients where a library has
# them: one switch rather than two neighbouring ones. ON by default, and
# guarded the same way as the bindings, so asking for them when the bindings
# were not built is a skip rather than an error.
option(BUILD_FORTRAN_CLIENTS "Build the rocSPARSE Fortran clients and binding tests" ON)

# BUILD_FORTRAN_BINDINGS and BUILD_FORTRAN_CLIENTS are ONE cache entry shared by
# every library in the monorepo, which is what you want for the common case:
# -DBUILD_FORTRAN_BINDINGS=OFF from the top turns the whole Fortran track off in
# a single flag. It is not what you want when one library has to be singled out,
# so each library also honours a name of its own, which overrides the shared
# default.
#
# For the CLIENTS half that override is a ceiling rather than a guarantee. The
# gate further down forces BUILD_FORTRAN_CLIENTS off in any in-tree build that
# is not building rocSPARSE's clients, and it does so unconditionally, an
# explicit ROCSPARSE_BUILD_FORTRAN_CLIENTS=ON included: the binding tests are
# GPU test executables and belong to the client/test track, so the library's own
# switch is a floor they cannot rise above.
if(DEFINED ROCSPARSE_BUILD_FORTRAN_BINDINGS)
  set(BUILD_FORTRAN_BINDINGS ${ROCSPARSE_BUILD_FORTRAN_BINDINGS})
endif()
if(DEFINED ROCSPARSE_BUILD_FORTRAN_CLIENTS)
  set(BUILD_FORTRAN_CLIENTS ${ROCSPARSE_BUILD_FORTRAN_CLIENTS})
endif()

# Which array-argument overloads the module exposes. One tri-state replaces the
# pair of booleans a caller would otherwise have to combine correctly, because
# the two upper tiers are mutually exclusive: an assumed-rank dummy is not
# distinguishable by rank from the per-rank specifics, so assumed-rank REPLACES
# the per-rank variants rather than adding to them. A tri-state has no way to
# spell the combination, which is the point of choosing one.
#
# Array overloads are generated for the subset of routines the generator
# supports; every other routine exposes the same plain type(c_ptr) interface
# under all three settings, so no entry point becomes unreachable by choosing a
# tier, and the tier only ever ADDS to the `none` surface.
#
#   none           the raw type(c_ptr) surface only (Fortran 2003)
#   assumed-shape  + dimension(:) overloads, one per rank, for the routines
#                  that have them (Fortran 2008): the default, and what the
#                  bindings shipped in ROCm are built with
#   assumed-rank   + one dimension(..) overload for each of those routines
#                  (Fortran 2018)
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
  set(ROCSPARSE_HAVE_FORTRAN_BINDINGS FALSE PARENT_SCOPE)
endif()

if(NOT BUILD_FORTRAN_BINDINGS)
  return()
endif()

# ILP64 makes rocsparse_int an int64_t, while the generated module binds every
# rocsparse_int argument as integer(c_int) and carries nothing that tells the
# two builds apart. The bindings would therefore compile, link against an ILP64
# librocsparse, and then pass four bytes where the library reads eight: not
# unavailable, but silently ABI-incompatible, which is the one failure mode a
# binding must not have. So under ILP64 no Fortran is built at all.
#
# A skip rather than a FATAL_ERROR: a C-only ILP64 build is a legitimate thing
# to ask for and has to keep configuring. ROCSPARSE_HAVE_FORTRAN_BINDINGS is
# already FALSE in the parent scope at this point, so clients/ sees the bindings
# as absent and skips its Fortran samples exactly as it does when no compiler
# was found.
#
# Only the in-tree flag is visible from here. Configured standalone against an
# installed ILP64 librocsparse there is nothing to read, and avoiding that
# mismatch remains the caller's.
if(BUILD_ROCSPARSE_ILP64)
  message(STATUS
    "rocSPARSE: BUILD_ROCSPARSE_ILP64 widens rocsparse_int to int64_t, which "
    "the generated module binds as integer(c_int); skipping the Fortran "
    "bindings rather than building one that is silently ABI-incompatible.")
  return()
endif()

# Probing for a Fortran compiler is not free, and every library in a monorepo
# build would otherwise repeat it, so the monorepo root probes ONCE and publishes
# the answer as ROCM_HAVE_FORTRAN. That name is deliberately neither
# rocm-libraries' nor rocm-systems': rocSPARSE is regularly built inside a
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
  message(STATUS "rocSPARSE: no Fortran compiler found, skipping the Fortran bindings")
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

add_library(rocsparse_fortran STATIC rocsparse.F90)

# roc::, matching the namespace rocSPARSE exports its C library under (see
# library/CMakeLists.txt), so that a consumer is not made to remember that two
# halves of one package sit in different namespaces.
add_library(roc::rocsparse_fortran ALIAS rocsparse_fortran)

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
# ROCSPARSE_FORTRAN_COMPILER_DIR itself, for example to gfortran-13.3.0.
set(ROCSPARSE_FORTRAN_COMPILER_DIR "${_fc_name}" CACHE STRING
    "Per-compiler subdirectory the .mod and the archive install under")

set(_moddir "${CMAKE_CURRENT_BINARY_DIR}/include")
set_target_properties(rocsparse_fortran PROPERTIES
  Fortran_MODULE_DIRECTORY "${_moddir}"
  # rocsparse.F90 is uppercase: the array overloads sit behind preprocessor
  # guards, so the source must go through the C preprocessor.
  Fortran_PREPROCESS ON
  # Free form is the .F90 default, but stating it lets CMake emit whichever flag
  # the compiler in use expects rather than relying on the extension.
  Fortran_FORMAT FREE
  POSITION_INDEPENDENT_CODE ON)

# Translate the tri-state into the macro the generated source is guarded with.
# The option value and the macro name are the same word, so there is nothing to
# remember: assumed-shape selects USE_ASSUMED_SHAPE and assumed-rank selects
# USE_ASSUMED_RANK.
#
# This branch defines exactly one macro, never both, because a tri-state has no
# way to spell the combination. Defining both by hand is nonetheless
# well-defined rather than an error: the generic-interface region and the
# procedure-body region of the generated source both test USE_ASSUMED_RANK
# first, so assumed-rank wins and USE_ASSUMED_SHAPE is ignored. Checked with
#
#   amdflang -cpp -DUSE_ASSUMED_SHAPE=1 -DUSE_ASSUMED_RANK=1 -fsyntax-only \
#     rocsparse.F90
#
# which compiles cleanly, and whose preprocessed output is byte-identical to
# that of -DUSE_ASSUMED_RANK=1 alone.
#
# none defines neither and leaves the raw type(c_ptr) surface.
if(FORTRAN_ARRAY_INTERFACES STREQUAL "assumed-shape")
  target_compile_definitions(rocsparse_fortran PRIVATE USE_ASSUMED_SHAPE=1)
elseif(FORTRAN_ARRAY_INTERFACES STREQUAL "assumed-rank")
  target_compile_definitions(rocsparse_fortran PRIVATE USE_ASSUMED_RANK=1)
endif()

# No line-length flag: the generator wraps the emitted source at 112 columns,
# inside the 132 that free-form Fortran guarantees, so every compiler reads it
# without a vendor-specific option.

target_include_directories(rocsparse_fortran PUBLIC
  "$<BUILD_INTERFACE:${_moddir}>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/fortran/${ROCSPARSE_FORTRAN_COMPILER_DIR}>")

# Link interface only: needed to link an executable, not to build this archive.
# roc::rocsparse is defined by rocSPARSE's library/, and brings the HIP runtime with
# it. Standalone it has to be looked for, and testing for the target without
# ever calling find_package would mean it is never found: the archive would then
# export no link interface at all, and every consumer would hit undefined
# references to rocsparse_create_handle and friends despite linking
# roc::rocsparse_fortran exactly as documented.
#
# rocsparse-config.cmake does find_dependency(hip), and hip-config.cmake reaches
# FindThreads, which hard-errors unless C or CXX is enabled. This project
# declares LANGUAGES NONE and then enables only Fortran, so C is enabled first;
# it compiles none of the sources here. QUIET, because a site that has no
# installed rocSPARSE can still legitimately build the bindings: they compile
# against nothing.
if(NOT TARGET roc::rocsparse)
  if(NOT CMAKE_C_COMPILER_LOADED AND NOT CMAKE_CXX_COMPILER_LOADED)
    enable_language(C)
  endif()
  find_package(rocsparse QUIET)
endif()

# Whichever way this goes is recorded for the config template, which must call
# find_dependency(rocsparse) only when the exported archive really carries
# roc::rocsparse in its link interface. A package built without the C library
# has an empty link interface, so requiring the consumer to find rocSPARSE would
# be over-strict about a dependency this package does not have, while still
# leaving them to resolve rocsparse_create_handle and friends by hand. The value
# is an explicit TRUE or FALSE rather than an empty string, because it is
# substituted straight into an if() in the installed config, and a bare if()
# there is a syntax error that breaks every consumer's find_package.
if(TARGET roc::rocsparse)
  target_link_libraries(rocsparse_fortran INTERFACE roc::rocsparse)
  set(ROCSPARSE_FORTRAN_NEEDS_C_PACKAGE TRUE)
else()
  set(ROCSPARSE_FORTRAN_NEEDS_C_PACKAGE FALSE)
  message(STATUS
    "rocSPARSE: the C library was not found, so the Fortran bindings carry no "
    "link interface; a consumer will have to link it (-lrocsparse) itself.")
endif()

if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(ROCSPARSE_HAVE_FORTRAN_BINDINGS TRUE PARENT_SCOPE)
endif()

# The binding tests below are GPU runtime executables, so in-tree they belong to
# the client/test track and follow the library's own switch: a library-only
# build has no business producing test binaries, and would be made to link a GPU
# runtime it never asked for. The condition is the one rocSPARSE guards
# add_subdirectory(clients) with -- BUILD_CLIENTS, which the root derives from
# BUILD_CLIENTS_ONLY, BUILD_CLIENTS_SAMPLES, BUILD_CLIENTS_TESTS and
# BUILD_CLIENTS_BENCHMARKS -- so the two cannot disagree.
#
# It sits here, next to the one place that reads the switch, rather than beside
# the option block: everything above this point can still return early, and with
# BUILD_FORTRAN_BINDINGS off, under ILP64, or with no Fortran compiler, no
# Fortran is configured at all. Announcing a skipped test suite there would
# report a reason that is not the operative one.
#
# Standalone, this directory IS the build and the tests are the point, so the
# library's switch does not exist and BUILD_FORTRAN_CLIENTS stands on its own.
# Setting it as a plain variable is deliberate: the cache entry is one switch
# shared by every library in the monorepo, so turning it off here has to be
# scoped to this directory and must not be written back.
if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR
   AND BUILD_FORTRAN_CLIENTS
   AND NOT BUILD_CLIENTS)
  message(STATUS
    "rocSPARSE: clients are not being built, skipping the Fortran binding tests.")
  set(BUILD_FORTRAN_CLIENTS OFF)
endif()

if(BUILD_FORTRAN_CLIENTS)
  # enable_testing() here is enough for a STANDALONE build of this directory, but
  # NOT for an in-tree one: CMake only writes the chain of CTestTestfile.cmake
  # that ctest walks down from a build root when testing was enabled at the TOP
  # level. Enabled only here, the tests are built and registered, and `ctest` run
  # from the top of the rocSPARSE build reports "Total Tests: 0": a silent
  # nothing, which is the worst way to find out. So say it.
  enable_testing()
  if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    get_property(_top_testing GLOBAL PROPERTY CTEST_TARGETS_ADDED)
    if(NOT _top_testing)
      message(STATUS
        "rocSPARSE: the Fortran binding tests are registered, but ctest discovers "
        "tests only from a build root where testing was enabled at the top "
        "level. If `ctest` there reports no tests, add include(CTest) or "
        "enable_testing() to the top-level CMakeLists, or run ctest from "
        "${CMAKE_CURRENT_BINARY_DIR}/test.")
    endif()
  endif()
  add_subdirectory(test)
endif()

include(CMakePackageConfigHelpers)

# lib/cmake/rocsparse is where rocSPARSE's own rocm_export_targets() puts
# rocsparse-config.cmake, so the Fortran package lands beside the C one and a
# single CMAKE_PREFIX_PATH entry finds both.
#
# rocRAND and hipRAND cannot do this and install into lib/cmake/<lib>-fortran
# alone, because their shipped <lib>-config.cmake carries a bare unguarded
# include() of a sibling <lib>-fortran-config.cmake, and putting a file there
# would recurse. rocSPARSE has no such include, checked against an installed
# ROCm, rocsparse-config.cmake mentions Fortran nowhere, so it uses the shared
# layout, like hipBLAS and the FFT libraries.
set(_fortran_pkgdir "${CMAKE_INSTALL_LIBDIR}/cmake/rocsparse")

# COMPONENT devel on every rule below, because everything this directory
# installs -- a static archive, a set of .mod files, the generated source and
# the CMake package -- is a development artifact: nothing here is needed to RUN
# a program, only to build one against the bindings. devel is also what
# rocm-cmake would have chosen: rocm_install_targets and rocm_install default to
# it. Without it these files land in the runtime component instead, which for
# rocSPARSE is a packaging regression, since the hand-written .f90 they replace
# was shipped by a rocm_install(FILES ...) into devel.
#
# Plain install(), not rocm_install(): this directory must also configure
# standalone, where rocm-cmake was never loaded and those commands do not exist.
install(TARGETS rocsparse_fortran
        EXPORT rocsparse-fortran-targets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}/fortran/${ROCSPARSE_FORTRAN_COMPILER_DIR}"
                COMPONENT devel)
install(DIRECTORY "${_moddir}/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/fortran/${ROCSPARSE_FORTRAN_COMPILER_DIR}"
        COMPONENT devel
        FILES_MATCHING PATTERN "*.mod")

# The generated source, so that a user on a compiler ROCm ships no .mod for can
# rebuild the binding without fetching a monorepo to get one file. Source
# obtained separately can also be generated from a different ROCm than the
# installed librocsparse, which surfaces as an undefined symbol at link time or as
# an interface that silently differs from the one the caller meant; source that
# ships with the library cannot drift from it.
#
# Under share/, not include/. The .mod is compiler-specific and this is not, and
# every entry under include/fortran/ is partitioned by compiler, so a source
# file there would be read as naming one: the config below enumerates that
# directory to report which compilers are installed, and would list rocsparse.F90
# among them.
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/rocsparse.F90"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/rocsparse/fortran"
        COMPONENT devel)
install(EXPORT rocsparse-fortran-targets
        FILE rocsparse-fortran-targets.cmake
        NAMESPACE roc::
        DESTINATION "${_fortran_pkgdir}"
        COMPONENT devel)

# The two directories the config package has to name, kept out of the template
# so that neither side spells lib/ or include/ literally. A literal lib/fortran
# is wrong the moment CMAKE_INSTALL_LIBDIR is lib64, and the config then hands
# the consumer an IMPORTED_LOCATION that does not exist.
set(ROCSPARSE_FORTRAN_LIBDIR     "${CMAKE_INSTALL_LIBDIR}/fortran")
set(ROCSPARSE_FORTRAN_INCLUDEDIR "${CMAKE_INSTALL_INCLUDEDIR}/fortran")

configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/rocsparse-fortran-config.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/rocsparse-fortran-config.cmake"
  INSTALL_DESTINATION "${_fortran_pkgdir}"
  # PATH_VARS is exactly what these two want: @PACKAGE_INIT@ rewrites each into
  # an absolute path computed from where the config file was FOUND, so the
  # package survives being relocated and gets lib64 right without the template
  # knowing which layout it was built for. ROCSPARSE_FORTRAN_COMPILER_DIR stays a
  # plain @-substitution on purpose -- it is a bare directory NAME, not a path,
  # and PATH_VARS would resolve it as one, under the install prefix.
  PATH_VARS ROCSPARSE_FORTRAN_LIBDIR ROCSPARSE_FORTRAN_INCLUDEDIR)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/rocsparse-fortran-config.cmake"
        DESTINATION "${_fortran_pkgdir}"
        COMPONENT devel)

# The config lands in lib/cmake/rocsparse, BESIDE the C package, because that is
# what would let rocsparse-config.cmake pick it up with a bare
# include(rocsparse-fortran-config.cmake OPTIONAL) once rocm_export_targets grows
# a hook for it: a Fortran binding installed after the C library would then be
# found at find_package(rocsparse) time, and ignored when absent.
#
# rocm_export_targets' existing INCLUDE argument is NOT that hook. It emits a
# bare, non-OPTIONAL include() and emits it BEFORE the targets file
# (ROCMInstallTargets.cmake writes INCLUDE entries first), so a Fortran config
# reached that way would run before roc::rocsparse exists. That change belongs in
# rocm-cmake.
#
# But find_package(rocsparse-fortran): the documented spelling, and the one the
# config's own header shows, searches lib/cmake/rocsparse-fortran*, which
# lib/cmake/rocsparse does not match, so on its own that placement makes the
# direct spelling fail with "could not find a package configuration file". A
# forwarder gives the search somewhere to land without duplicating the real
# config, which would also need its own copy of the targets file and would then
# be a second thing to keep in step.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/forward/rocsparse-fortran-config.cmake"
"# Generated forwarder. The real config lives beside the rocSPARSE C package, in
# lib/cmake/rocsparse, so that rocsparse-config.cmake can include it as a sibling.
# This file only exists so that find_package(rocsparse-fortran) resolves to the
# same place.
include(\"\${CMAKE_CURRENT_LIST_DIR}/../rocsparse/rocsparse-fortran-config.cmake\")
")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/forward/rocsparse-fortran-config.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/rocsparse-fortran"
        COMPONENT devel)

# A version file, so that find_package(rocsparse-fortran 5.1) resolves rather than
# failing with "could not find a configuration file compatible with requested
# version". SameMajorVersion because the module's bind(C) surface tracks the C
# API's compatibility promise.
#
# The version is read from THIS tree's top-level CMakeLists, not from
# rocsparse_VERSION. That variable looks like the obvious source and is a trap: in
# a standalone build it is set by the find_package(rocsparse) above, so it reports
# whatever rocSPARSE happens to be INSTALLED (4.5.0 from a stock /opt/rocm) rather
# than the version of the headers these bindings were generated from. The
# bindings ship with their source tree, so the source tree is the truth.
set(_fortran_version "")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../CMakeLists.txt")
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../CMakeLists.txt" _rocsparse_root)
  # rocSPARSE spells it set(VERSION_STRING "5.1.0") and then passes the variable
  # to rocm_setup_version, so match the literal assignment rather than the call.
  string(REGEX MATCH "set\\([ \t]*VERSION_STRING[ \t]+\"([0-9]+\\.[0-9]+\\.[0-9]+)\""
         _m "${_rocsparse_root}")
  if(CMAKE_MATCH_1)
    set(_fortran_version "${CMAKE_MATCH_1}")
  endif()
  unset(_rocsparse_root)
endif()

if(_fortran_version)
  write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/rocsparse-fortran-config-version.cmake"
    VERSION "${_fortran_version}"
    COMPATIBILITY SameMajorVersion)
  # Installed in BOTH package directories, and it has to be. CMake looks for
  # <pkg>-config-version.cmake NEXT TO the <pkg>-config.cmake it actually
  # resolved, not wherever the real one lives. find_package(rocsparse-fortran 5.1)
  # resolves the forwarder in lib/cmake/rocsparse-fortran, so a version file only
  # beside the real config in lib/cmake/rocsparse is never consulted, and the call
  # fails with "could not find a configuration file compatible with requested
  # version" even though the config is right there and the version matches.
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/rocsparse-fortran-config-version.cmake"
          DESTINATION "${_fortran_pkgdir}"
          COMPONENT devel)
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/rocsparse-fortran-config-version.cmake"
          DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/rocsparse-fortran"
          COMPONENT devel)
else()
  # Better no version file than a wrong one: without it an unversioned
  # find_package still succeeds, whereas a version file claiming the wrong
  # number makes a versioned one succeed and link the wrong .mod.
  message(STATUS
    "rocSPARSE: could not determine the version for the Fortran package, so no "
    "rocsparse-fortran-config-version.cmake is installed; "
    "find_package(rocsparse-fortran <version>) will not match.")
endif()
