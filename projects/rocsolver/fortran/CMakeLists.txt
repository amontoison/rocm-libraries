# Fortran bindings for rocSOLVER.
#
# rocsolver.F90 is GENERATED from the rocSOLVER C headers by the rocm-fortran
# generator and committed here, so that a header change and its binding travel in
# the same pull request. Do not edit it by hand: regenerate it instead.
#
# It is one Fortran module, `rocsolver`: the bind(C) interfaces, the enum
# constants and the rocsolverCheck status helper. Every handle is a bare
# type(c_ptr), so there is no wrapper type to declare.
#
# UNLIKE every other binding in this monorepo, it is not self-contained at
# COMPILE time: rocsolver-functions.h includes rocblas.h and the API is spelled
# in rocBLAS enumerators, so the module says `use rocblas` and rocblas.mod, built
# by the SAME compiler, must exist before this archive can be compiled. That
# dependency is PUBLIC -- a consumer's `use rocsolver` needs rocblas.mod too.
# librocsolver, in contrast, is needed only to LINK a final executable.

cmake_minimum_required(VERSION 3.19)

# Standalone, this directory is its own project, so the bindings can be built and
# packaged on their own -- the supported path for a Fortran compiler other than
# the amdflang ROCm ships them for.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(rocsolver-fortran LANGUAGES NONE)
endif()

# GUARDED, so a site with no Fortran compiler still builds the C library.
# "${UNIX}" rather than ON because ROCm ships no Fortran compiler on Windows at
# all (TheRock adds flang only in the non-WIN32 branch of
# compiler/pre_hook_amd-llvm.cmake), so the probe falls through to a stray
# gfortran and enable_language(Fortran) then fails to identify it. Only a
# default: -DBUILD_FORTRAN_BINDINGS=ON still works wherever a compiler exists.
option(BUILD_FORTRAN_BINDINGS "Build the rocSOLVER Fortran bindings" "${UNIX}")

# The binding tests and the Fortran clients: one switch rather than two. Asking
# for them when the bindings were not built is a skip rather than an error.
option(BUILD_FORTRAN_CLIENTS "Build the rocSOLVER Fortran clients and binding tests" ON)

# Both options are ONE cache entry shared by every library in the monorepo, so
# -DBUILD_FORTRAN_BINDINGS=OFF from the top turns the whole Fortran track off in
# one flag; each library also honours a name of its own, which overrides it.
#
# For the clients that per-library name only SELECTS the Fortran tests within a
# build already producing clients: they are GPU test executables and cannot rise
# above rocSOLVER's own client/test switch. That gate sits further down, next to
# where the tests are added.
if(DEFINED ROCSOLVER_BUILD_FORTRAN_BINDINGS)
  set(BUILD_FORTRAN_BINDINGS ${ROCSOLVER_BUILD_FORTRAN_BINDINGS})
endif()
if(DEFINED ROCSOLVER_BUILD_FORTRAN_CLIENTS)
  set(BUILD_FORTRAN_CLIENTS ${ROCSOLVER_BUILD_FORTRAN_CLIENTS})
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
# interface at all three tiers. For rocSOLVER that subset is empty: the option
# is accepted for uniformity with the other bindings but has no effect.
set(FORTRAN_ARRAY_INTERFACES "assumed-shape" CACHE STRING
    "Array-argument overloads to expose: none, assumed-shape, or assumed-rank")
set_property(CACHE FORTRAN_ARRAY_INTERFACES PROPERTY STRINGS
             none assumed-shape assumed-rank)

# Report to the parent whether the bindings were really built; ROCM_HAVE_FORTRAN
# says only that a compiler exists. Guarded because standalone this is the
# top-level scope, where PARENT_SCOPE warns under -Wdev -- a hard failure under
# -Werror=dev.
if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(ROCSOLVER_HAVE_FORTRAN_BINDINGS FALSE PARENT_SCOPE)
endif()

if(NOT BUILD_FORTRAN_BINDINGS)
  return()
endif()

# An enclosing superbuild may probe for a Fortran compiler ONCE and publish
# ROCM_HAVE_FORTRAN (ROCM_LIBS_HAVE_FORTRAN is a legacy spelling); the shared,
# repo-neutral name keeps this branch uniform across the bindings. The
# rocm-libraries superbuild does not build rocSOLVER yet (its root stops with a
# FATAL_ERROR if asked to), so today neither variable is set here and the local
# probe below is the path that always runs.
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
  message(STATUS "rocSOLVER: no Fortran compiler found, skipping the Fortran bindings")
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

# The rocBLAS binding, the one non-optional dependency: the module says
# `use rocblas`, so rocblas.mod must exist before rocsolver.F90 can be compiled.
# The target normally comes from find_package below; the TARGET test only skips
# the search when an enclosing project has already defined it.
# PATH_SUFFIXES lib/cmake/rocblas is where the real config lives, beside the C
# package; the forwarder in lib/cmake/rocblas-fortran covers the plain spelling.
# Resolved BEFORE the target is created, and a skip rather than a hard error: a
# missing optional binding must not take the C library down with it.
if(NOT TARGET roc::rocblas_fortran)
  find_package(rocblas-fortran QUIET CONFIG PATH_SUFFIXES lib/cmake/rocblas)
endif()
if(NOT TARGET roc::rocblas_fortran)
  message(STATUS
    "rocSOLVER: the rocBLAS Fortran binding was not found, skipping the "
    "rocSOLVER Fortran bindings. They are written against it (`use rocblas`), "
    "so rocblas.mod must be built with ${CMAKE_Fortran_COMPILER} and reachable "
    "through CMAKE_PREFIX_PATH.")
  return()
endif()

add_library(rocsolver_fortran STATIC rocsolver.F90)

# roc::, matching the namespace rocSOLVER exports its C library under, so that a
# consumer is not made to remember two namespaces for one package.
add_library(roc::rocsolver_fortran ALIAS rocsolver_fortran)

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
set(ROCSOLVER_FORTRAN_COMPILER_DIR "${_fc_name}" CACHE STRING
    "Per-compiler subdirectory the .mod and the archive install under")

set(_moddir "${CMAKE_CURRENT_BINARY_DIR}/include")
set_target_properties(rocsolver_fortran PROPERTIES
  Fortran_MODULE_DIRECTORY "${_moddir}"
  # Kept for uniform handling across the bindings, and harmless: rocsolver.F90
  # carries no preprocessor guards (and no array overloads) today.
  Fortran_PREPROCESS ON
  # The .F90 default, but stating it lets CMake emit the flag the compiler in
  # use expects rather than relying on the extension.
  Fortran_FORMAT FREE
  POSITION_INDEPENDENT_CODE ON)

# The tri-state as a macro, defined for uniformity with the other bindings; the
# option value and the macro name are the same word, and none defines neither.
# rocsolver.F90 tests neither macro -- the generator emits no array overloads for
# rocSOLVER -- so FORTRAN_ARRAY_INTERFACES has no effect on this module.
if(FORTRAN_ARRAY_INTERFACES STREQUAL "assumed-shape")
  target_compile_definitions(rocsolver_fortran PRIVATE USE_ASSUMED_SHAPE=1)
elseif(FORTRAN_ARRAY_INTERFACES STREQUAL "assumed-rank")
  target_compile_definitions(rocsolver_fortran PRIVATE USE_ASSUMED_RANK=1)
endif()

# No line-length flag: the generator wraps the emitted source at 112 columns,
# inside the 132 free-form Fortran guarantees.

target_include_directories(rocsolver_fortran PUBLIC
  "$<BUILD_INTERFACE:${_moddir}>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/fortran/${ROCSOLVER_FORTRAN_COMPILER_DIR}>")

# PUBLIC, not INTERFACE: a consumer that says `use rocsolver` needs rocblas.mod
# on its include path too, since the compiler follows the use-association.
target_link_libraries(rocsolver_fortran PUBLIC roc::rocblas_fortran)

# Link interface only: needed to link an executable, not to build this archive.
# Standalone it has to be looked for -- testing for the target without ever
# calling find_package would export no link interface at all, and every consumer
# would hit undefined references despite linking roc::rocsolver_fortran.
#
# rocsolver-config.cmake does find_dependency(hip), and hip-config.cmake reaches
# FindThreads, which hard-errors unless C or CXX is enabled, so C is enabled
# first; it compiles none of the sources here. QUIET, because the bindings
# compile against nothing and build fine with no rocSOLVER installed.
if(NOT TARGET roc::rocsolver)
  if(NOT CMAKE_C_COMPILER_LOADED AND NOT CMAKE_CXX_COMPILER_LOADED)
    enable_language(C)
  endif()
  find_package(rocsolver QUIET)
endif()

# Records whether the search produced a link interface; the config template turns
# it into the condition guarding its own find_dependency(rocsolver). Demanding
# the C package unconditionally would oblige a consumer to find a package this
# archive does not depend on. Set on BOTH branches, to an explicit TRUE or FALSE:
# the template substitutes the value straight into an if(), and an unset variable
# would leave a bare if() that every consumer's find_package dies on.
if(TARGET roc::rocsolver)
  target_link_libraries(rocsolver_fortran INTERFACE roc::rocsolver)
  set(ROCSOLVER_FORTRAN_NEEDS_C_PACKAGE TRUE)
else()
  set(ROCSOLVER_FORTRAN_NEEDS_C_PACKAGE FALSE)
  message(STATUS
    "rocSOLVER: the C library was not found, so the Fortran bindings carry no "
    "link interface; a consumer will have to link it (-lrocsolver) itself.")
endif()

if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(ROCSOLVER_HAVE_FORTRAN_BINDINGS TRUE PARENT_SCOPE)
endif()

# The binding tests are GPU runtime executables, so in-tree they follow the
# library's own client/test switch; the condition below is the one rocSOLVER
# guards add_subdirectory(clients) with, so the two cannot disagree. Standalone
# that switch does not exist. Set as a plain variable: the cache entry is shared
# monorepo-wide, so turning it off here must stay scoped here.
#
# Placed immediately before BUILD_FORTRAN_CLIENTS is read, not up with the
# option() calls: a configure that never reaches this point has already
# returned, and would otherwise name the wrong reason for the skip.
if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR
   AND BUILD_FORTRAN_CLIENTS
   AND NOT (BUILD_CLIENTS_TESTS OR BUILD_CLIENTS_BENCHMARKS OR BUILD_CLIENTS_SAMPLES))
  message(STATUS
    "rocSOLVER: clients are not being built, skipping the Fortran binding tests.")
  set(BUILD_FORTRAN_CLIENTS OFF)
endif()

if(BUILD_FORTRAN_CLIENTS)
  # Enough standalone, but NOT in-tree on its own: CMake writes the chain of
  # CTestTestfile.cmake that ctest walks only when testing was enabled at the TOP
  # level. rocSOLVER's root calls enable_testing() only under BUILD_TESTING (the
  # variable include(CTest) sets as well), so without it `ctest` from the top
  # reports "Total Tests: 0". Hence the note, in exactly that case.
  enable_testing()
  if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR AND NOT BUILD_TESTING)
    message(STATUS
      "rocSOLVER: the Fortran binding tests are registered, but BUILD_TESTING is "
      "OFF, so testing is not enabled at the top level and `ctest` from the "
      "build root will not see them. Configure with -DBUILD_TESTING=ON, or run "
      "ctest from ${CMAKE_CURRENT_BINARY_DIR}/test.")
  endif()
  add_subdirectory(test)
endif()

include(CMakePackageConfigHelpers)

# Beside the C package, so a single CMAKE_PREFIX_PATH entry finds both. rocRAND
# and hipRAND cannot do this -- their <lib>-config.cmake carries a bare unguarded
# include() of a sibling <lib>-fortran-config.cmake, so a file there would
# recurse -- but rocsolver-config.cmake mentions Fortran nowhere.
set(_fortran_pkgdir "${CMAKE_INSTALL_LIBDIR}/cmake/rocsolver")

# COMPONENT devel on every rule below: everything installed here is a development
# artifact, and devel is what rocm-cmake's install helpers default to -- without
# it these land in the runtime component. Plain install(), not rocm_install():
# this directory must also configure standalone, without rocm-cmake.
install(TARGETS rocsolver_fortran
        EXPORT rocsolver-fortran-targets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}/fortran/${ROCSOLVER_FORTRAN_COMPILER_DIR}"
                COMPONENT devel)
install(DIRECTORY "${_moddir}/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/fortran/${ROCSOLVER_FORTRAN_COMPILER_DIR}"
        COMPONENT devel
        FILES_MATCHING PATTERN "*.mod")

# The generated source, so that a user on a compiler ROCm ships no .mod for can
# rebuild the binding without fetching a monorepo, from source that cannot drift
# from the installed librocsolver. Under share/, not include/: every entry under
# include/fortran/ is read as naming a compiler -- the config below enumerates
# that directory to report which are installed, and would list rocsolver.F90.
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/rocsolver.F90"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/rocsolver/fortran"
        COMPONENT devel)
install(EXPORT rocsolver-fortran-targets
        FILE rocsolver-fortran-targets.cmake
        NAMESPACE roc::
        DESTINATION "${_fortran_pkgdir}"
        COMPONENT devel)

# Kept out of the template so neither side spells lib/ or include/ literally.
set(ROCSOLVER_FORTRAN_LIBDIR     "${CMAKE_INSTALL_LIBDIR}/fortran")
set(ROCSOLVER_FORTRAN_INCLUDEDIR "${CMAKE_INSTALL_INCLUDEDIR}/fortran")

configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/rocsolver-fortran-config.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/rocsolver-fortran-config.cmake"
  INSTALL_DESTINATION "${_fortran_pkgdir}"
  # PATH_VARS: @PACKAGE_INIT@ rewrites each into an absolute path computed from
  # where the config was FOUND, so the package is relocatable and gets lib64
  # right. ROCSOLVER_FORTRAN_COMPILER_DIR stays a plain @-substitution: it is a
  # bare directory NAME, and PATH_VARS would resolve it under the prefix.
  PATH_VARS ROCSOLVER_FORTRAN_LIBDIR ROCSOLVER_FORTRAN_INCLUDEDIR)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/rocsolver-fortran-config.cmake"
        DESTINATION "${_fortran_pkgdir}"
        COMPONENT devel)

# find_package(rocsolver-fortran), the documented spelling, searches
# lib/cmake/rocsolver-fortran*, which the directory above does not match. A
# forwarder gives that search somewhere to land without duplicating the real
# config and its targets file.
#
# rocm_export_targets' INCLUDE argument is not an alternative: it emits a
# non-OPTIONAL include(), and emits it BEFORE the targets file, so a Fortran
# config reached that way would run before roc::rocsolver exists.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/forward/rocsolver-fortran-config.cmake"
"# Generated forwarder. The real config lives beside the rocSOLVER C package, in
# lib/cmake/rocsolver, so one CMAKE_PREFIX_PATH entry finds both.
# This file only exists so that find_package(rocsolver-fortran) resolves to the
# same place.
include(\"\${CMAKE_CURRENT_LIST_DIR}/../rocsolver/rocsolver-fortran-config.cmake\")
")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/forward/rocsolver-fortran-config.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/rocsolver-fortran"
        COMPONENT devel)

# A version file, so that find_package(rocsolver-fortran 3.38) resolves.
# SameMajorVersion because the bind(C) surface tracks the C API's promise.
#
# Read from THIS tree's top-level CMakeLists, not from rocsolver_VERSION: in a
# standalone build that variable is set by the find_package(rocsolver) above and
# reports whatever rocSOLVER is INSTALLED, not the version the bindings were
# generated from.
set(_fortran_version "")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../CMakeLists.txt")
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../CMakeLists.txt" _rocsolver_root)
  # rocSOLVER spells it set(VERSION_STRING "3.38.0"), so match the assignment
  # rather than the rocm_setup_version call it is passed to.
  string(REGEX MATCH "set\\([ \t]*VERSION_STRING[ \t]+\"([0-9]+\\.[0-9]+\\.[0-9]+)\""
         _m "${_rocsolver_root}")
  if(CMAKE_MATCH_1)
    set(_fortran_version "${CMAKE_MATCH_1}")
  endif()
  unset(_rocsolver_root)
endif()

if(_fortran_version)
  write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/rocsolver-fortran-config-version.cmake"
    VERSION "${_fortran_version}"
    COMPATIBILITY SameMajorVersion)
  # BOTH directories: CMake looks for <pkg>-config-version.cmake NEXT TO the
  # config it actually resolved, and a versioned find_package(rocsolver-fortran)
  # resolves the forwarder.
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/rocsolver-fortran-config-version.cmake"
          DESTINATION "${_fortran_pkgdir}"
          COMPONENT devel)
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/rocsolver-fortran-config-version.cmake"
          DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/rocsolver-fortran"
          COMPONENT devel)
else()
  # Better no version file than a wrong one: an unversioned find_package still
  # succeeds, whereas a wrong number makes a versioned one link the wrong .mod.
  message(STATUS
    "rocSOLVER: could not determine the version for the Fortran package, so no "
    "rocsolver-fortran-config-version.cmake is installed; "
    "find_package(rocsolver-fortran <version>) will not match.")
endif()
