# Fortran bindings for hipRAND.
#
# hiprand.F90 is GENERATED from the hipRAND C headers by the rocm-fortran
# generator and committed next to the library it binds, so a header change and
# its binding travel in the same pull request. Do not edit it by hand.
#
# One self-contained module, `hiprand`: one bind(C) interface per hipRAND C
# entry point, the enum constants and the hiprandCheck helper. It declares no
# derived types -- opaque handles are type(c_ptr), and the strongly-typed
# overloads and device-pointer specifics are not part of the packaged track.
# That is the shape the packaged track generates, not an omission. The module
# compiles against nothing, so this directory does not depend on the hipRAND C
# target; libhiprand is needed only to LINK, and is carried as the link
# interface.

cmake_minimum_required(VERSION 3.19)

# Standalone, this directory is its own project, so the bindings can be built
# and packaged alone -- the supported path for a Fortran compiler other than the
# amdflang the shipped bindings use. In-tree, hipRAND's project() is in effect.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(hiprand-fortran LANGUAGES NONE)
endif()

# ON by default but guarded: a site with no Fortran compiler must still build
# the C library. "${UNIX}" rather than ON because ROCm ships no Fortran compiler
# on Windows: TheRock adds flang only in the non-WIN32 branch, so the amdflang
# preference below finds nothing, the probe falls through to a stray gfortran,
# and enable_language(Fortran) then fails to identify it. hipRAND's root
# declares the same default and wins when this directory is reached through it.
#
# OFF on the CUDA backend, and that one is about the GENERATED SOURCE rather
# than the compiler: hiprand.F90 is produced from the ROCm code path, and no CI
# job builds or runs it against a cuRAND-backed libhiprand, so shipping it on by
# default would publish an untested artefact.
#
# BUILD_WITH_LIB is hipRAND's own backend switch. The root must define it above
# its own option(BUILD_FORTRAN_BINDINGS), because this default reads it.
# Standalone it is undefined and the default falls back to ${UNIX}: a standalone
# build has no backend of its own. In-tree this option() call is the SECOND one, so the root's has already
# fixed the cache entry and this is a no-op; both compute the same default from
# the same variable, so they can never disagree.
if(BUILD_WITH_LIB STREQUAL "CUDA")
  set(_hiprand_fortran_default OFF)
else()
  set(_hiprand_fortran_default "${UNIX}")
endif()
option(BUILD_FORTRAN_BINDINGS "Build the hipRAND Fortran bindings"
       "${_hiprand_fortran_default}")
unset(_hiprand_fortran_default)

# The bindings' own test suite, and the Fortran clients where a library has
# them: one switch rather than two. Guarded like the bindings, so asking for
# them when the bindings were not built is a skip rather than an error.
option(BUILD_FORTRAN_CLIENTS "Build the hipRAND Fortran clients and binding tests" ON)

# Both options are ONE cache entry shared by every library in the monorepo, so
# -DBUILD_FORTRAN_BINDINGS=OFF from the top turns the whole Fortran track off.
# Each library also honours a name of its own, to single itself out. For the
# BINDINGS that override is the last word; for the CLIENTS it is only a ceiling,
# since in-tree the tests belong to hipRAND's test track and BUILD_TEST still
# bounds them from below. That gate runs further down, past the early returns.
if(DEFINED HIPRAND_BUILD_FORTRAN_BINDINGS)
  set(BUILD_FORTRAN_BINDINGS ${HIPRAND_BUILD_FORTRAN_BINDINGS})
endif()
if(DEFINED HIPRAND_BUILD_FORTRAN_CLIENTS)
  set(BUILD_FORTRAN_CLIENTS ${HIPRAND_BUILD_FORTRAN_CLIENTS})
endif()

# Which array-argument overloads the module exposes. USE_ASSUMED_SHAPE and
# USE_ASSUMED_RANK, the macros the generated source is guarded with, are
# alternatives rather than a switch plus a modifier: the source selects between
# them with #ifdef/#else, so assumed-rank REPLACES the per-rank variants rather
# than adding to them. Only the subset of routines the generator emits array
# overloads for is affected; the raw type(c_ptr) surface stays in every tier.
#
#   none           the raw type(c_ptr) surface only (Fortran 2003)
#   assumed-shape  + the per-rank overloads (Fortran 2008): the default, and
#                  what the bindings shipped in ROCm are built with
#   assumed-rank   + one dimension(..) overload instead (Fortran 2018)
set(FORTRAN_ARRAY_INTERFACES "assumed-shape" CACHE STRING
    "Array-argument overloads to expose: none, assumed-shape, or assumed-rank")
set_property(CACHE FORTRAN_ARRAY_INTERFACES PROPERTY STRINGS
             none assumed-shape assumed-rank)

# Distinct from ROCM_HAVE_FORTRAN, which says only that a compiler exists.
# Guarded because PARENT_SCOPE at top level warns under -Wdev, which is fatal
# under -Werror=dev, and the standalone build has to configure cleanly.
if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(HIPRAND_HAVE_FORTRAN_BINDINGS FALSE PARENT_SCOPE)
endif()

if(NOT BUILD_FORTRAN_BINDINGS)
  return()
endif()

# Reached on the CUDA backend only when the default above was overridden by
# hand. Warn rather than fail: the bindings DO build and link there, since the
# public hipRAND headers carry no CUDART_VERSION guard on their entry points and
# library/src/nvidia_detail/ defines every symbol the module references. What is
# missing is evidence, not symbols.
if(BUILD_WITH_LIB STREQUAL "CUDA")
  message(WARNING
    "hipRAND: the Fortran bindings were requested on the CUDA backend. "
    "hiprand.F90 is generated from the ROCm code path and is not tested "
    "against a cuRAND-backed libhiprand, which is why it is off by default "
    "there. Building it is supported; vouching for it is not.")
endif()

# Probing is not free, so the monorepo root probes ONCE and publishes
# ROCM_HAVE_FORTRAN. That name is neither repo's on purpose: hipRAND is
# regularly built inside a rocm-systems tree (and HIP inside this one), and a
# repo-specific name would read the wrong variable. ROCM_LIBS_HAVE_FORTRAN is
# the rocm-libraries root's current spelling, honoured until it adopts the new
# name. The local probe is the standalone path.
if(DEFINED ROCM_HAVE_FORTRAN)
  set(_have_fortran ${ROCM_HAVE_FORTRAN})
elseif(DEFINED ROCM_LIBS_HAVE_FORTRAN)
  set(_have_fortran ${ROCM_LIBS_HAVE_FORTRAN})
else()
  include(CheckLanguage)

  # check_language() writes the CACHE entry CMAKE_Fortran_COMPILER, which
  # rocm-cmake's ROCMChecks watches to catch a project setting a toolchain
  # variable behind the toolchain file's back. A legitimate probe is
  # indistinguishable from a violation, and under -DROCM_ERROR_TOOLCHAIN_VAR=ON
  # (which CI sets) that is a hard configure failure, so silence the checker
  # across the probe and restore it afterwards. Plain variables, so they shadow
  # the cache for this scope only; both names, because the callback escalates on
  # the error flag before it ever looks at the warn one.
  set(_rocm_warn_toolchain_var "${ROCM_WARN_TOOLCHAIN_VAR}")
  set(_rocm_error_toolchain_var "${ROCM_ERROR_TOOLCHAIN_VAR}")
  set(ROCM_WARN_TOOLCHAIN_VAR OFF)
  set(ROCM_ERROR_TOOLCHAIN_VAR OFF)

  # Prefer ROCm's own amdflang: check_language(Fortran) takes the first compiler
  # on the search path, which on a ROCm CI image is the system gfortran -- that
  # is how the bindings ended up installed under fortran/gfortran/ while the
  # documentation promises fortran/amdflang/. Look beside the clang already
  # compiling this project, with ROCM_PATH for a standalone configure and
  # NO_DEFAULT_PATH so a stray amdflang cannot win. An explicit
  # CMAKE_Fortran_COMPILER, or FC, always wins. check_language() is a no-op once
  # CMAKE_Fortran_COMPILER is defined, so this has to run before it.
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
      # Confirm it runs before committing to it: a ROCm tree can carry a
      # dangling amdflang symlink, which check_language() would not catch, so
      # the failure would surface far later as a broken enable_language.
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
  message(STATUS "hipRAND: no Fortran compiler found, skipping the Fortran bindings")
  return()
endif()

enable_language(Fortran)

# Here rather than beside the install() rules, because the install layout is
# also baked into the target's INSTALL_INTERFACE include directory, set well
# before them. Nothing must hardcode lib/: a distro with
# CMAKE_INSTALL_LIBDIR=lib64 would get a lib/fortran that does not exist.
include(GNUInstallDirs)

if(NOT FORTRAN_ARRAY_INTERFACES MATCHES "^(none|assumed-shape|assumed-rank)$")
  message(FATAL_ERROR
    "FORTRAN_ARRAY_INTERFACES must be none, assumed-shape or assumed-rank, "
    "not '${FORTRAN_ARRAY_INTERFACES}'.")
endif()

add_library(hiprand_fortran STATIC hiprand.F90)

# hip::, the namespace hipRAND exports its C library under, so the two halves of
# one package are not in different namespaces.
add_library(hip::hiprand_fortran ALIAS hiprand_fortran)

# A .mod is not portable across compilers, so the artifacts install under a
# per-compiler directory and two compilers can be installed side by side. Named
# after the compiler a user actually types (amdflang, gfortran), so the -I/-L
# paths in the documentation are the ones a raw link line needs. Derived from
# the compiler ID rather than from the basename of CMAKE_Fortran_COMPILER, which
# would give the wrong directory for an MPI wrapper (mpif90), a versioned binary
# (gfortran-13), an absolute path or the Cray wrapper (ftn).
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
  # Best guess available, and still collision-free.
  get_filename_component(_fc_name "${CMAKE_Fortran_COMPILER}" NAME_WE)
  string(TOLOWER "${_fc_name}" _fc_name)
endif()

# Deliberately lossy: a .mod is tied to the compiler VERSION too, so gfortran 13
# and 14 share a directory and the second overwrites the first. That is the
# documented layout and right for the common case of one compiler per prefix; a
# site with several versions of the SAME compiler sets this itself.
set(HIPRAND_FORTRAN_COMPILER_DIR "${_fc_name}" CACHE STRING
    "Per-compiler subdirectory the .mod and the archive install under")

set(_moddir "${CMAKE_CURRENT_BINARY_DIR}/include")
set_target_properties(hiprand_fortran PROPERTIES
  Fortran_MODULE_DIRECTORY "${_moddir}"
  # The array overloads sit behind preprocessor guards.
  Fortran_PREPROCESS ON
  # Free form is the .F90 default, but stating it lets CMake emit whichever flag
  # the compiler in use expects rather than relying on the extension.
  Fortran_FORMAT FREE
  POSITION_INDEPENDENT_CODE ON)

# The option value and the macro name are the same word, one tier each. The
# generic-interface region and the CONTAINS region of hiprand.F90 test the two
# macros in the SAME order, so defining both is not an error -- assumed-rank
# wins in both and the module still compiles -- but the branches below define
# exactly one, so a build gets the tier it asked for. `none` defines neither and
# leaves the raw type(c_ptr) surface, which every tier keeps.
if(FORTRAN_ARRAY_INTERFACES STREQUAL "assumed-shape")
  target_compile_definitions(hiprand_fortran PRIVATE USE_ASSUMED_SHAPE=1)
elseif(FORTRAN_ARRAY_INTERFACES STREQUAL "assumed-rank")
  target_compile_definitions(hiprand_fortran PRIVATE USE_ASSUMED_RANK=1)
endif()

# No line-length flag: the generator wraps the emitted source at 112 columns,
# inside the 132 that free-form Fortran guarantees.

target_include_directories(hiprand_fortran PUBLIC
  "$<BUILD_INTERFACE:${_moddir}>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/fortran/${HIPRAND_FORTRAN_COMPILER_DIR}>")

# Link interface only: needed to link an executable, not to build this archive.
# hip::hiprand is defined by hipRAND's library/; standalone it has to be found,
# since testing for the target without ever calling find_package would leave the
# archive exporting no link interface and every consumer hitting undefined
# references despite linking hip::hiprand_fortran exactly as documented.
#
# hiprand-config.cmake does find_dependency(hip), and hip-config.cmake reaches
# FindThreads, which hard-errors unless C or CXX is enabled; C is enabled here
# and compiles none of the sources. QUIET, because the bindings compile against
# nothing and are legitimately buildable where no hipRAND is installed.
if(NOT TARGET hip::hiprand)
  if(NOT CMAKE_C_COMPILER_LOADED AND NOT CMAKE_CXX_COMPILER_LOADED)
    enable_language(C)
  endif()
  find_package(hiprand QUIET)
endif()

# Whichever way that goes, record it: the config package must call
# find_dependency(hiprand) when, and only when, the exported target really names
# hip::hiprand in its link interface. An explicit TRUE or FALSE on both paths,
# never the empty string: it is substituted into an if() in the template, and an
# empty substitution is a syntax error every consumer hits.
if(TARGET hip::hiprand)
  target_link_libraries(hiprand_fortran INTERFACE hip::hiprand)
  set(HIPRAND_FORTRAN_NEEDS_C_PACKAGE TRUE)
else()
  set(HIPRAND_FORTRAN_NEEDS_C_PACKAGE FALSE)
  message(STATUS
    "hipRAND: the C library was not found, so the Fortran bindings carry no "
    "link interface; a consumer will have to link it (-lhiprand) itself.")
endif()

if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(HIPRAND_HAVE_FORTRAN_BINDINGS TRUE PARENT_SCOPE)
endif()

# In-tree the binding tests are GPU runtime executables belonging to the test
# track, so they follow the same condition hipRAND guards add_subdirectory(test)
# with, and unconditionally: HIPRAND_BUILD_FORTRAN_CLIENTS=ON does not reopen
# it, because what it asks for is a test executable this build does not make.
# Set as a plain variable, because the cache entry is one switch shared by every
# library and must not be written back. Placed past both early returns: a
# configure that left earlier would announce a skip for the wrong reason.
if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR
   AND BUILD_FORTRAN_CLIENTS
   AND NOT BUILD_TEST)
  message(STATUS
    "hipRAND: tests are not being built, skipping the Fortran binding tests.")
  set(BUILD_FORTRAN_CLIENTS OFF)
endif()

if(BUILD_FORTRAN_CLIENTS)
  # For a STANDALONE build. In tree the tests require BUILD_TEST (above), under
  # which the hipRAND root calls enable_testing() itself.
  enable_testing()
  add_subdirectory(test)
endif()

include(CMakePackageConfigHelpers)

# Where hiprand's own rocm_export_targets() puts hiprand-config.cmake, so one
# CMAKE_PREFIX_PATH entry finds both halves of the package.
set(_fortran_pkgdir "${CMAKE_INSTALL_LIBDIR}/cmake/hiprand")

# COMPONENT devel on every rule below: everything installed here is needed to
# build against the bindings, not to run a program. Without it these files land
# in runtime instead, because ROCMCreatePackage sets
# CMAKE_INSTALL_DEFAULT_COMPONENT_NAME to runtime for the whole project. Plain
# install(), not rocm_install(): this directory must also configure standalone,
# where rocm-cmake was never loaded and those commands do not exist.
install(TARGETS hiprand_fortran
        EXPORT hiprand-fortran-targets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}/fortran/${HIPRAND_FORTRAN_COMPILER_DIR}"
                COMPONENT devel)
install(DIRECTORY "${_moddir}/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/fortran/${HIPRAND_FORTRAN_COMPILER_DIR}"
        COMPONENT devel
        FILES_MATCHING PATTERN "*.mod")

# The generated source, so a user on a compiler ROCm ships no .mod for can
# rebuild the binding, and from the same ROCm as the installed libhiprand.
# Under share/, not include/: every entry under include/fortran/ is a compiler
# name, and the config below enumerates that directory to report which compilers
# are installed, so a source file there would be listed as one.
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/hiprand.F90"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/hiprand/fortran"
        COMPONENT devel)
install(EXPORT hiprand-fortran-targets
        FILE hiprand-fortran-targets.cmake
        NAMESPACE hip::
        DESTINATION "${_fortran_pkgdir}"
        COMPONENT devel)

# Kept out of the template so neither side spells lib/ or include/ literally: a
# literal lib/fortran is wrong the moment CMAKE_INSTALL_LIBDIR is lib64.
set(HIPRAND_FORTRAN_LIBDIR     "${CMAKE_INSTALL_LIBDIR}/fortran")
set(HIPRAND_FORTRAN_INCLUDEDIR "${CMAKE_INSTALL_INCLUDEDIR}/fortran")

configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/hiprand-fortran-config.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/hiprand-fortran-config.cmake"
  INSTALL_DESTINATION "${_fortran_pkgdir}"
  # @PACKAGE_INIT@ rewrites each of these into an absolute path computed from
  # where the config file was FOUND, so the package survives relocation and gets
  # lib64 right. HIPRAND_FORTRAN_COMPILER_DIR stays a plain @-substitution: it
  # is a bare directory NAME, and PATH_VARS would resolve it under the prefix.
  PATH_VARS HIPRAND_FORTRAN_LIBDIR HIPRAND_FORTRAN_INCLUDEDIR)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/hiprand-fortran-config.cmake"
        DESTINATION "${_fortran_pkgdir}"
        COMPONENT devel)

# find_package(hiprand-fortran), the documented spelling, searches
# lib/cmake/hiprand-fortran*, which lib/cmake/hiprand does not match.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/forward/hiprand-fortran-config.cmake"
"# Generated forwarder. The real config lives beside the hiprand C package, in
# lib/cmake/hiprand. This file only exists so that find_package(hiprand-fortran)
# resolves to the same place.
include(\"\${CMAKE_CURRENT_LIST_DIR}/../hiprand/hiprand-fortran-config.cmake\")
")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/forward/hiprand-fortran-config.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/hiprand-fortran"
        COMPONENT devel)

# So that find_package(hiprand-fortran 5.1) resolves at all. SameMajorVersion
# because the module's bind(C) surface tracks the C API's compatibility promise.
#
# Read from THIS tree's top-level CMakeLists, not from hiprand_VERSION: that
# variable is the obvious choice and a trap, because in a standalone build it is
# set by the find_package(hiprand) above and reports whatever hipRAND is
# INSTALLED rather than the version these bindings were generated from.
set(_fortran_version "")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../CMakeLists.txt")
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../CMakeLists.txt" _hiprand_root)
  # hipRAND spells it set(VERSION_STRING "3.4.0") and then passes the variable to
  # rocm_setup_version, so match the literal assignment rather than the call.
  string(REGEX MATCH "set\\([ \t]*VERSION_STRING[ \t]+\"([0-9]+\\.[0-9]+\\.[0-9]+)\""
         _m "${_hiprand_root}")
  if(CMAKE_MATCH_1)
    set(_fortran_version "${CMAKE_MATCH_1}")
  endif()
  unset(_hiprand_root)
endif()

if(_fortran_version)
  write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/hiprand-fortran-config-version.cmake"
    VERSION "${_fortran_version}"
    COMPATIBILITY SameMajorVersion)
  # Both directories: CMake looks for the version file next to the config it
  # actually resolved, and a versioned find_package resolves the forwarder.
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/hiprand-fortran-config-version.cmake"
          DESTINATION "${_fortran_pkgdir}"
          COMPONENT devel)
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/hiprand-fortran-config-version.cmake"
          DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/hiprand-fortran"
          COMPONENT devel)
else()
  # Better none than a wrong one: a version file claiming the wrong number makes
  # a versioned find_package succeed and link the wrong .mod.
  message(STATUS
    "hipRAND: could not determine the version for the Fortran package, so no "
    "hiprand-fortran-config-version.cmake is installed; "
    "find_package(hiprand-fortran <version>) will not match.")
endif()
