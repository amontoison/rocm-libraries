# Fortran bindings for hipSPARSE.
#
# hipsparse.F90 is GENERATED from the hipSPARSE C headers by the rocm-fortran
# generator and committed next to the library it binds, so a header change and
# its binding travel in the same pull request. Do not edit it: regenerate it.
#
# It is one self-contained module, `hipsparse`, carrying the bind(C) interfaces,
# the enum constants and the hipsparseCheck status helper. Every handle,
# descriptor and info object stays the opaque type(c_ptr) the C API hands back:
# no derived types, no _typed or _dptr overloads, no docstrings. Array overloads
# cover only the subset of routines the generator supports; every other routine
# is reached through type(c_ptr) under every tier.
#
# A Fortran binding compiles against nothing, so this directory needs no
# hipSPARSE C target to build. libhipsparse is required to LINK an executable,
# and is carried as the target's link interface.
#
# This REPLACES the hand-written library/src/hipsparse.f90 and
# hipsparse_enums.f90, which were compiled into an OBJECT library for the
# in-tree samples and never installed -- calling hipSPARSE from Fortran outside
# this tree had no supported path at all. This installs as an archive plus a
# .mod set, and `use hipsparse` carries the enum constants too.

cmake_minimum_required(VERSION 3.19)

# Standalone (not a subdirectory of the hipSPARSE build) this is its own
# project, so the bindings can be built and packaged alone -- the supported path
# for a Fortran compiler other than the amdflang ROCm ships them built with.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(hipsparse-fortran LANGUAGES NONE)
endif()

# Guarded, so a site with no Fortran compiler can still build the C library.
# "${UNIX}" rather than ON because ROCm ships no Fortran compiler on Windows at
# all: TheRock adds flang to LLVM_ENABLE_PROJECTS only in the non-WIN32 branch
# of compiler/pre_hook_amd-llvm.cmake, so the amdflang preference below finds
# nothing, the probe falls through to a stray gfortran, and
# enable_language(Fortran) fails to identify it. hipSPARSE gates
# HIPSPARSE_ENABLE_FORTRAN the same way in its root.
#
# OFF on the CUDA backend, and that is about the GENERATED SOURCE rather than
# the compiler. hipsparse.F90 is generated from the ROCm path and declares all
# 557 entry points, but the public headers are CUDART_VERSION-conditional, so a
# cuSPARSE-backed libhipsparse implements only a subset. At the default
# assumed-shape tier the module object REFERENCES 361 of them -- the CONTAINS
# wrappers call the bind(c) interfaces -- and roughly 119 are absent from a CUDA
# 12.x libhipsparse (44 on CUDA 11.8, 242 on CUDA 13). hipsparseCheck lives in
# that same object, so merely saying `use hipsparse` drags in every dead
# reference and the link fails. The none tier emits none and is the one safe
# setting there; the warning further down says so.
#
# HIPSPARSE_ENABLE_CUDA is hipSPARSE's own backend switch, set by the root
# before this directory is added. Standalone it is undefined and the default
# falls back to ${UNIX}, which is right: that build has no backend of its own.
if(HIPSPARSE_ENABLE_CUDA)
  set(_hipsparse_fortran_default OFF)
else()
  set(_hipsparse_fortran_default "${UNIX}")
endif()
# The ONLY option(BUILD_FORTRAN_BINDINGS) in hipSPARSE, deliberately: the first
# option() call in a CMake run fixes the cache entry, so a second one in the
# root with a different default would be silently ignored by whichever ran
# second and the two would read as disagreeing.
option(BUILD_FORTRAN_BINDINGS "Build the hipSPARSE Fortran bindings"
       "${_hipsparse_fortran_default}")
unset(_hipsparse_fortran_default)

# In hipSPARSE this switch gates only the binding tests in fortran/test; the
# Fortran samples follow HIPSPARSE_ENABLE_FORTRAN instead. ON by default, but
# guarded like the bindings: asking for the tests when the bindings were not
# built is a skip, not an error.
option(BUILD_FORTRAN_CLIENTS "Build the hipSPARSE Fortran binding tests" ON)

# BUILD_FORTRAN_BINDINGS and BUILD_FORTRAN_CLIENTS are ONE cache entry shared by
# every library in the monorepo, so -DBUILD_FORTRAN_BINDINGS=OFF from the top
# turns the whole Fortran track off. Each library also honours a name of its own
# for when one has to be singled out. That override is the last word for the
# BINDINGS only: in-tree the binding TESTS are capped further down by hipSPARSE's
# own client/test switch, so an explicit -DHIPSPARSE_BUILD_FORTRAN_CLIENTS=ON
# raises them no higher than the library's own tests go.
if(DEFINED HIPSPARSE_BUILD_FORTRAN_BINDINGS)
  set(BUILD_FORTRAN_BINDINGS ${HIPSPARSE_BUILD_FORTRAN_BINDINGS})
endif()
if(DEFINED HIPSPARSE_BUILD_FORTRAN_CLIENTS)
  set(BUILD_FORTRAN_CLIENTS ${HIPSPARSE_BUILD_FORTRAN_CLIENTS})
endif()

# Which array-argument overloads the module exposes. One tri-state rather than a
# boolean per tier, because the two upper tiers are alternatives, not layers:
# the generator emits the assumed-rank specific INSTEAD OF the per-rank ones, so
# there is no combination in which both sets exist. Array overloads exist only
# for the subset of routines the generator supports; every other routine keeps
# the plain type(c_ptr) interface under all three.
#
#   none           the raw type(c_ptr) surface only (Fortran 2003)
#   assumed-shape  + the per-rank array overloads (Fortran 2008): the default,
#                  and what the bindings shipped in ROCm are built with
#   assumed-rank   + one dimension(..) overload per overloaded routine
#                  (Fortran 2018)
set(FORTRAN_ARRAY_INTERFACES "assumed-shape" CACHE STRING
    "Array-argument overloads to expose: none, assumed-shape, or assumed-rank")
set_property(CACHE FORTRAN_ARRAY_INTERFACES PROPERTY STRINGS
             none assumed-shape assumed-rank)

# Reports whether the bindings were really BUILT, unlike ROCM_HAVE_FORTRAN,
# which says only that a compiler exists. Guarded because standalone this is the
# top-level scope, where PARENT_SCOPE warns under -Wdev and therefore fails
# under -Werror=dev, and standalone has to configure cleanly.
if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(HIPSPARSE_HAVE_FORTRAN_BINDINGS FALSE PARENT_SCOPE)
endif()

if(NOT BUILD_FORTRAN_BINDINGS)
  return()
endif()

# Reached on the CUDA backend only when the default above was overridden by
# hand. Warn rather than fail: the bindings DO build there, and at the none tier
# they also link, so an explicit request is honoured. The two upper tiers emit
# CONTAINS wrappers that reference entry points a cuSPARSE-backed libhipsparse
# does not define, and because hipsparseCheck shares that object every consumer
# of the module inherits them.
if(HIPSPARSE_ENABLE_CUDA AND NOT FORTRAN_ARRAY_INTERFACES STREQUAL "none")
  message(WARNING
    "hipSPARSE: the Fortran bindings were requested on the CUDA backend. "
    "hipsparse.F90 is generated from the ROCm path and the "
    "'${FORTRAN_ARRAY_INTERFACES}' tier references 361 entry points, around "
    "119 of which a CUDA 12.x libhipsparse does not define (44 on CUDA 11.8, "
    "242 on CUDA 13); anything that uses the module will fail to link. "
    "Configure with -DFORTRAN_ARRAY_INTERFACES=none, which emits no such "
    "references, or leave BUILD_FORTRAN_BINDINGS at its default.")
endif()

# The monorepo root probes for a Fortran compiler ONCE and publishes
# ROCM_HAVE_FORTRAN. That name is deliberately neither rocm-libraries' nor
# rocm-systems': hipSPARSE is regularly built inside a rocm-systems tree, and a
# repo-specific name would read the wrong variable, or none. The rocm-libraries
# root still publishes ROCM_LIBS_HAVE_FORTRAN, honoured as a legacy spelling.
# The local probe is the standalone path, where there is no root to ask.
if(DEFINED ROCM_HAVE_FORTRAN)
  set(_have_fortran ${ROCM_HAVE_FORTRAN})
elseif(DEFINED ROCM_LIBS_HAVE_FORTRAN)
  set(_have_fortran ${ROCM_LIBS_HAVE_FORTRAN})
else()
  include(CheckLanguage)
  include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/ROCmFortranCompiler.cmake")

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

  # Express the amdflang preference before probing: check_language() is a no-op
  # once CMAKE_Fortran_COMPILER is defined, so whatever this leaves behind is
  # what the probe reports, and the probe never overrides it. The macro lives in
  # cmake/ROCmFortranCompiler.cmake because hipSPARSE's root CMakeLists has to
  # make the same choice, earlier, before its own enable_language(Fortran).
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
  message(STATUS "hipSPARSE: no Fortran compiler found, skipping the Fortran bindings")
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

add_library(hipsparse_fortran STATIC hipsparse.F90)

# roc::, the namespace hipSPARSE exports its C library under, so a consumer is
# not made to remember two namespaces for two halves of one package.
add_library(roc::hipsparse_fortran ALIAS hipsparse_fortran)

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
# the SAME compiler sets HIPSPARSE_FORTRAN_COMPILER_DIR itself, e.g.
# gfortran-13.
set(HIPSPARSE_FORTRAN_COMPILER_DIR "${_fc_name}" CACHE STRING
    "Per-compiler subdirectory the .mod and the archive install under")

set(_moddir "${CMAKE_CURRENT_BINARY_DIR}/include")
set_target_properties(hipsparse_fortran PROPERTIES
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
# Compiling hipsparse.F90 by hand with both set is harmless but yields
# assumed-rank -- the interface region and the CONTAINS region both test
# USE_ASSUMED_RANK first, so the module still compiles (checked clean with
# amdflang -cpp -fsyntax-only).
if(FORTRAN_ARRAY_INTERFACES STREQUAL "assumed-shape")
  target_compile_definitions(hipsparse_fortran PRIVATE USE_ASSUMED_SHAPE=1)
elseif(FORTRAN_ARRAY_INTERFACES STREQUAL "assumed-rank")
  target_compile_definitions(hipsparse_fortran PRIVATE USE_ASSUMED_RANK=1)
endif()

# No line-length flag: the generator wraps the emitted source at 112 columns,
# inside the 132 free-form Fortran guarantees, so every compiler reads it
# without a vendor-specific option.

target_include_directories(hipsparse_fortran PUBLIC
  "$<BUILD_INTERFACE:${_moddir}>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/fortran/${HIPSPARSE_FORTRAN_COMPILER_DIR}>")

# Link interface only: roc::hipsparse is needed to link an executable, not to
# build this archive. Standalone it has to be looked for -- testing for the
# target without ever calling find_package would mean it is never found, the
# archive would export no link interface at all, and every consumer would hit
# undefined references despite linking roc::hipsparse_fortran as documented.
#
# hipsparse-config.cmake does find_dependency(hip), whose hip-config.cmake
# reaches FindThreads and hard-errors unless C or CXX is enabled, so C is
# enabled here (it compiles nothing). QUIET: the bindings build with no
# hipSPARSE installed.
if(NOT TARGET roc::hipsparse)
  if(NOT CMAKE_C_COMPILER_LOADED AND NOT CMAKE_CXX_COMPILER_LOADED)
    enable_language(C)
  endif()
  find_package(hipsparse QUIET)
endif()

# Whether the archive really carries roc::hipsparse in its link interface is
# what the installed config has to know and cannot work out for itself -- it
# sees only the targets file -- so record it here for the template. Its
# find_dependency(hipsparse) exists solely to make roc::hipsparse exist before
# the targets file names it; a binding built with no C library in sight names
# nothing, so requiring the package there would be an invented requirement.
if(TARGET roc::hipsparse)
  target_link_libraries(hipsparse_fortran INTERFACE roc::hipsparse)
  set(HIPSPARSE_FORTRAN_NEEDS_C_PACKAGE TRUE)
else()
  set(HIPSPARSE_FORTRAN_NEEDS_C_PACKAGE FALSE)
  message(STATUS
    "hipSPARSE: the C library was not found, so the Fortran bindings carry no "
    "link interface; a consumer will have to link it (-lhipsparse) itself.")
endif()

if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(HIPSPARSE_HAVE_FORTRAN_BINDINGS TRUE PARENT_SCOPE)
endif()

# The binding tests below are GPU runtime executables, so in-tree they belong to
# the client/test track: a library-only build has no business producing test
# binaries. The gate is unconditional rather than deferring to an explicit
# HIPSPARSE_BUILD_FORTRAN_CLIENTS=ON, and its condition is the one hipSPARSE
# guards add_subdirectory(clients) with, so the two cannot disagree.
#
# Standalone the tests are the point, so the library's switch does not exist and
# BUILD_FORTRAN_CLIENTS stands on its own. Set as a plain variable deliberately:
# the cache entry is one switch shared by every library, so turning it off here
# must stay scoped to this directory and must not be written back.
#
# Placed immediately before the switch is read, rather than beside the option()
# above: everything between the two can return early, and a message about
# skipping tests would then blame a reason that is not the one that applied.
if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR
   AND BUILD_FORTRAN_CLIENTS
   AND NOT (HIPSPARSE_ENABLE_CLIENT OR HIPSPARSE_BUILD_TESTING OR BUILD_TESTING))
  message(STATUS
    "hipSPARSE: clients are not being built, skipping the Fortran binding tests.")
  set(BUILD_FORTRAN_CLIENTS OFF)
endif()

# Under ASAN libhipsparse is instrumented and the uninstrumented Fortran test
# executables cannot link against it (rocSPARSE turns its Fortran clients off
# for the same reason).
if(BUILD_FORTRAN_CLIENTS AND HIPSPARSE_ENABLE_ASAN)
  message(STATUS
    "hipSPARSE: address sanitizer is enabled, skipping the Fortran binding tests.")
  set(BUILD_FORTRAN_CLIENTS OFF)
endif()

if(BUILD_FORTRAN_CLIENTS)
  # Load-bearing on both paths. Standalone this is the only enable_testing() in
  # the build. In-tree it is still needed, because hipSPARSE's root adds this
  # directory BEFORE its own call, so the directory property is not inherited
  # here; it is the root's later call that puts this directory into the chain of
  # CTestTestfile.cmake ctest walks down from the build root. Nothing here can
  # check that the root really made that call: it has not run yet, so probing
  # the global property CTest sets would warn on every in-tree build.
  enable_testing()
  add_subdirectory(test)
endif()

include(CMakePackageConfigHelpers)

# Beside hipsparse-config.cmake, so one CMAKE_PREFIX_PATH entry finds both
# halves. rocRAND and hipRAND cannot do this and install to
# lib/cmake/<lib>-fortran alone: their shipped <lib>-config.cmake carries a bare
# unguarded include() of a sibling <lib>-fortran-config.cmake, so a file here
# would recurse. hipsparse-config.cmake mentions Fortran nowhere, checked
# against an installed ROCm, so it uses the shared layout.
set(_fortran_pkgdir "${CMAKE_INSTALL_LIBDIR}/cmake/hipsparse")

# COMPONENT devel on every rule below: an archive, a set of .mod files, the
# generated source and the CMake package are all development artifacts, and it
# is what rocm-cmake defaults to. Without it they land in the runtime component,
# where the user who installs devel to build against hipSPARSE never sees them.
# Plain install(), not rocm_install(): standalone, rocm-cmake was never loaded.
install(TARGETS hipsparse_fortran
        EXPORT hipsparse-fortran-targets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}/fortran/${HIPSPARSE_FORTRAN_COMPILER_DIR}"
                COMPONENT devel)
install(DIRECTORY "${_moddir}/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/fortran/${HIPSPARSE_FORTRAN_COMPILER_DIR}"
        COMPONENT devel
        FILES_MATCHING PATTERN "*.mod")

# The generated source, so a user on a compiler ROCm ships no .mod for can
# rebuild the binding, from source that cannot drift from the installed
# libhipsparse. Under share/, not include/: it is not compiler-specific, and the
# config below enumerates include/fortran/ to report which compilers are
# installed, so a source file among them would be listed as one.
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/hipsparse.F90"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/hipsparse/fortran"
        COMPONENT devel)
install(EXPORT hipsparse-fortran-targets
        FILE hipsparse-fortran-targets.cmake
        NAMESPACE roc::
        DESTINATION "${_fortran_pkgdir}"
        COMPONENT devel)

# Kept out of the template so that neither side spells lib/ or include/
# literally: a literal lib/fortran is wrong the moment CMAKE_INSTALL_LIBDIR is
# lib64, and the config would hand out an IMPORTED_LOCATION that does not exist.
set(HIPSPARSE_FORTRAN_LIBDIR     "${CMAKE_INSTALL_LIBDIR}/fortran")
set(HIPSPARSE_FORTRAN_INCLUDEDIR "${CMAKE_INSTALL_INCLUDEDIR}/fortran")

configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/hipsparse-fortran-config.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/hipsparse-fortran-config.cmake"
  INSTALL_DESTINATION "${_fortran_pkgdir}"
  # PATH_VARS: @PACKAGE_INIT@ rewrites each into an absolute path computed from
  # where the config was FOUND, so the package survives relocation and gets
  # lib64 right. HIPSPARSE_FORTRAN_COMPILER_DIR stays a plain @-substitution: it
  # is a bare directory NAME, and PATH_VARS would resolve it under the prefix.
  PATH_VARS HIPSPARSE_FORTRAN_LIBDIR HIPSPARSE_FORTRAN_INCLUDEDIR)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/hipsparse-fortran-config.cmake"
        DESTINATION "${_fortran_pkgdir}"
        COMPONENT devel)

# The real config lands in lib/cmake/hipsparse, beside the C package, so that
# hipsparse-config.cmake could pick it up with a bare include(... OPTIONAL) once
# rocm_export_targets grows a hook for it. Its existing INCLUDE argument is NOT
# that hook: it emits a non-OPTIONAL include(), and emits it BEFORE the targets
# file, so a config reached that way would run before roc::hipsparse exists.
#
# find_package(hipsparse-fortran), the documented spelling, searches
# lib/cmake/hipsparse-fortran*, which lib/cmake/hipsparse does not match, so it
# needs somewhere to land. A forwarder rather than a second copy of the config,
# which would need its own targets file and be a second thing to keep in step.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/forward/hipsparse-fortran-config.cmake"
"# Generated forwarder. The real config lives beside the hipSPARSE C package, in
# lib/cmake/hipsparse, so that hipsparse-config.cmake can include it as a sibling.
# This file only exists so that find_package(hipsparse-fortran) resolves to the
# same place.
include(\"\${CMAKE_CURRENT_LIST_DIR}/../hipsparse/hipsparse-fortran-config.cmake\")
")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/forward/hipsparse-fortran-config.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/hipsparse-fortran"
        COMPONENT devel)

# A version file, so that find_package(hipsparse-fortran 4.8) resolves rather
# than failing on a missing one. SameMajorVersion, because the module's bind(C)
# surface tracks the C API's compatibility promise. Read from THIS tree's
# top-level CMakeLists, not from hipsparse_VERSION: in a standalone build that
# variable is set by the find_package(hipsparse) above and reports whatever
# hipSPARSE is INSTALLED, not the headers these bindings were generated from.
set(_fortran_version "")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../CMakeLists.txt")
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../CMakeLists.txt" _hipsparse_root)
  # hipSPARSE carries its version on the project() call itself,
  # project(hipsparse VERSION 4.8.0 ...), not in a VERSION_STRING variable.
  string(REGEX MATCH "project[ \t]*\\([ \t]*hipsparse[ \t]+VERSION[ \t]+([0-9]+\\.[0-9]+\\.[0-9]+)"
         _m "${_hipsparse_root}")
  if(CMAKE_MATCH_1)
    set(_fortran_version "${CMAKE_MATCH_1}")
  endif()
  unset(_hipsparse_root)
endif()

if(_fortran_version)
  write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/hipsparse-fortran-config-version.cmake"
    VERSION "${_fortran_version}"
    COMPATIBILITY SameMajorVersion)
  # In BOTH package directories, necessarily: CMake looks for
  # <pkg>-config-version.cmake NEXT TO the <pkg>-config.cmake it actually
  # resolved. find_package(hipsparse-fortran 4.8) resolves the forwarder, so a
  # version file only beside the real config is never consulted and the
  # versioned call fails even though the version matches.
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/hipsparse-fortran-config-version.cmake"
          DESTINATION "${_fortran_pkgdir}"
          COMPONENT devel)
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/hipsparse-fortran-config-version.cmake"
          DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/hipsparse-fortran"
          COMPONENT devel)
else()
  # Better no version file than a wrong one: without it an unversioned
  # find_package still succeeds, whereas a wrong number makes a versioned one
  # succeed and link the wrong .mod.
  message(STATUS
    "hipSPARSE: could not determine the version for the Fortran package, so no "
    "hipsparse-fortran-config-version.cmake is installed; "
    "find_package(hipsparse-fortran <version>) will not match.")
endif()
