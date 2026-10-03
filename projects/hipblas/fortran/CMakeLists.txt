# Fortran bindings for hipBLAS.
#
# hipblas.F90 is GENERATED from the hipBLAS C headers by the rocm-fortran
# generator and committed next to the library it binds, so a header change and
# its binding travel in the same pull request. Do not edit it: regenerate it.
#
# It is one self-contained module, `hipblas`, carrying the bind(C) interfaces,
# the enum constants and the hipblasCheck status helper. Every opaque C object,
# hipblasHandle_t among them, is a plain type(c_ptr): no derived types, no
# _typed or _dptr overloads, no docstrings. Array overloads cover only the
# subset of routines the generator supports; every other routine is reached
# through type(c_ptr) under every tier.
#
# A Fortran binding compiles against nothing, so this directory needs no hipBLAS
# C target to build. libhipblas is required to LINK an executable, and is
# carried as the target's link interface.
#
# This REPLACES library/src/hipblas_module.f90, removed in the same commit. That
# file defined two modules, hipblas and hipblas_enums; this one defines only
# hipblas, enums folded in, so `use hipblas_enums` becomes `use hipblas`.

cmake_minimum_required(VERSION 3.19)

# Standalone (not a subdirectory of the hipBLAS build) this is its own project,
# so the bindings can be built and packaged alone -- the supported path for a
# Fortran compiler other than the amdflang ROCm ships them built with.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(hipblas-fortran LANGUAGES NONE)
endif()

# Guarded, so a site with no Fortran compiler can still build the C library.
# "${UNIX}" rather than ON because ROCm ships no Fortran compiler on Windows at
# all: TheRock adds flang to LLVM_ENABLE_PROJECTS only in the non-WIN32 branch
# of compiler/pre_hook_amd-llvm.cmake, so the amdflang preference below finds
# nothing, the probe falls through to a stray gfortran, and
# enable_language(Fortran) fails to identify it. hipBLAS gates its hand-written
# Fortran clients the same way, in clients/cmake/build-options.cmake.
#
# OFF on the CUDA backend, and that is about the GENERATED SOURCE rather than
# the compiler: hipblas.F90 is produced from the ROCm code path and no CI job
# builds or runs it against a cuBLAS-backed libhipblas, so shipping it on by
# default would publish an untested artefact. It has to be asked for by name.
#
# hipBLAS has no backend variable of its own. The root sets ENV{HIP_PLATFORM} to
# nvidia for the deprecated -DUSE_CUDA=ON, and again when find_package(hip)
# fails and the HIP MODULE path takes over, and only then gives the plain
# HIP_PLATFORM variable a value, so both spellings are read: either can be the
# one carrying the answer. Both blocks run before add_subdirectory(fortran), so
# the value has settled here. Standalone neither is defined and the default
# falls back to ${UNIX}, which is right: that build has no backend of its own.
if("$ENV{HIP_PLATFORM}" STREQUAL "nvidia" OR "${HIP_PLATFORM}" STREQUAL "nvidia")
  set(_hipblas_fortran_default OFF)
else()
  set(_hipblas_fortran_default "${UNIX}")
endif()
# The ONLY option(BUILD_FORTRAN_BINDINGS) in hipBLAS, deliberately: the first
# option() call in a CMake run fixes the cache entry, so a second one in the
# root with a different default would be silently ignored by whichever ran
# second and the two would read as disagreeing.
option(BUILD_FORTRAN_BINDINGS "Build the hipBLAS Fortran bindings"
       "${_hipblas_fortran_default}")
unset(_hipblas_fortran_default)

# One switch for the binding tests and a library's Fortran clients. ON by
# default, but guarded like the bindings: asking for them when the bindings were
# not built is a skip, not an error.
option(BUILD_FORTRAN_CLIENTS "Build the hipBLAS Fortran clients and binding tests" ON)

# BUILD_FORTRAN_BINDINGS and BUILD_FORTRAN_CLIENTS are ONE cache entry shared by
# every library in the monorepo, so -DBUILD_FORTRAN_BINDINGS=OFF from the top
# turns the whole Fortran track off. Each library also honours a name of its own
# for when one has to be singled out. That override is the last word for the
# BINDINGS only: the Fortran clients are GPU test and sample executables, so
# clients/ turns them off whenever this directory produced no binding target,
# whatever HIPBLAS_BUILD_FORTRAN_CLIENTS said.
if(DEFINED HIPBLAS_BUILD_FORTRAN_BINDINGS)
  set(BUILD_FORTRAN_BINDINGS ${HIPBLAS_BUILD_FORTRAN_BINDINGS})
endif()
if(DEFINED HIPBLAS_BUILD_FORTRAN_CLIENTS)
  set(BUILD_FORTRAN_CLIENTS ${HIPBLAS_BUILD_FORTRAN_CLIENTS})
endif()

# Which array-argument overloads the module exposes. One tri-state rather than a
# boolean per tier, because the two upper tiers are alternatives, not additions:
# an assumed-rank dummy is not distinguishable by rank from the per-rank
# specifics, so assumed-rank REPLACES the per-rank variants.
#
#   none           the raw type(c_ptr) surface only (Fortran 2003)
#   assumed-shape  + the per-rank array overloads (Fortran 2008): the default,
#                  and what the bindings shipped in ROCm are built with
#   assumed-rank   + one dimension(..) overload in place of that per-rank set
#                  (Fortran 2018)
#
# A routine gains a generic only where the generator could give its array
# argument a Fortran type, which rules out the void* arguments of the _ex family
# (hipblasAxpyEx takes x and y as void*, so it gets none even though both are
# arrays). Every other routine is a single explicit interface with type(c_ptr)
# array arguments, and reads the same in all three tiers.
set(FORTRAN_ARRAY_INTERFACES "assumed-shape" CACHE STRING
    "Array-argument overloads to expose: none, assumed-shape, or assumed-rank")
set_property(CACHE FORTRAN_ARRAY_INTERFACES PROPERTY STRINGS
             none assumed-shape assumed-rank)

# Reports whether the bindings were really BUILT, unlike ROCM_HAVE_FORTRAN,
# which says only that a compiler exists. Guarded because standalone this is the
# top-level scope, where PARENT_SCOPE warns under -Wdev and therefore fails
# under -Werror=dev, and standalone has to configure cleanly.
if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(HIPBLAS_HAVE_FORTRAN_BINDINGS FALSE PARENT_SCOPE)
endif()

if(NOT BUILD_FORTRAN_BINDINGS)
  return()
endif()

# Reached on the CUDA backend only when the default above was overridden by
# hand. Warn rather than fail: the bindings DO build and link there, since the
# public headers carry no CUDART_VERSION guard on their entry points and
# library/src/nvidia_detail/ defines every symbol the module references. What is
# missing is evidence, not symbols, so an explicit request is honoured.
if("$ENV{HIP_PLATFORM}" STREQUAL "nvidia" OR "${HIP_PLATFORM}" STREQUAL "nvidia")
  message(WARNING
    "hipBLAS: the Fortran bindings were requested on the CUDA backend. "
    "hipblas.F90 is generated from the ROCm code path and is not tested "
    "against a cuBLAS-backed libhipblas, which is why it is off by default "
    "there. Building it is supported; vouching for it is not.")
endif()

# The monorepo root probes for a Fortran compiler ONCE and publishes
# ROCM_HAVE_FORTRAN. That name is deliberately neither rocm-libraries' nor
# rocm-systems': hipBLAS is regularly built inside a rocm-systems tree, and a
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
  message(STATUS "hipBLAS: no Fortran compiler found, skipping the Fortran bindings")
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

add_library(hipblas_fortran STATIC hipblas.F90)

# roc::, not hip::: hipBLAS exports its C library as roc::hipblas, so a consumer
# is not made to remember two namespaces for two halves of one package.
add_library(roc::hipblas_fortran ALIAS hipblas_fortran)

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
# the SAME compiler sets HIPBLAS_FORTRAN_COMPILER_DIR itself, e.g. gfortran-13.
set(HIPBLAS_FORTRAN_COMPILER_DIR "${_fc_name}" CACHE STRING
    "Per-compiler subdirectory the .mod and the archive install under")

set(_moddir "${CMAKE_CURRENT_BINARY_DIR}/include")
set_target_properties(hipblas_fortran PROPERTIES
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
# Compiling hipblas.F90 by hand with both set is harmless but yields
# assumed-rank -- both regions of the source, the interface blocks and the
# contains region, test USE_ASSUMED_RANK first, and preprocessing with both
# yields text identical to USE_ASSUMED_RANK alone (amdflang -fsyntax-only
# accepts it). A build should still say which tier it meant.
if(FORTRAN_ARRAY_INTERFACES STREQUAL "assumed-shape")
  target_compile_definitions(hipblas_fortran PRIVATE USE_ASSUMED_SHAPE=1)
elseif(FORTRAN_ARRAY_INTERFACES STREQUAL "assumed-rank")
  target_compile_definitions(hipblas_fortran PRIVATE USE_ASSUMED_RANK=1)
endif()

# No line-length flag: every line of the emitted source fits within the 132
# columns free-form Fortran guarantees, so every compiler reads it without a
# vendor-specific option.

target_include_directories(hipblas_fortran PUBLIC
  "$<BUILD_INTERFACE:${_moddir}>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/fortran/${HIPBLAS_FORTRAN_COMPILER_DIR}>")

# Link interface only: roc::hipblas is needed to link an executable, not to
# build this archive. Standalone it has to be looked for -- testing for the
# target without ever calling find_package would mean it is never found, the
# archive would export no link interface at all, and every consumer would hit
# undefined references despite linking roc::hipblas_fortran as documented.
#
# hipblas-config.cmake does find_dependency(hip), whose hip-config.cmake reaches
# FindThreads and hard-errors unless C or CXX is enabled, so C is enabled here
# (it compiles nothing). QUIET: the bindings build with no hipBLAS installed.
if(NOT TARGET roc::hipblas)
  if(NOT CMAKE_C_COMPILER_LOADED AND NOT CMAKE_CXX_COMPILER_LOADED)
    enable_language(C)
  endif()
  find_package(hipblas QUIET)
endif()

# Record which of the two happened: the template's find_dependency(hipblas) is
# guarded on HIPBLAS_FORTRAN_NEEDS_C_PACKAGE. A config that required the C
# package unconditionally would oblige the consumer to find something this
# archive does not name; one that never required it would leave roc::hipblas
# undefined for the consumers that do link against it. Spelled TRUE/FALSE and
# never left empty -- the template substitutes this into a bare if(), and an
# empty value is a syntax error in every installed copy.
if(TARGET roc::hipblas)
  target_link_libraries(hipblas_fortran INTERFACE roc::hipblas)
  set(HIPBLAS_FORTRAN_NEEDS_C_PACKAGE TRUE)
else()
  message(STATUS
    "hipBLAS: the C library was not found, so the Fortran bindings carry no "
    "link interface; a consumer will have to link it (-lhipblas) itself.")
  set(HIPBLAS_FORTRAN_NEEDS_C_PACKAGE FALSE)
endif()

if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(HIPBLAS_HAVE_FORTRAN_BINDINGS TRUE PARENT_SCOPE)
endif()

# No fortran/test/ here: hipBLAS already drives this binding from clients/,
# where hipblas_fortran_module.f90 `use hipblas`s the generated module and wraps
# the API for the gtest suite. That is broader coverage than the hipFORT-derived
# tests would add, and already in CI. The nine libraries with no such path keep
# theirs.

include(CMakePackageConfigHelpers)

# Everything below installs into lib/cmake/hipblas, where hipBLAS's own
# rocm_export_targets() puts hipblas-config.cmake, so one CMAKE_PREFIX_PATH
# entry finds both halves of the package.
#
# COMPONENT devel on every rule: an archive, .mod files, the generated source
# and the CMake package are all development artifacts. It is also what
# rocm-cmake defaults to, and what the hipblas_module.f90 these replace shipped
# as, so omitting it would be a packaging regression. Plain install(), not
# rocm_install(): standalone, rocm-cmake was never loaded.
install(TARGETS hipblas_fortran
        EXPORT hipblas-fortran-targets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}/fortran/${HIPBLAS_FORTRAN_COMPILER_DIR}"
                COMPONENT devel)
install(DIRECTORY "${_moddir}/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/fortran/${HIPBLAS_FORTRAN_COMPILER_DIR}"
        COMPONENT devel
        FILES_MATCHING PATTERN "*.mod")

# The generated source, so a user on a compiler ROCm ships no .mod for can
# rebuild the binding, from source that cannot drift from the installed
# libhipblas. Under share/, not include/: it is not compiler-specific, and the
# config below enumerates include/fortran/ to report which compilers are
# installed, so a source file among them would be listed as one.
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/hipblas.F90"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/hipblas/fortran"
        COMPONENT devel)
install(EXPORT hipblas-fortran-targets
        FILE hipblas-fortran-targets.cmake
        NAMESPACE roc::
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/hipblas"
        COMPONENT devel)

# Kept out of the template so that neither side spells lib/ or include/
# literally: a literal lib/fortran is wrong the moment CMAKE_INSTALL_LIBDIR is
# lib64, and the config would hand out an IMPORTED_LOCATION that does not exist.
set(HIPBLAS_FORTRAN_LIBDIR     "${CMAKE_INSTALL_LIBDIR}/fortran")
set(HIPBLAS_FORTRAN_INCLUDEDIR "${CMAKE_INSTALL_INCLUDEDIR}/fortran")

configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/hipblas-fortran-config.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/hipblas-fortran-config.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/hipblas"
  # PATH_VARS: @PACKAGE_INIT@ rewrites each into an absolute path computed from
  # where the config was FOUND, so the package survives relocation and gets
  # lib64 right. HIPBLAS_FORTRAN_COMPILER_DIR stays a plain @-substitution: it
  # is a bare directory NAME, and PATH_VARS would resolve it under the prefix.
  PATH_VARS HIPBLAS_FORTRAN_LIBDIR HIPBLAS_FORTRAN_INCLUDEDIR)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/hipblas-fortran-config.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/hipblas"
        COMPONENT devel)

# The real config sits in lib/cmake/hipblas so that hipblas-config.cmake could
# pick it up with a bare include(... OPTIONAL) once rocm_export_targets grows a
# hook for it. But find_package(hipblas-fortran), the documented spelling,
# searches lib/cmake/hipblas-fortran*, which that does not match, so it needs
# somewhere to land. A forwarder rather than a second copy of the config, which
# would need its own targets file and drift from this one.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/forward/hipblas-fortran-config.cmake"
"# Generated forwarder. The real config lives beside the hipBLAS C package, in
# lib/cmake/hipblas, so that hipblas-config.cmake can include it as a sibling.
# This file only exists so that find_package(hipblas-fortran) resolves to the
# same place.
include(\"\${CMAKE_CURRENT_LIST_DIR}/../hipblas/hipblas-fortran-config.cmake\")
")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/forward/hipblas-fortran-config.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/hipblas-fortran"
        COMPONENT devel)

# A version file, so that find_package(hipblas-fortran 3.8) resolves rather than
# failing on a missing one. SameMajorVersion, because the module's bind(C)
# surface tracks the C API's compatibility promise. Read from THIS tree's
# top-level CMakeLists, not from hipblas_VERSION: in a standalone build that
# variable is set by the find_package(hipblas) above and reports whatever
# hipBLAS is INSTALLED, not the headers these bindings were generated from.
set(_fortran_version "")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../CMakeLists.txt")
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../CMakeLists.txt" _hipblas_root)
  # hipBLAS spells it set ( VERSION_STRING "3.8.0" ), note the spaces, and passes
  # the variable to rocm_setup_version, so match the assignment, not the call.
  string(REGEX MATCH "set[ \t]*\\([ \t]*VERSION_STRING[ \t]+\"([0-9]+\\.[0-9]+\\.[0-9]+)\""
         _m "${_hipblas_root}")
  if(CMAKE_MATCH_1)
    set(_fortran_version "${CMAKE_MATCH_1}")
  endif()
  unset(_hipblas_root)
endif()

if(_fortran_version)
  write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/hipblas-fortran-config-version.cmake"
    VERSION "${_fortran_version}"
    COMPATIBILITY SameMajorVersion)
  # In BOTH package directories, necessarily: CMake looks for
  # <pkg>-config-version.cmake NEXT TO the <pkg>-config.cmake it actually
  # resolved. find_package(hipblas-fortran 3.8) resolves the forwarder, so a
  # version file only beside the real config is never consulted and the
  # versioned call fails even though the version matches.
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/hipblas-fortran-config-version.cmake"
          DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/hipblas"
          COMPONENT devel)
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/hipblas-fortran-config-version.cmake"
          DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/hipblas-fortran"
          COMPONENT devel)
else()
  # Better no version file than a wrong one: without it an unversioned
  # find_package still succeeds, whereas a wrong number makes a versioned one
  # succeed and link the wrong .mod.
  message(STATUS
    "hipBLAS: could not determine the version for the Fortran package, so no "
    "hipblas-fortran-config-version.cmake is installed; "
    "find_package(hipblas-fortran <version>) will not match.")
endif()
