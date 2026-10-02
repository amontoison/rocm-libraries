# Fortran bindings for hipSOLVER.
#
# hipsolver.F90 is GENERATED from the hipSOLVER C headers by the rocm-fortran
# generator and committed here, next to the headers it binds, so that a header
# change and its binding travel in the same pull request. Do not edit it by hand:
# regenerate it instead.
#
# The bindings are a single self-contained Fortran module, `hipsolver`, carrying
# the bind(C) interfaces, the enum constants and the hipsolverCheck status
# helper. A consumer writes one `use hipsolver`.
#
# The module declares no derived types at all: every hipSOLVER handle and every
# opaque info object is a bare type(c_ptr), and the status codes are enumerators
# rather than a wrapper type. So the Fortran-visible surface is the C one, plus
# whichever array overloads the tri-state below selects. That is the shape of
# the packaged track, not an omission -- the convenience layer a generator can
# also emit (strongly-typed wrappers, device-pointer specifics) is deliberately
# not part of what ROCm ships.
#
# This directory is self-contained: it builds with no dependency on the hipSOLVER
# C target, because a Fortran binding compiles against nothing (it only declares
# bind(C) interfaces). libhipsolver is required to LINK a final executable, and is
# carried as the target's link interface for that reason.
#
# This module REPLACES the hand-written binding that used to live in
# library/src (hipsolver_module.f90), removed in the same commit that brought
# this directory to its current shape. That one was shipped and built by default,
# and the clients' Fortran shim was compiled from it; the shim now compiles
# against this module instead and links the target exported here.

cmake_minimum_required(VERSION 3.19)

# Configured directly (not as a subdirectory of the hipSOLVER build), this
# directory is its own project, so that the bindings can be built and packaged
# on their own, which is the supported path for anyone whose Fortran compiler
# is not the amdflang the shipped bindings were built with. In-tree, hipSOLVER's
# own project() is already in effect.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(hipsolver-fortran LANGUAGES NONE)
endif()

# ON by default but GUARDED: a site with no Fortran compiler must still
# configure and build the C library. The monorepo root already probes once and
# publishes ROCM_HAVE_FORTRAN; a standalone hipSOLVER build falls back to its own
# probe.
#
# "${UNIX}" rather than ON, because Windows is not such a site: ROCm ships no
# Fortran compiler there at all. TheRock puts flang in LLVM_ENABLE_PROJECTS
# only in the non-WIN32 branch of compiler/pre_hook_amd-llvm.cmake, so the
# amdflang preference below finds nothing, the probe falls through to whatever
# gfortran happens to be on PATH, and enable_language(Fortran) then fails to
# identify it. hipSOLVER's own root already declares this option the same way;
# repeating it here keeps a standalone configure of this directory in step. A
# default and not a veto: -DBUILD_FORTRAN_BINDINGS=ON still works wherever a
# usable compiler exists.
#
# OFF on the CUDA backend, and that one is about the GENERATED SOURCE rather
# than about the compiler. hipsolver.F90 is produced from the ROCm code path and
# is exercised only there: no CI job builds or runs it against a cuSOLVER-backed
# libhipsolver, so nothing in this tree says the binding is correct on that
# backend. Shipping it on by default would publish an untested artefact, so it
# is off there and has to be asked for by name.
#
# _hipsolver_cuda_backend is the root's own backend test, computed above its
# project() call and inherited here, so the detection lives in one place.
# Standalone it is simply undefined and the default falls back to ${UNIX}, which
# is correct: a standalone build of the bindings has no backend of its own.
#
# This option() call is the SECOND in an in-tree build -- the root's runs first,
# because enable_language(Fortran) there needs the answer before this directory
# is added -- so it is a no-op then, and live only standalone. Both compute the
# same default from the same variable, so the two can never read as disagreeing.
if(_hipsolver_cuda_backend)
  set(_hipsolver_fortran_default OFF)
else()
  set(_hipsolver_fortran_default "${UNIX}")
endif()
option(BUILD_FORTRAN_BINDINGS "Build the hipSOLVER Fortran bindings"
       "${_hipsolver_fortran_default}")
unset(_hipsolver_fortran_default)

# The bindings' own test suite, and the Fortran clients where a library has
# them: one switch rather than two neighbouring ones. ON by default, and
# guarded the same way as the bindings, so asking for them when the bindings
# were not built is a skip rather than an error.
option(BUILD_FORTRAN_CLIENTS "Build the hipSOLVER Fortran clients and binding tests" ON)

# BUILD_FORTRAN_BINDINGS and BUILD_FORTRAN_CLIENTS are ONE cache entry shared by
# every library in the monorepo, which is what you want for the common case:
# -DBUILD_FORTRAN_BINDINGS=OFF from the top turns the whole Fortran track off in
# a single flag. It is not what you want when one library has to be singled out,
# so each library also honours a name of its own, which overrides the shared
# default.
#
# The two halves are not symmetric, and only one of them is a free choice.
# HIPSOLVER_BUILD_FORTRAN_BINDINGS decides on its own whether this directory
# builds anything. HIPSOLVER_BUILD_FORTRAN_CLIENTS can only ever turn the
# binding tests OFF: in-tree they are GPU test executables and belong to the
# library's client/test track, so hipSOLVER's own clients switch is a floor they
# cannot rise above, whatever either Fortran name says. See the gate further
# down, just before add_subdirectory(test).
if(DEFINED HIPSOLVER_BUILD_FORTRAN_BINDINGS)
  set(BUILD_FORTRAN_BINDINGS ${HIPSOLVER_BUILD_FORTRAN_BINDINGS})
endif()
if(DEFINED HIPSOLVER_BUILD_FORTRAN_CLIENTS)
  set(BUILD_FORTRAN_CLIENTS ${HIPSOLVER_BUILD_FORTRAN_CLIENTS})
endif()

# Which array-argument overloads the module exposes. One tri-state, rather than
# one boolean per tier, because the two upper tiers cannot legally coexist: an
# assumed-rank dummy is not distinguishable by rank from the per-rank specifics,
# so assumed-rank REPLACES the per-rank variants rather than adding to them.
#
# The tier applies only where there is an array to overload on. Array overloads
# are generated for the subset of routines the generator supports; every other
# routine exposes the plain type(c_ptr) interface under all three tiers, so for
# those the choice is invisible.
#
#   none           the raw type(c_ptr) surface only (Fortran 2003)
#   assumed-shape  + the per-rank dimension(:) specifics for those routines
#                  (Fortran 2008): the default, and what the bindings shipped
#                  in ROCm are built with
#   assumed-rank   + one dimension(..) specific in place of them (Fortran 2018)
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
  set(HIPSOLVER_HAVE_FORTRAN_BINDINGS FALSE PARENT_SCOPE)
endif()

if(NOT BUILD_FORTRAN_BINDINGS)
  return()
endif()

# Reached on the CUDA backend only when the default above was overridden by
# hand. Warn rather than fail: the bindings DO build and link there, since the
# public hipSOLVER headers carry no CUDART_VERSION guard on their entry points
# and library/src/nvidia_detail/ defines every symbol the module references.
# What is missing is evidence, not symbols, so an explicit request is honoured
# and the warning says exactly what the user is taking on.
if(_hipsolver_cuda_backend)
  message(WARNING
    "hipSOLVER: the Fortran bindings were requested on the CUDA backend. "
    "hipsolver.F90 is generated from the ROCm code path and is not tested "
    "against a cuSOLVER-backed libhipsolver, which is why it is off by default "
    "there. Building it is supported; vouching for it is not.")
endif()

# Probing for a Fortran compiler is not free, and every library in a monorepo
# build would otherwise repeat it, so the monorepo root probes ONCE and publishes
# the answer as ROCM_HAVE_FORTRAN. That name is deliberately neither
# rocm-libraries' nor rocm-systems': hipSOLVER is regularly built inside a
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
  include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/ROCmFortranCompiler.cmake")

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

  # Express the amdflang preference before probing: check_language() is a no-op
  # once CMAKE_Fortran_COMPILER is defined, so whatever this leaves behind is
  # what the probe reports, and the probe never overrides it.
  #
  # This call is the STANDALONE path. In-tree the choice was already made by
  # hipSOLVER's root CMakeLists, which has to make it before its own
  # enable_language(Fortran) freezes the answer for the whole tree, and the
  # macro then finds a compiler already set and does nothing. Both sites go
  # through the one module so that the two cannot drift apart: two copies of a
  # compiler-selection policy diverge silently, since the build succeeds either
  # way and the only visible difference is which per-compiler directory the
  # artifacts land in.
  rocm_fortran_prefer_amdflang()

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
  message(STATUS "hipSOLVER: no Fortran compiler found, skipping the Fortran bindings")
  return()
endif()

enable_language(Fortran)

if(NOT FORTRAN_ARRAY_INTERFACES MATCHES "^(none|assumed-shape|assumed-rank)$")
  message(FATAL_ERROR
    "FORTRAN_ARRAY_INTERFACES must be none, assumed-shape or assumed-rank, "
    "not '${FORTRAN_ARRAY_INTERFACES}'.")
endif()

# Pulled in here rather than beside the install() rules below because the
# install layout is also baked into the target's INSTALL_INTERFACE include
# directory, which is set well before them. Nothing must hardcode lib/ or
# include/: a distro that installs to lib64 gets CMAKE_INSTALL_LIBDIR=lib64,
# and a literal lib/fortran then names a directory that does not exist.
include(GNUInstallDirs)

add_library(hipsolver_fortran STATIC hipsolver.F90)

# roc::, matching the namespace hipSOLVER exports its C library under (see
# library/src/CMakeLists.txt), so that a consumer is not made to remember that
# two halves of one package sit in different namespaces.
add_library(roc::hipsolver_fortran ALIAS hipsolver_fortran)

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
# HIPSOLVER_FORTRAN_COMPILER_DIR itself, for example to gfortran-13.3.0.
set(HIPSOLVER_FORTRAN_COMPILER_DIR "${_fc_name}" CACHE STRING
    "Per-compiler subdirectory the .mod and the archive install under")

set(_moddir "${CMAKE_CURRENT_BINARY_DIR}/include")
set_target_properties(hipsolver_fortran PROPERTIES
  Fortran_MODULE_DIRECTORY "${_moddir}"
  # hipsolver.F90 is uppercase: the array overloads sit behind preprocessor
  # guards, so the source must go through the C preprocessor.
  Fortran_PREPROCESS ON
  # Free form is the .F90 default, but stating it lets CMake emit whichever flag
  # the compiler in use expects rather than relying on the extension.
  Fortran_FORMAT FREE
  POSITION_INDEPENDENT_CODE ON)

# Translate the tri-state into the macro the generated source is guarded with.
# The option value and the macro name are the same word, so there is nothing to
# remember: assumed-shape selects USE_ASSUMED_SHAPE, assumed-rank selects
# USE_ASSUMED_RANK, and none defines neither and leaves the raw type(c_ptr)
# surface.
#
# The if/elseif below defines exactly ONE of the two, and that is a requirement,
# not a tidiness preference. The tiers are mutually exclusive rather than
# nested, because an assumed-rank dummy is not distinguishable by rank from the
# per-rank specifics and the two cannot legally coexist in one generic.
#
# Defining both is not a configuration this file can produce, and the generated
# source does not treat it as an error either: both guarded regions -- the
# interface blocks and the module procedures under CONTAINS -- test
# USE_ASSUMED_RANK first, so assumed-rank wins and the module compiles. Checked
# with amdflang -cpp -DUSE_ASSUMED_SHAPE=1 -DUSE_ASSUMED_RANK=1 -fsyntax-only.
if(FORTRAN_ARRAY_INTERFACES STREQUAL "assumed-shape")
  target_compile_definitions(hipsolver_fortran PRIVATE USE_ASSUMED_SHAPE=1)
elseif(FORTRAN_ARRAY_INTERFACES STREQUAL "assumed-rank")
  target_compile_definitions(hipsolver_fortran PRIVATE USE_ASSUMED_RANK=1)
endif()

# No line-length flag: the generator wraps the emitted source at 112 columns,
# inside the 132 that free-form Fortran guarantees, so every compiler reads it
# without a vendor-specific option.

target_include_directories(hipsolver_fortran PUBLIC
  "$<BUILD_INTERFACE:${_moddir}>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/fortran/${HIPSOLVER_FORTRAN_COMPILER_DIR}>")

# Link interface only: needed to link an executable, not to build this archive.
# roc::hipsolver is defined by hipSOLVER's library/, and brings the HIP runtime
# with it. Standalone it has to be looked for, and testing for the target without
# ever calling find_package would mean it is never found: the archive would then
# export no link interface at all, and every consumer would hit undefined
# references to hipsolverCreate and friends despite linking
# roc::hipsolver_fortran exactly as documented.
#
# hipsolver-config.cmake does find_dependency(HIP), and hip-config.cmake reaches
# FindThreads, which hard-errors unless C or CXX is enabled. This project
# declares LANGUAGES NONE and then enables only Fortran, so C is enabled first;
# it compiles none of the sources here. QUIET, because a site that has no
# installed hipSOLVER can still legitimately build the bindings: they compile
# against nothing.
if(NOT TARGET roc::hipsolver)
  if(NOT CMAKE_C_COMPILER_LOADED AND NOT CMAKE_CXX_COMPILER_LOADED)
    enable_language(C)
  endif()
  find_package(hipsolver QUIET)
endif()

# Whether the archive really carries roc::hipsolver in its link interface is
# also what the installed config package has to know, and it cannot work it out
# for itself: it sees only the targets file. So record it here and interpolate
# it into the template. The consumer-side find_dependency(hipsolver) exists
# solely to make roc::hipsolver exist before the targets file names it; a
# binding built with no C library in sight names nothing, and making its
# consumers find a package the artifact does not depend on would be an invented
# requirement. Written as the literal TRUE/FALSE the template's if() reads: an
# unset variable would interpolate to nothing and leave a bare if() in the
# installed config, which is a syntax error in every consumer's find_package.
if(TARGET roc::hipsolver)
  target_link_libraries(hipsolver_fortran INTERFACE roc::hipsolver)
  set(HIPSOLVER_FORTRAN_NEEDS_C_PACKAGE TRUE)
else()
  set(HIPSOLVER_FORTRAN_NEEDS_C_PACKAGE FALSE)
  message(STATUS
    "hipSOLVER: the C library was not found, so the Fortran bindings carry no "
    "link interface; a consumer will have to link it (-lhipsolver) itself.")
endif()

if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(HIPSOLVER_HAVE_FORTRAN_BINDINGS TRUE PARENT_SCOPE)
endif()

# The binding tests below are GPU runtime executables, so in-tree they belong to
# the client/test track and have to follow the library's own switch: a
# library-only build has no business producing test binaries, and would be made
# to link a GPU runtime it never asked for. The condition is the one hipSOLVER
# guards add_subdirectory(clients) with, so the two cannot disagree. It is a
# floor, not a default: an explicit HIPSOLVER_BUILD_FORTRAN_CLIENTS=ON does not
# lift it, because the executables it asks for belong to a track that is not
# being built at all.
#
# Standalone, this directory IS the build and the tests are the point, so the
# library's switch does not exist and BUILD_FORTRAN_CLIENTS stands on its own.
# Setting it as a plain variable is deliberate: the cache entry is one switch
# shared by every library in the monorepo, so turning it off here has to be
# scoped to this directory and must not be written back.
#
# Placed here rather than beside the option block, because up there it would run
# ahead of the two early returns above and announce a decision about tests to a
# configure that is about to build no Fortran whatsoever.
if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR
   AND BUILD_FORTRAN_CLIENTS
   AND NOT (BUILD_CLIENTS_SAMPLES OR BUILD_CLIENTS_TESTS OR BUILD_CLIENTS_BENCHMARKS))
  message(STATUS
    "hipSOLVER: clients are not being built, skipping the Fortran binding tests.")
  set(BUILD_FORTRAN_CLIENTS OFF)
endif()

if(BUILD_FORTRAN_CLIENTS)
  # enable_testing() here is enough for a STANDALONE build of this directory, but
  # NOT for an in-tree one: CMake only writes the chain of CTestTestfile.cmake
  # that ctest walks down from a build root when testing was enabled at the TOP
  # level. Enabled only here, the tests are built and registered, and `ctest` run
  # from the top of the hipSOLVER build reports "Total Tests: 0": a silent
  # nothing, which is the worst way to find out. So say it.
  enable_testing()
  if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    get_property(_top_testing GLOBAL PROPERTY CTEST_TARGETS_ADDED)
    if(NOT _top_testing)
      message(STATUS
        "hipSOLVER: the Fortran binding tests are registered, but ctest discovers "
        "tests only from a build root where testing was enabled at the top "
        "level. If `ctest` there reports no tests, add include(CTest) or "
        "enable_testing() to the top-level CMakeLists, or run ctest from "
        "${CMAKE_CURRENT_BINARY_DIR}/test.")
    endif()
  endif()
  add_subdirectory(test)
endif()

include(CMakePackageConfigHelpers)

# lib/cmake/hipsolver is where hipSOLVER's own rocm_export_targets() puts
# hipsolver-config.cmake, so the Fortran package lands beside the C one and a
# single CMAKE_PREFIX_PATH entry finds both.
#
# rocRAND and hipRAND cannot do this and install into lib/cmake/<lib>-fortran
# alone, because their shipped <lib>-config.cmake carries a bare unguarded
# include() of a sibling <lib>-fortran-config.cmake, and putting a file there
# would recurse. hipSOLVER has no such include, checked against an installed
# ROCm: hipsolver-config.cmake mentions Fortran nowhere, so it uses the shared
# layout, like hipBLAS, rocSPARSE and the FFT libraries.
set(_fortran_pkgdir "${CMAKE_INSTALL_LIBDIR}/cmake/hipsolver")

# COMPONENT devel on every rule below, because everything this directory
# installs -- a static archive, a set of .mod files, the generated source and
# the CMake package -- is a development artifact: nothing here is needed to RUN
# a program, only to build one against the bindings. devel is also what
# rocm-cmake would have chosen: rocm_install_targets and rocm_install default to
# it. Without it these files land in the runtime component instead.
#
# Plain install(), not rocm_install(): this directory must also configure
# standalone, where rocm-cmake was never loaded and those commands do not exist.
install(TARGETS hipsolver_fortran
        EXPORT hipsolver-fortran-targets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}/fortran/${HIPSOLVER_FORTRAN_COMPILER_DIR}"
                COMPONENT devel)
install(DIRECTORY "${_moddir}/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/fortran/${HIPSOLVER_FORTRAN_COMPILER_DIR}"
        COMPONENT devel
        FILES_MATCHING PATTERN "*.mod")

# The generated source, so that a user on a compiler ROCm ships no .mod for can
# rebuild the binding without fetching a monorepo to get one file. Source
# obtained separately can also be generated from a different ROCm than the
# installed libhipsolver, which surfaces as an undefined symbol at link time or
# as an interface that silently differs from the one the caller meant; source
# that ships with the library cannot drift from it.
#
# Under share/, not include/. The .mod is compiler-specific and this is not, and
# every entry under include/fortran/ is partitioned by compiler, so a source
# file there would be read as naming one: the config below enumerates that
# directory to report which compilers are installed, and would list hipsolver.F90
# among them.
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/hipsolver.F90"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/hipsolver/fortran"
        COMPONENT devel)
install(EXPORT hipsolver-fortran-targets
        FILE hipsolver-fortran-targets.cmake
        NAMESPACE roc::
        DESTINATION "${_fortran_pkgdir}"
        COMPONENT devel)

# The two directories the config package has to name, kept out of the template
# so that neither side spells lib/ or include/ literally. A literal lib/fortran
# is wrong the moment CMAKE_INSTALL_LIBDIR is lib64, and the config then hands
# the consumer an IMPORTED_LOCATION that does not exist.
set(HIPSOLVER_FORTRAN_LIBDIR     "${CMAKE_INSTALL_LIBDIR}/fortran")
set(HIPSOLVER_FORTRAN_INCLUDEDIR "${CMAKE_INSTALL_INCLUDEDIR}/fortran")

configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/hipsolver-fortran-config.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/hipsolver-fortran-config.cmake"
  INSTALL_DESTINATION "${_fortran_pkgdir}"
  # PATH_VARS is exactly what these two want: @PACKAGE_INIT@ rewrites each into
  # an absolute path computed from where the config file was FOUND, so the
  # package survives being relocated and gets lib64 right without the template
  # knowing which layout it was built for. HIPSOLVER_FORTRAN_COMPILER_DIR stays a
  # plain @-substitution on purpose -- it is a bare directory NAME, not a path,
  # and PATH_VARS would resolve it as one, under the install prefix.
  PATH_VARS HIPSOLVER_FORTRAN_LIBDIR HIPSOLVER_FORTRAN_INCLUDEDIR)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/hipsolver-fortran-config.cmake"
        DESTINATION "${_fortran_pkgdir}"
        COMPONENT devel)

# The config lands in lib/cmake/hipsolver, BESIDE the C package, because that is
# what would let hipsolver-config.cmake pick it up with a bare
# include(hipsolver-fortran-config.cmake OPTIONAL) once rocm_export_targets grows
# a hook for it: a Fortran binding installed after the C library would then be
# found at find_package(hipsolver) time, and ignored when absent.
#
# rocm_export_targets' existing INCLUDE argument is NOT that hook. It emits a
# bare, non-OPTIONAL include() and emits it BEFORE the targets file
# (ROCMInstallTargets.cmake writes INCLUDE entries first), so a Fortran config
# reached that way would run before roc::hipsolver exists. That change belongs in
# rocm-cmake.
#
# But find_package(hipsolver-fortran): the documented spelling, and the one the
# config's own header shows, searches lib/cmake/hipsolver-fortran*, which
# lib/cmake/hipsolver does not match, so on its own that placement makes the
# direct spelling fail with "could not find a package configuration file". A
# forwarder gives the search somewhere to land without duplicating the real
# config, which would also need its own copy of the targets file and would then
# be a second thing to keep in step.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/forward/hipsolver-fortran-config.cmake"
"# Generated forwarder. The real config lives beside the hipSOLVER C package, in
# lib/cmake/hipsolver, so that hipsolver-config.cmake can include it as a sibling.
# This file only exists so that find_package(hipsolver-fortran) resolves to the
# same place.
include(\"\${CMAKE_CURRENT_LIST_DIR}/../hipsolver/hipsolver-fortran-config.cmake\")
")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/forward/hipsolver-fortran-config.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/hipsolver-fortran"
        COMPONENT devel)

# A version file, so that find_package(hipsolver-fortran 3.8) resolves rather than
# failing with "could not find a configuration file compatible with requested
# version". SameMajorVersion because the module's bind(C) surface tracks the C
# API's compatibility promise.
#
# The version is read from THIS tree's top-level CMakeLists, not from
# hipsolver_VERSION. That variable looks like the obvious source and is a trap: in
# a standalone build it is set by the find_package(hipsolver) above, so it reports
# whatever hipSOLVER happens to be INSTALLED rather than the version of the
# headers these bindings were generated from. The bindings ship with their source
# tree, so the source tree is the truth.
set(_fortran_version "")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../CMakeLists.txt")
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../CMakeLists.txt" _hipsolver_root)
  # hipSOLVER spells it set(VERSION_STRING "3.8.0") and then passes the variable
  # to rocm_setup_version, so match the literal assignment rather than the call.
  string(REGEX MATCH "set\\([ \t]*VERSION_STRING[ \t]+\"([0-9]+\\.[0-9]+\\.[0-9]+)\""
         _m "${_hipsolver_root}")
  if(CMAKE_MATCH_1)
    set(_fortran_version "${CMAKE_MATCH_1}")
  endif()
  unset(_hipsolver_root)
endif()

if(_fortran_version)
  write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/hipsolver-fortran-config-version.cmake"
    VERSION "${_fortran_version}"
    COMPATIBILITY SameMajorVersion)
  # Installed in BOTH package directories, and it has to be. CMake looks for
  # <pkg>-config-version.cmake NEXT TO the <pkg>-config.cmake it actually
  # resolved, not wherever the real one lives. find_package(hipsolver-fortran 3.8)
  # resolves the forwarder in lib/cmake/hipsolver-fortran, so a version file only
  # beside the real config in lib/cmake/hipsolver is never consulted, and the call
  # fails with "could not find a configuration file compatible with requested
  # version" even though the config is right there and the version matches.
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/hipsolver-fortran-config-version.cmake"
          DESTINATION "${_fortran_pkgdir}"
          COMPONENT devel)
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/hipsolver-fortran-config-version.cmake"
          DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/hipsolver-fortran"
          COMPONENT devel)
else()
  # Better no version file than a wrong one: without it an unversioned
  # find_package still succeeds, whereas a version file claiming the wrong
  # number makes a versioned one succeed and link the wrong .mod.
  message(STATUS
    "hipSOLVER: could not determine the version for the Fortran package, so no "
    "hipsolver-fortran-config-version.cmake is installed; "
    "find_package(hipsolver-fortran <version>) will not match.")
endif()
