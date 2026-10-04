# Fortran bindings for rocFFT.
#
# rocfft.F90 is GENERATED from the rocFFT C headers by the rocm-fortran generator
# and committed here, so that a header change and its binding travel in the same
# pull request. Do not edit it by hand: regenerate it instead.
#
# It is one self-contained Fortran module, `rocfft`: the bind(C) interfaces, the
# enum constants and the rocfftCheck status helper. It declares no derived type
# of its own, so rocfft_plan, rocfft_plan_description, rocfft_execution_info,
# rocfft_field and rocfft_brick each cross the boundary as a plain type(c_ptr),
# which is what the C headers make them.
#
# The directory builds with no dependency on the rocFFT C target, because a
# Fortran binding compiles against nothing. librocfft is needed only to LINK a
# final executable, and is carried as the target's link interface for that
# reason.

cmake_minimum_required(VERSION 3.19)

# Standalone, this directory is its own project, so the bindings can be built and
# packaged on their own.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(rocfft-fortran LANGUAGES NONE)
endif()

# GUARDED, so a site with no Fortran compiler still builds the C library.
# "${UNIX}" rather than ON because ROCm ships no Fortran compiler on Windows at
# all (TheRock adds flang only in the non-WIN32 branch of
# compiler/pre_hook_amd-llvm.cmake), so the probe falls through to a stray
# gfortran and enable_language(Fortran) then fails to identify it. Only a
# default: -DBUILD_FORTRAN_BINDINGS=ON still works wherever a compiler exists.
option(BUILD_FORTRAN_BINDINGS "Build the rocFFT Fortran bindings" "${UNIX}")

# The binding tests and the Fortran clients: one switch rather than two. Asking
# for them when the bindings were not built is a skip rather than an error.
option(BUILD_FORTRAN_CLIENTS "Build the rocFFT Fortran clients and binding tests" ON)

# Both options are ONE cache entry shared by every library in the monorepo, so
# -DBUILD_FORTRAN_BINDINGS=OFF from the top turns the whole Fortran track off in
# one flag; each library also honours a name of its own, which overrides it.
#
# That override is the last word for the BINDINGS only. In-tree the binding tests
# are GPU test executables and cannot rise above rocFFT's own client switch: the
# gate further down turns BUILD_FORTRAN_CLIENTS off in a library-only build
# whatever ROCFFT_BUILD_FORTRAN_CLIENTS says.
if(DEFINED ROCFFT_BUILD_FORTRAN_BINDINGS)
  set(BUILD_FORTRAN_BINDINGS ${ROCFFT_BUILD_FORTRAN_BINDINGS})
endif()
if(DEFINED ROCFFT_BUILD_FORTRAN_CLIENTS)
  set(BUILD_FORTRAN_CLIENTS ${ROCFFT_BUILD_FORTRAN_CLIENTS})
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
# overloads for; every other routine exposes the same plain type(c_ptr) interface
# at all three tiers. For rocFFT that subset is rocfft_plan_create and
# rocfft_plan_description_set_data_layout -- the only named generic interfaces in
# rocfft.F90 and the only place the USE_ASSUMED_* guards appear. The tier is
# still worth choosing: the arguments those specifics take as Fortran arrays are
# the integer(c_size_t) length, offset and stride vectors a caller builds on the
# host.
set(FORTRAN_ARRAY_INTERFACES "assumed-shape" CACHE STRING
    "Array-argument overloads to expose: none, assumed-shape, or assumed-rank")
set_property(CACHE FORTRAN_ARRAY_INTERFACES PROPERTY STRINGS
             none assumed-shape assumed-rank)

# Report to the parent whether the bindings were really built; ROCM_HAVE_FORTRAN
# says only that a compiler exists. Guarded because standalone this is the
# top-level scope, where PARENT_SCOPE warns under -Wdev.
if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(ROCFFT_HAVE_FORTRAN_BINDINGS FALSE PARENT_SCOPE)
endif()

if(NOT BUILD_FORTRAN_BINDINGS)
  return()
endif()

# The answer is computed ONCE by the monorepo root and published as
# ROCM_HAVE_FORTRAN. The name is deliberately neither rocm-libraries' nor
# rocm-systems': a library from one monorepo is regularly built inside the other,
# and a repo-specific name would read the wrong variable. rocm-systems, which has
# no root CMakeLists, publishes the same name from shared/cmake/ROCmFortran.cmake.
# ROCM_LIBS_HAVE_FORTRAN is what this repo's root publishes TODAY, honoured
# second and never over the new name; once the root is switched over that branch
# is dead and should go. The local probe is the standalone path.
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
  message(STATUS "rocFFT: no Fortran compiler found, skipping the Fortran bindings")
  return()
endif()

enable_language(Fortran)

# Here rather than beside the install() rules, because the install layout is also
# baked into the target's INSTALL_INTERFACE include directory, set well before
# them. Nothing must hardcode lib/ or include/: a distro installing to lib64 gets
# CMAKE_INSTALL_LIBDIR=lib64, and a literal lib/fortran then names nothing.
include(GNUInstallDirs)

if(NOT FORTRAN_ARRAY_INTERFACES MATCHES "^(none|assumed-shape|assumed-rank)$")
  message(FATAL_ERROR
    "FORTRAN_ARRAY_INTERFACES must be none, assumed-shape or assumed-rank, "
    "not '${FORTRAN_ARRAY_INTERFACES}'.")
endif()

add_library(rocfft_fortran STATIC rocfft.F90)
# roc::, matching the namespace rocFFT exports its C library under, so that a
# consumer is not made to remember two namespaces for one package.
add_library(roc::rocfft_fortran ALIAS rocfft_fortran)

# A .mod is not portable across compilers, so artifacts install under a
# per-compiler directory and two compilers can coexist in one prefix. The name is
# the compiler a user types (amdflang, gfortran), not CMake's compiler ID, so the
# -I/-L paths in the documentation match a raw link line. Derived from the ID
# rather than from the basename of CMAKE_Fortran_COMPILER, which would be wrong
# for an MPI wrapper (mpif90), a versioned binary (gfortran-13) or Cray's ftn.
#
# This mapping MUST mirror the one in rocfft-fortran-config.cmake.in exactly:
# that file recomputes the same name for the CONSUMER's compiler and compares the
# two, and the two cannot share code, because the config runs in a consumer's
# build long after this one is gone. Let them drift and the config rejects every
# consumer with a compiler mismatch that does not exist.
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
set(ROCFFT_FORTRAN_COMPILER_DIR "${_fc_name}" CACHE STRING
    "Per-compiler subdirectory the .mod and the archive install under")

set(_moddir "${CMAKE_CURRENT_BINARY_DIR}/include")
set_target_properties(rocfft_fortran PROPERTIES
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
# amdflang -cpp -DUSE_ASSUMED_SHAPE=1 -DUSE_ASSUMED_RANK=1 -fsyntax-only, and -E
# under both macros is byte-identical to -E under USE_ASSUMED_RANK alone.
if(FORTRAN_ARRAY_INTERFACES STREQUAL "assumed-shape")
  target_compile_definitions(rocfft_fortran PRIVATE USE_ASSUMED_SHAPE=1)
elseif(FORTRAN_ARRAY_INTERFACES STREQUAL "assumed-rank")
  target_compile_definitions(rocfft_fortran PRIVATE USE_ASSUMED_RANK=1)
endif()

# No line-length flag: the generator wraps the emitted source at 112 columns,
# inside the 132 free-form Fortran guarantees.

target_include_directories(rocfft_fortran PUBLIC
  "$<BUILD_INTERFACE:${_moddir}>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/fortran/${ROCFFT_FORTRAN_COMPILER_DIR}>")

# Link interface only: needed to link an executable, not to build this archive.
# Standalone it has to be looked for -- testing for the target without ever
# calling find_package would export no link interface at all, and every consumer
# would hit undefined references despite linking roc::rocfft_fortran.
#
# rocfft-config.cmake does find_dependency(hip), and hip-config.cmake reaches
# FindThreads, which hard-errors unless C or CXX is enabled, so C is enabled
# first; it compiles none of the sources here. QUIET, because the bindings
# compile against nothing and build fine with no rocFFT installed.
if(NOT TARGET roc::rocfft)
  if(NOT CMAKE_C_COMPILER_LOADED AND NOT CMAKE_CXX_COMPILER_LOADED)
    enable_language(C)
  endif()
  find_package(rocfft QUIET)
endif()

# Records whether the search produced a link interface; the config template turns
# it into the condition guarding its own find_dependency(rocfft). Demanding the C
# package unconditionally would oblige a consumer to find a package this archive
# does not depend on. Set on BOTH branches, to an explicit TRUE or FALSE: the
# template substitutes the value straight into an if(), and an unset variable
# would leave a bare if() that every consumer's find_package dies on.
if(TARGET roc::rocfft)
  target_link_libraries(rocfft_fortran INTERFACE roc::rocfft)
  set(ROCFFT_FORTRAN_NEEDS_C_PACKAGE TRUE)
else()
  set(ROCFFT_FORTRAN_NEEDS_C_PACKAGE FALSE)
  message(STATUS
    "rocFFT: librocfft was not found, so the Fortran bindings carry no link "
    "interface; a consumer will have to link it (-lrocfft) itself.")
endif()

if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(ROCFFT_HAVE_FORTRAN_BINDINGS TRUE PARENT_SCOPE)
endif()

# The binding tests are GPU runtime executables, so in-tree they follow the
# library's own client/test switch; the condition below is the one rocFFT guards
# add_subdirectory( clients ) with, and the root resolves those three switches
# before reaching this directory so that both read the same values. It is a
# ceiling: nothing a user can pass turns the tests back on in a library-only build.
# Standalone that switch does not exist. Set as a plain variable: the cache entry
# is shared monorepo-wide, so turning it off here must stay scoped here.
#
# Placed after the early returns above rather than beside the option(), so that a
# configure which never builds any Fortran does not announce a decision about
# tests it was never going to reach.
if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR
   AND BUILD_FORTRAN_CLIENTS
   AND NOT (BUILD_CLIENTS_SAMPLES OR BUILD_CLIENTS_TESTS OR BUILD_CLIENTS_BENCH))
  message(STATUS
    "rocFFT: clients are not being built, skipping the Fortran binding tests.")
  set(BUILD_FORTRAN_CLIENTS OFF)
endif()

if(BUILD_FORTRAN_CLIENTS)
  # ctest walks the chain of CTestTestfile.cmake down from a build root, and
  # CMake writes it only for directories where testing was enabled, starting at
  # the top, so in-tree the root has to enable it too -- which it does, on the
  # same three client switches the gate above reads.
  enable_testing()
  add_subdirectory(test)
endif()

include(CMakePackageConfigHelpers)

# COMPONENT devel on every rule below: everything installed here is a development
# artifact, and devel is what rocm-cmake's install helpers default to -- without
# it these land in the runtime component. Plain install(), not rocm_install():
# this directory must also configure standalone, without rocm-cmake.
install(TARGETS rocfft_fortran
        EXPORT rocfft-fortran-targets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}/fortran/${ROCFFT_FORTRAN_COMPILER_DIR}"
                COMPONENT devel)
install(DIRECTORY "${_moddir}/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/fortran/${ROCFFT_FORTRAN_COMPILER_DIR}"
        COMPONENT devel
        FILES_MATCHING PATTERN "*.mod")

# The generated source, so that a user on a compiler ROCm ships no .mod for can
# rebuild the binding without fetching a monorepo, from source that cannot drift
# from the installed librocfft. Under share/, not include/: every entry under
# include/fortran/ is read as naming a compiler -- the config below enumerates
# that directory to report which are installed, and would list rocfft.F90.
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/rocfft.F90"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/rocfft/fortran"
        COMPONENT devel)
install(EXPORT rocfft-fortran-targets
        FILE rocfft-fortran-targets.cmake
        NAMESPACE roc::
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/rocfft"
        COMPONENT devel)

# Kept out of the template so neither side spells lib/ or include/ literally.
set(ROCFFT_FORTRAN_LIBDIR     "${CMAKE_INSTALL_LIBDIR}/fortran")
set(ROCFFT_FORTRAN_INCLUDEDIR "${CMAKE_INSTALL_INCLUDEDIR}/fortran")

# Beside the C package, so a single CMAKE_PREFIX_PATH entry finds both.
configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/rocfft-fortran-config.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/rocfft-fortran-config.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/rocfft"
  # PATH_VARS: @PACKAGE_INIT@ rewrites each into an absolute path computed from
  # where the config was FOUND, so the package is relocatable and gets lib64
  # right. ROCFFT_FORTRAN_COMPILER_DIR stays a plain @-substitution: it is a
  # bare directory NAME, and PATH_VARS would resolve it under the prefix.
  PATH_VARS ROCFFT_FORTRAN_LIBDIR ROCFFT_FORTRAN_INCLUDEDIR)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/rocfft-fortran-config.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/rocfft"
        COMPONENT devel)

# find_package(rocfft-fortran), though, searches lib/cmake/rocfft-fortran*,
# which lib/cmake/rocfft does not match. A forwarder gives that search somewhere
# to land without duplicating the real config and its targets file.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/forward/rocfft-fortran-config.cmake"
"# Generated forwarder. The real config lives beside the rocFFT C package, in
# lib/cmake/rocfft, so that one CMAKE_PREFIX_PATH entry finds both. This
# file only exists so that find_package(rocfft-fortran) resolves to the same
# place.
include(\"\${CMAKE_CURRENT_LIST_DIR}/../rocfft/rocfft-fortran-config.cmake\")
")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/forward/rocfft-fortran-config.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/rocfft-fortran"
        COMPONENT devel)

# A version file, so that find_package(rocfft-fortran 1.0) resolves.
# SameMajorVersion because the bind(C) surface tracks the C API's promise.
#
# Read from THIS tree's top-level CMakeLists, not from rocfft_VERSION: in a
# standalone build that variable is set by the find_package(rocfft) above and
# reports whatever rocFFT is INSTALLED, not the version the bindings were
# generated from.
set(_fortran_version "")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../CMakeLists.txt")
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../CMakeLists.txt" _rocfft_root)
  # rocFFT does NOT spell the version as a literal anywhere: it sets the three
  # components and composes VERSION_STRING from them, so read the components and
  # recompose. Note the spaces inside the parentheses -- rocFFT writes
  # set ( ROCFFT_VERSION_MAJOR "1" ).
  #
  # Success is judged on the whole match being non-empty, NOT on the captured
  # group being truthy: the minor component is the string "0", one of CMake's
  # false constants, so if(NOT CMAKE_MATCH_1) would treat a good match as a
  # failure and install no version file at all.
  set(_rocfft_parts "")
  foreach(_part MAJOR MINOR PATCH)
    string(REGEX MATCH "set[ \t]*\\([ \t]*ROCFFT_VERSION_${_part}[ \t]+\"?([0-9]+)"
           _m "${_rocfft_root}")
    if(_m STREQUAL "")
      set(_rocfft_parts "")
      break()
    endif()
    list(APPEND _rocfft_parts "${CMAKE_MATCH_1}")
  endforeach()
  if(_rocfft_parts)
    list(JOIN _rocfft_parts "." _fortran_version)
  endif()
  unset(_rocfft_parts)
  unset(_rocfft_root)
endif()

if(_fortran_version)
  write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/rocfft-fortran-config-version.cmake"
    VERSION "${_fortran_version}"
    COMPATIBILITY SameMajorVersion)
  # BOTH directories: CMake looks for <pkg>-config-version.cmake NEXT TO the
  # config it actually resolved, and a versioned find_package(rocfft-fortran)
  # resolves the forwarder.
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/rocfft-fortran-config-version.cmake"
          DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/rocfft"
          COMPONENT devel)
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/rocfft-fortran-config-version.cmake"
          DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/rocfft-fortran"
          COMPONENT devel)
else()
  # Better no version file than a wrong one: an unversioned find_package still
  # succeeds, whereas a wrong number makes a versioned one link the wrong .mod.
  message(STATUS
    "rocFFT: could not determine the version for the Fortran package, so no "
    "rocfft-fortran-config-version.cmake is installed; "
    "find_package(rocfft-fortran <version>) will not match.")
endif()
