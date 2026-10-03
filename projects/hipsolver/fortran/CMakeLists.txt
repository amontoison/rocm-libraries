# Fortran bindings for hipSOLVER.
#
# hipsolver.F90 is GENERATED from the hipSOLVER C headers by the rocm-fortran
# generator and committed here, so that a header change and its binding travel in
# the same pull request. Do not edit it by hand: regenerate it instead.
#
# It is one self-contained Fortran module, `hipsolver`: the bind(C) interfaces,
# the enum constants and the hipsolverCheck status helper. Every handle and every
# opaque info object is a bare type(c_ptr), so there is no wrapper type to
# declare, and the Fortran-visible surface is the C one plus whichever array
# overloads the tri-state below selects.
#
# The directory builds with no dependency on the hipSOLVER C target, because a
# Fortran binding compiles against nothing. libhipsolver is needed only to LINK a
# final executable, and is carried as the target's link interface for that
# reason. This module replaced the hand-written library/src/hipsolver_module.f90,
# which the clients' Fortran shim now compiles against instead.

cmake_minimum_required(VERSION 3.19)

# Standalone, this directory is its own project, so the bindings can be built and
# packaged on their own -- the supported path for a Fortran compiler other than
# the amdflang ROCm ships them for.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(hipsolver-fortran LANGUAGES NONE)
endif()

# GUARDED, so a site with no Fortran compiler still builds the C library.
# "${UNIX}" rather than ON because ROCm ships no Fortran compiler on Windows at
# all (TheRock adds flang only in the non-WIN32 branch of
# compiler/pre_hook_amd-llvm.cmake), so the probe falls through to a stray
# gfortran and enable_language(Fortran) then fails to identify it. Only a
# default: -DBUILD_FORTRAN_BINDINGS=ON still works wherever a compiler exists.
#
# OFF on the CUDA backend, and that is about the GENERATED SOURCE, not the
# compiler: hipsolver.F90 is produced from the ROCm code path and no CI job
# builds or runs it against a cuSOLVER-backed libhipsolver, so defaulting ON
# there would publish an untested artefact. It has to be asked for by name.
#
# _hipsolver_cuda_backend is the root's own backend test, computed above its
# project() call and inherited here. Standalone the root never runs, so the same
# test is repeated below against the variables a standalone caller can set --
# without that repeat -DUSE_CUDA=ON would be ignored and the bindings would
# default ON on the CUDA backend.
#
# In-tree this option() call is the SECOND, so it is a no-op: the root's runs
# first, because its enable_language(Fortran) needs the answer before this
# directory is added. Both compute the same default from the same variable.
if(NOT DEFINED _hipsolver_cuda_backend)
  if(USE_CUDA OR "$ENV{HIP_PLATFORM}" STREQUAL "nvidia" OR "${HIP_PLATFORM}" STREQUAL "nvidia")
    set(_hipsolver_cuda_backend ON)
  else()
    set(_hipsolver_cuda_backend OFF)
  endif()
endif()

if(_hipsolver_cuda_backend)
  set(_hipsolver_fortran_default OFF)
else()
  set(_hipsolver_fortran_default "${UNIX}")
endif()
option(BUILD_FORTRAN_BINDINGS "Build the hipSOLVER Fortran bindings"
       "${_hipsolver_fortran_default}")
unset(_hipsolver_fortran_default)

# The binding tests and the Fortran clients: one switch rather than two. Asking
# for them when the bindings were not built is a skip rather than an error.
option(BUILD_FORTRAN_CLIENTS "Build the hipSOLVER Fortran clients and binding tests" ON)

# Both options are ONE cache entry shared by every library in the monorepo, so
# -DBUILD_FORTRAN_BINDINGS=OFF from the top turns the whole Fortran track off in
# one flag; each library also honours a name of its own, which overrides it.
#
# The two halves are not symmetric. HIPSOLVER_BUILD_FORTRAN_BINDINGS decides on
# its own whether this directory builds anything; HIPSOLVER_BUILD_FORTRAN_CLIENTS
# can only ever turn the binding tests OFF, because in-tree they are GPU test
# executables and cannot rise above hipSOLVER's own clients switch. That gate
# sits further down, just before add_subdirectory(test).
if(DEFINED HIPSOLVER_BUILD_FORTRAN_BINDINGS)
  set(BUILD_FORTRAN_BINDINGS ${HIPSOLVER_BUILD_FORTRAN_BINDINGS})
endif()
if(DEFINED HIPSOLVER_BUILD_FORTRAN_CLIENTS)
  set(BUILD_FORTRAN_CLIENTS ${HIPSOLVER_BUILD_FORTRAN_CLIENTS})
endif()

# Which array-argument overloads the module exposes. One tri-state rather than a
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
# overloads for; every other routine exposes the same plain type(c_ptr)
# interface at all three tiers.
set(FORTRAN_ARRAY_INTERFACES "assumed-shape" CACHE STRING
    "Array-argument overloads to expose: none, assumed-shape, or assumed-rank")
set_property(CACHE FORTRAN_ARRAY_INTERFACES PROPERTY STRINGS
             none assumed-shape assumed-rank)

# Report to the parent whether the bindings were really built; ROCM_HAVE_FORTRAN
# says only that a compiler exists. Guarded because standalone this is the
# top-level scope, where PARENT_SCOPE warns under -Wdev -- a hard failure under
# -Werror=dev.
if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(HIPSOLVER_HAVE_FORTRAN_BINDINGS FALSE PARENT_SCOPE)
endif()

if(NOT BUILD_FORTRAN_BINDINGS)
  return()
endif()

# Reached on the CUDA backend only when the default above was overridden by hand.
# Warn rather than fail: the bindings DO build and link there -- the public
# headers carry no CUDART_VERSION guard and library/src/nvidia_detail/ defines
# every symbol the module references -- so what is missing is evidence, not
# symbols, and an explicit request is honoured.
if(_hipsolver_cuda_backend)
  message(WARNING
    "hipSOLVER: the Fortran bindings were requested on the CUDA backend. "
    "hipsolver.F90 is generated from the ROCm code path and is not tested "
    "against a cuSOLVER-backed libhipsolver, which is why it is off by default "
    "there. Building it is supported; vouching for it is not.")
endif()

# The monorepo root probes for a Fortran compiler ONCE and publishes
# ROCM_HAVE_FORTRAN. The name is deliberately neither rocm-libraries' nor
# rocm-systems': hipSOLVER is regularly built inside a rocm-systems tree, and a
# repo-specific name would read the wrong variable. ROCM_LIBS_HAVE_FORTRAN is a
# legacy spelling, honoured until the rocm-libraries root adopts the new one.
# The local probe is the standalone path, where there is no root to ask.
if(DEFINED ROCM_HAVE_FORTRAN)
  set(_have_fortran ${ROCM_HAVE_FORTRAN})
elseif(DEFINED ROCM_LIBS_HAVE_FORTRAN)
  set(_have_fortran ${ROCM_LIBS_HAVE_FORTRAN})
else()
  include(CheckLanguage)
  include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/ROCmFortranCompiler.cmake")

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

  # MUST run before check_language(), which is a no-op once
  # CMAKE_Fortran_COMPILER is defined.
  #
  # This call is the STANDALONE path: in-tree the choice was already made by
  # hipSOLVER's root, before its own enable_language(Fortran) froze the answer
  # for the whole tree, and the macro then does nothing. Both sites go through
  # the one module so the policy cannot drift -- two copies would diverge
  # silently, since the build succeeds either way and the only visible difference
  # is which per-compiler directory the artifacts land in.
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

# Here rather than beside the install() rules, because the install layout is also
# baked into the target's INSTALL_INTERFACE include directory, set well before
# them. Nothing must hardcode lib/ or include/: a distro installing to lib64 gets
# CMAKE_INSTALL_LIBDIR=lib64, and a literal lib/fortran then names nothing.
include(GNUInstallDirs)

add_library(hipsolver_fortran STATIC hipsolver.F90)

# roc::, matching the namespace hipSOLVER exports its C library under, so that a
# consumer is not made to remember two namespaces for one package.
add_library(roc::hipsolver_fortran ALIAS hipsolver_fortran)

# A .mod is not portable across compilers, so artifacts install under a
# per-compiler directory and two compilers can coexist in one prefix. The name is
# the compiler a user types (amdflang, gfortran), not CMake's compiler ID, so the
# -I/-L paths in the documentation match a raw link line. Derived from the ID
# rather than from the basename of CMAKE_Fortran_COMPILER, which would be wrong
# for an MPI wrapper (mpif90), a versioned binary (gfortran-13) or Cray's ftn.
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
set(HIPSOLVER_FORTRAN_COMPILER_DIR "${_fc_name}" CACHE STRING
    "Per-compiler subdirectory the .mod and the archive install under")

set(_moddir "${CMAKE_CURRENT_BINARY_DIR}/include")
set_target_properties(hipsolver_fortran PROPERTIES
  Fortran_MODULE_DIRECTORY "${_moddir}"
  # The array overloads sit behind preprocessor guards.
  Fortran_PREPROCESS ON
  # The .F90 default, but stating it lets CMake emit the flag the compiler in
  # use expects rather than relying on the extension.
  Fortran_FORMAT FREE
  POSITION_INDEPENDENT_CODE ON)

# The tri-state as a macro; the option value and the macro name are the same
# word, and none defines neither. The generated source settles the mutual
# exclusion itself -- both guarded regions test USE_ASSUMED_RANK first and fall
# back to USE_ASSUMED_SHAPE -- so defining both is not an error. Checked with
# amdflang -cpp -DUSE_ASSUMED_SHAPE=1 -DUSE_ASSUMED_RANK=1 -fsyntax-only.
if(FORTRAN_ARRAY_INTERFACES STREQUAL "assumed-shape")
  target_compile_definitions(hipsolver_fortran PRIVATE USE_ASSUMED_SHAPE=1)
elseif(FORTRAN_ARRAY_INTERFACES STREQUAL "assumed-rank")
  target_compile_definitions(hipsolver_fortran PRIVATE USE_ASSUMED_RANK=1)
endif()

# No line-length flag: the generator wraps the emitted source at 112 columns,
# inside the 132 free-form Fortran guarantees.

target_include_directories(hipsolver_fortran PUBLIC
  "$<BUILD_INTERFACE:${_moddir}>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/fortran/${HIPSOLVER_FORTRAN_COMPILER_DIR}>")

# Link interface only: needed to link an executable, not to build this archive.
# Standalone it has to be looked for -- testing for the target without ever
# calling find_package would export no link interface at all, and every consumer
# would hit undefined references despite linking roc::hipsolver_fortran.
#
# hipsolver-config.cmake does find_dependency(HIP), and hip-config.cmake reaches
# FindThreads, which hard-errors unless C or CXX is enabled, so C is enabled
# first; it compiles none of the sources here. QUIET, because the bindings
# compile against nothing and build fine with no hipSOLVER installed.
if(NOT TARGET roc::hipsolver)
  if(NOT CMAKE_C_COMPILER_LOADED AND NOT CMAKE_CXX_COMPILER_LOADED)
    enable_language(C)
  endif()
  find_package(hipsolver QUIET)
endif()

# Records whether the search produced a link interface; the config template turns
# it into the condition guarding its own find_dependency(hipsolver). Demanding
# the C package unconditionally would oblige a consumer to find a package this
# archive does not depend on. Set on BOTH branches, to an explicit TRUE or FALSE:
# the template substitutes the value straight into an if(), and an unset variable
# would leave a bare if() that every consumer's find_package dies on.
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

# The binding tests are GPU runtime executables, so in-tree they follow the
# library's own client/test switch; the condition below is the one hipSOLVER
# guards add_subdirectory(clients) with, so the two cannot disagree. It is a
# floor: an explicit HIPSOLVER_BUILD_FORTRAN_CLIENTS=ON does not lift it.
# Standalone that switch does not exist. Set as a plain variable: the cache entry
# is shared monorepo-wide, so turning it off here must stay scoped here.
#
# Placed here rather than beside the option block, where it would run ahead of
# the early returns above and announce a decision about tests to a configure that
# is about to build no Fortran at all.
if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR
   AND BUILD_FORTRAN_CLIENTS
   AND NOT (BUILD_CLIENTS_SAMPLES OR BUILD_CLIENTS_TESTS OR BUILD_CLIENTS_BENCHMARKS))
  message(STATUS
    "hipSOLVER: clients are not being built, skipping the Fortran binding tests.")
  set(BUILD_FORTRAN_CLIENTS OFF)
endif()

if(BUILD_FORTRAN_CLIENTS)
  # Enough standalone, but NOT in-tree: CMake writes the chain of
  # CTestTestfile.cmake that ctest walks only when testing was enabled at the TOP
  # level, so `ctest` from the top reports "Total Tests: 0". Hence the warning.
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

# Beside the C package, so a single CMAKE_PREFIX_PATH entry finds both. rocRAND
# and hipRAND cannot do this -- their <lib>-config.cmake carries a bare unguarded
# include() of a sibling <lib>-fortran-config.cmake, so a file there would
# recurse -- but hipsolver-config.cmake mentions Fortran nowhere.
set(_fortran_pkgdir "${CMAKE_INSTALL_LIBDIR}/cmake/hipsolver")

# COMPONENT devel on every rule below: everything installed here is a development
# artifact, and devel is what rocm-cmake's install helpers default to -- without
# it these land in the runtime component. Plain install(), not rocm_install():
# this directory must also configure standalone, without rocm-cmake.
install(TARGETS hipsolver_fortran
        EXPORT hipsolver-fortran-targets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}/fortran/${HIPSOLVER_FORTRAN_COMPILER_DIR}"
                COMPONENT devel)
install(DIRECTORY "${_moddir}/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/fortran/${HIPSOLVER_FORTRAN_COMPILER_DIR}"
        COMPONENT devel
        FILES_MATCHING PATTERN "*.mod")

# The generated source, so that a user on a compiler ROCm ships no .mod for can
# rebuild the binding without fetching a monorepo, from source that cannot drift
# from the installed libhipsolver. Under share/, not include/: every entry under
# include/fortran/ is read as naming a compiler -- the config below enumerates
# that directory to report which are installed, and would list hipsolver.F90.
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/hipsolver.F90"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/hipsolver/fortran"
        COMPONENT devel)
install(EXPORT hipsolver-fortran-targets
        FILE hipsolver-fortran-targets.cmake
        NAMESPACE roc::
        DESTINATION "${_fortran_pkgdir}"
        COMPONENT devel)

# Kept out of the template so neither side spells lib/ or include/ literally.
set(HIPSOLVER_FORTRAN_LIBDIR     "${CMAKE_INSTALL_LIBDIR}/fortran")
set(HIPSOLVER_FORTRAN_INCLUDEDIR "${CMAKE_INSTALL_INCLUDEDIR}/fortran")

configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/hipsolver-fortran-config.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/hipsolver-fortran-config.cmake"
  INSTALL_DESTINATION "${_fortran_pkgdir}"
  # PATH_VARS: @PACKAGE_INIT@ rewrites each into an absolute path computed from
  # where the config was FOUND, so the package is relocatable and gets lib64
  # right. HIPSOLVER_FORTRAN_COMPILER_DIR stays a plain @-substitution: it is a
  # bare directory NAME, and PATH_VARS would resolve it under the prefix.
  PATH_VARS HIPSOLVER_FORTRAN_LIBDIR HIPSOLVER_FORTRAN_INCLUDEDIR)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/hipsolver-fortran-config.cmake"
        DESTINATION "${_fortran_pkgdir}"
        COMPONENT devel)

# find_package(hipsolver-fortran), the documented spelling, searches
# lib/cmake/hipsolver-fortran*, which the directory above does not match. A
# forwarder gives that search somewhere to land without duplicating the real
# config and its targets file.
#
# rocm_export_targets' INCLUDE argument is not an alternative: it emits a
# non-OPTIONAL include(), and emits it BEFORE the targets file, so a Fortran
# config reached that way would run before roc::hipsolver exists.
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

# A version file, so that find_package(hipsolver-fortran 3.8) resolves.
# SameMajorVersion because the bind(C) surface tracks the C API's promise.
#
# Read from THIS tree's top-level CMakeLists, not from hipsolver_VERSION: in a
# standalone build that variable is set by the find_package(hipsolver) above and
# reports whatever hipSOLVER is INSTALLED, not the version the bindings were
# generated from.
set(_fortran_version "")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../CMakeLists.txt")
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../CMakeLists.txt" _hipsolver_root)
  # hipSOLVER spells it set(VERSION_STRING "3.8.0"), so match the assignment
  # rather than the rocm_setup_version call it is passed to.
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
  # BOTH directories: CMake looks for <pkg>-config-version.cmake NEXT TO the
  # config it actually resolved, and a versioned find_package(hipsolver-fortran)
  # resolves the forwarder.
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/hipsolver-fortran-config-version.cmake"
          DESTINATION "${_fortran_pkgdir}"
          COMPONENT devel)
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/hipsolver-fortran-config-version.cmake"
          DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/hipsolver-fortran"
          COMPONENT devel)
else()
  # Better no version file than a wrong one: an unversioned find_package still
  # succeeds, whereas a wrong number makes a versioned one link the wrong .mod.
  message(STATUS
    "hipSOLVER: could not determine the version for the Fortran package, so no "
    "hipsolver-fortran-config-version.cmake is installed; "
    "find_package(hipsolver-fortran <version>) will not match.")
endif()
