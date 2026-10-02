# Fortran bindings for hipSPARSE.
#
# hipsparse.F90 is GENERATED from the hipSPARSE C headers by the rocm-fortran
# generator and committed here, next to the library it binds, so that a header
# change and its binding travel in the same pull request. Do not edit it by
# hand: regenerate it instead.
#
# The bindings are a single self-contained Fortran module, `hipsparse`, carrying
# the bind(C) interfaces, the enum constants and the hipsparseCheck status
# helper, and nothing beyond them: the module declares no derived types of its
# own (grep finds no `type, bind(c)`), no strongly-typed `_typed` overloads and
# no `_dptr` device-pointer specifics, and it carries no docstrings, the C
# headers being the single place that prose lives. Every hipSPARSE handle,
# descriptor and info object stays the opaque type(c_ptr) the C API hands back,
# so there is nothing to keep in step with the headers beyond the interfaces
# themselves. A consumer writes one `use hipsparse`.
#
# This directory is self-contained: it builds with no dependency on the hipSPARSE
# C target, because a Fortran binding compiles against nothing (it only declares
# bind(C) interfaces). libhipsparse is required to LINK a final executable, and is
# carried as the target's link interface for that reason.
#
# This binding REPLACES the hand-written library/src/hipsparse.f90 and
# hipsparse_enums.f90. Those were compiled into an OBJECT library for the
# in-tree Fortran samples and were never installed, so calling hipSPARSE from
# Fortran outside this tree had no supported path at all. This binding installs
# as an archive plus a .mod set, so there now is one. The separate
# hipsparse_enums module goes with them: `use hipsparse` carries the enum
# constants too.

cmake_minimum_required(VERSION 3.19)

# Configured directly (not as a subdirectory of the hipSPARSE build), this
# directory is its own project, so that the bindings can be built and packaged
# on their own, which is the supported path for anyone whose Fortran compiler
# is not the amdflang the shipped bindings were built with. In-tree, hipSPARSE's
# own project() is already in effect.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(hipsparse-fortran LANGUAGES NONE)
endif()

# ON by default but GUARDED: a site with no Fortran compiler must still
# configure and build the C library. The monorepo root already probes once and
# publishes ROCM_HAVE_FORTRAN; a standalone hipSPARSE build falls back to its own
# probe.
#
# "${UNIX}" rather than ON, because Windows is not such a site: ROCm ships no
# Fortran compiler there at all. TheRock puts flang in LLVM_ENABLE_PROJECTS
# only in the non-WIN32 branch of compiler/pre_hook_amd-llvm.cmake, so the
# amdflang preference below finds nothing, the probe falls through to whatever
# gfortran happens to be on PATH, and enable_language(Fortran) then fails to
# identify it. hipSPARSE already gates HIPSPARSE_ENABLE_FORTRAN this way in its
# root, which is what keeps the root's own enable_language(Fortran) off
# Windows. A default and not a veto: -DBUILD_FORTRAN_BINDINGS=ON still works
# wherever a usable compiler exists.
#
# OFF on the CUDA backend, and that one is about the GENERATED SOURCE rather
# than about the compiler. hipsparse.F90 is generated from the ROCm path through
# the public headers and declares all 557 entry points, but those headers are
# CUDART_VERSION-conditional (59 of them, mirrored by 57 files under
# library/src/nvidia_detail/), so a cuSPARSE-backed libhipsparse implements only
# a subset. At the default assumed-shape tier the module object REFERENCES 361
# of the entry points -- the CONTAINS wrappers call the bind(c) interfaces --
# and roughly 119 of those are absent from a CUDA 12.x libhipsparse (44 on CUDA
# 11.8, 242 on CUDA 13). hipsparseCheck lives in that same object, so merely
# saying `use hipsparse` drags in every dead reference and the link fails. The
# none tier emits no such references and is the one safe setting there; the
# warning further down says so.
#
# HIPSPARSE_ENABLE_CUDA is hipSPARSE's own backend switch, set by the root
# before this directory is added. Standalone it is simply undefined and the
# default falls back to ${UNIX}, which is correct: a standalone build of the
# bindings has no backend of its own.
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

# The bindings' own test suite, and the Fortran clients where a library has
# them: one switch rather than two neighbouring ones. ON by default, and
# guarded the same way as the bindings, so asking for them when the bindings
# were not built is a skip rather than an error.
option(BUILD_FORTRAN_CLIENTS "Build the hipSPARSE Fortran clients and binding tests" ON)

# BUILD_FORTRAN_BINDINGS and BUILD_FORTRAN_CLIENTS are ONE cache entry shared by
# every library in the monorepo, which is what you want for the common case:
# -DBUILD_FORTRAN_BINDINGS=OFF from the top turns the whole Fortran track off in
# a single flag. It is not what you want when one library has to be singled out,
# so each library also honours a name of its own, which overrides the shared
# default.
#
# That override is the last word for the BINDINGS only. In-tree the binding
# TESTS are capped further down by hipSPARSE's own client/test switch, because
# they are GPU test executables and belong to that track: an explicit
# -DHIPSPARSE_BUILD_FORTRAN_CLIENTS=ON raises them no higher than the library's
# own tests go. Standalone there is no such switch and the override stands on
# its own.
if(DEFINED HIPSPARSE_BUILD_FORTRAN_BINDINGS)
  set(BUILD_FORTRAN_BINDINGS ${HIPSPARSE_BUILD_FORTRAN_BINDINGS})
endif()
if(DEFINED HIPSPARSE_BUILD_FORTRAN_CLIENTS)
  set(BUILD_FORTRAN_CLIENTS ${HIPSPARSE_BUILD_FORTRAN_CLIENTS})
endif()

# Which array-argument overloads the module exposes. One tri-state rather than a
# pair of independent booleans, because the two upper tiers are alternatives and
# not layers: the generator emits the assumed-rank specific INSTEAD OF the
# per-rank ones, so there is no combination in which both sets exist and no
# second boolean to offer.
#
# Array overloads are generated for the subset of routines the generator
# supports; every other routine exposes the same plain type(c_ptr) interface
# under all three settings.
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

# Probing for a Fortran compiler is not free, and every library in a monorepo
# build would otherwise repeat it, so the monorepo root probes ONCE and publishes
# the answer as ROCM_HAVE_FORTRAN. That name is deliberately neither
# rocm-libraries' nor rocm-systems': hipSPARSE is regularly built inside a
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

add_library(hipsparse_fortran STATIC hipsparse.F90)

# roc::, matching the namespace hipSPARSE exports its C library under (see
# library/CMakeLists.txt), so that a consumer is not made to remember that two
# halves of one package sit in different namespaces.
add_library(roc::hipsparse_fortran ALIAS hipsparse_fortran)

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
# HIPSPARSE_FORTRAN_COMPILER_DIR itself, for example to gfortran-13.3.0.
set(HIPSPARSE_FORTRAN_COMPILER_DIR "${_fc_name}" CACHE STRING
    "Per-compiler subdirectory the .mod and the archive install under")

set(_moddir "${CMAKE_CURRENT_BINARY_DIR}/include")
set_target_properties(hipsparse_fortran PROPERTIES
  Fortran_MODULE_DIRECTORY "${_moddir}"
  # hipsparse.F90 is uppercase: the array overloads sit behind preprocessor
  # guards, so the source must go through the C preprocessor.
  Fortran_PREPROCESS ON
  # Free form is the .F90 default, but stating it lets CMake emit whichever flag
  # the compiler in use expects rather than relying on the extension.
  Fortran_FORMAT FREE
  POSITION_INDEPENDENT_CODE ON)

# Translate the tri-state into the macro the generated source is guarded with.
# The option value and the macro name are the same word, and the two tiers are
# mutually exclusive, so exactly one macro is defined below and never both:
# none defines neither and leaves the raw type(c_ptr) surface the C API is
# bound to.
#
# Defining both is not a supported combination, but it is not a trap either:
# the generated file guards its interface region and its CONTAINS region in the
# same order, testing USE_ASSUMED_RANK first in both, so with both defined the
# assumed-rank specifics win and the module still compiles. Checked with
#
#     amdflang -cpp -DUSE_ASSUMED_SHAPE=1 -DUSE_ASSUMED_RANK=1 \
#       -fsyntax-only hipsparse.F90
#
# which accepts the module. This file asks for exactly one either way, so which
# tier wins is never in question here.
if(FORTRAN_ARRAY_INTERFACES STREQUAL "assumed-shape")
  target_compile_definitions(hipsparse_fortran PRIVATE USE_ASSUMED_SHAPE=1)
elseif(FORTRAN_ARRAY_INTERFACES STREQUAL "assumed-rank")
  target_compile_definitions(hipsparse_fortran PRIVATE USE_ASSUMED_RANK=1)
endif()

# No line-length flag: the generator wraps the emitted source at 112 columns,
# inside the 132 that free-form Fortran guarantees, so every compiler reads it
# without a vendor-specific option.

target_include_directories(hipsparse_fortran PUBLIC
  "$<BUILD_INTERFACE:${_moddir}>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/fortran/${HIPSPARSE_FORTRAN_COMPILER_DIR}>")

# Link interface only: needed to link an executable, not to build this archive.
# roc::hipsparse is defined by hipSPARSE's library/, and brings the HIP runtime with
# it. Standalone it has to be looked for, and testing for the target without
# ever calling find_package would mean it is never found: the archive would then
# export no link interface at all, and every consumer would hit undefined
# references to hipsparse_create_handle and friends despite linking
# roc::hipsparse_fortran exactly as documented.
#
# hipsparse-config.cmake does find_dependency(hip), and hip-config.cmake reaches
# FindThreads, which hard-errors unless C or CXX is enabled. This project
# declares LANGUAGES NONE and then enables only Fortran, so C is enabled first;
# it compiles none of the sources here. QUIET, because a site that has no
# installed hipSPARSE can still legitimately build the bindings: they compile
# against nothing.
if(NOT TARGET roc::hipsparse)
  if(NOT CMAKE_C_COMPILER_LOADED AND NOT CMAKE_CXX_COMPILER_LOADED)
    enable_language(C)
  endif()
  find_package(hipsparse QUIET)
endif()

# Whether the archive really carries roc::hipsparse in its link interface is
# also what the installed config package has to know, and it cannot work it out
# for itself: it sees only the targets file. So record it here and interpolate
# it into the template. The consumer-side find_dependency(hipsparse) exists
# solely to make roc::hipsparse exist before the targets file names it; a
# binding built with no C library in sight names nothing, and making its
# consumers find a package the artifact does not depend on would be an invented
# requirement.
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
# the client/test track and have to follow the library's own switch: a
# library-only build has no business producing test binaries, and would be made
# to link a GPU runtime it never asked for. That holds without exception, which
# is why the gate is unconditional rather than deferring to an explicit
# HIPSPARSE_BUILD_FORTRAN_CLIENTS=ON. The condition is the one hipSPARSE guards
# add_subdirectory(clients) with, so the two cannot disagree.
#
# Standalone, this directory IS the build and the tests are the point, so the
# library's switch does not exist and BUILD_FORTRAN_CLIENTS stands on its own.
# Setting it as a plain variable is deliberate: the cache entry is one switch
# shared by every library in the monorepo, so turning it off here has to be
# scoped to this directory and must not be written back.
#
# Placed here, immediately before the switch is read, rather than beside the
# option() above: everything between the two can return early -- a
# -DBUILD_FORTRAN_BINDINGS=OFF configure, or a host with no Fortran compiler --
# and a message about skipping tests would then be printed for a directory that
# configures nothing at all, blaming a reason that is not the one that applied.
if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR
   AND BUILD_FORTRAN_CLIENTS
   AND NOT (HIPSPARSE_ENABLE_CLIENT OR HIPSPARSE_BUILD_TESTING OR BUILD_TESTING))
  message(STATUS
    "hipSPARSE: clients are not being built, skipping the Fortran binding tests.")
  set(BUILD_FORTRAN_CLIENTS OFF)
endif()

if(BUILD_FORTRAN_CLIENTS)
  # Load-bearing on both paths. Standalone this is the only enable_testing() in
  # the build. In-tree it is still needed, because hipSPARSE's root adds this
  # directory BEFORE its own call, so the directory property is not inherited
  # here; it is the root's later call that puts this directory into the chain of
  # CTestTestfile.cmake ctest walks down from the build root.
  #
  # Nothing here can check that the root really made that call. The gate above
  # has just established that we are inside the exact condition the root guards
  # it with, and the call has not run yet at this point, so a probe of the
  # global property CTest sets would read unset on every in-tree build and warn
  # about a problem that is not there.
  enable_testing()
  add_subdirectory(test)
endif()

include(CMakePackageConfigHelpers)

# lib/cmake/hipsparse is where hipSPARSE's own rocm_export_targets() puts
# hipsparse-config.cmake, so the Fortran package lands beside the C one and a
# single CMAKE_PREFIX_PATH entry finds both.
#
# rocRAND and hipRAND cannot do this and install into lib/cmake/<lib>-fortran
# alone, because their shipped <lib>-config.cmake carries a bare unguarded
# include() of a sibling <lib>-fortran-config.cmake, and putting a file there
# would recurse. hipSPARSE has no such include, checked against an installed
# ROCm, hipsparse-config.cmake mentions Fortran nowhere, so it uses the shared
# layout, like hipBLAS and the FFT libraries.
set(_fortran_pkgdir "${CMAKE_INSTALL_LIBDIR}/cmake/hipsparse")

# COMPONENT devel on every rule below, because everything this directory
# installs -- a static archive, a set of .mod files, the generated source and
# the CMake package -- is a development artifact: nothing here is needed to RUN
# a program, only to build one against the bindings. devel is also what
# rocm-cmake would have chosen: rocm_install_targets and rocm_install default to
# it. Without it these files land in the runtime component instead, so a user
# who installs only the runtime package gets an archive and a set of .mod files
# they cannot use, while the one who installs devel to build against hipSPARSE
# does not get them at all.
#
# Plain install(), not rocm_install(): this directory must also configure
# standalone, where rocm-cmake was never loaded and those commands do not exist.
install(TARGETS hipsparse_fortran
        EXPORT hipsparse-fortran-targets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}/fortran/${HIPSPARSE_FORTRAN_COMPILER_DIR}"
                COMPONENT devel)
install(DIRECTORY "${_moddir}/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/fortran/${HIPSPARSE_FORTRAN_COMPILER_DIR}"
        COMPONENT devel
        FILES_MATCHING PATTERN "*.mod")

# The generated source, so that a user on a compiler ROCm ships no .mod for can
# rebuild the binding without fetching a monorepo to get one file. Source
# obtained separately can also be generated from a different ROCm than the
# installed libhipsparse, which surfaces as an undefined symbol at link time or as
# an interface that silently differs from the one the caller meant; source that
# ships with the library cannot drift from it.
#
# Under share/, not include/. The .mod is compiler-specific and this is not, and
# every entry under include/fortran/ is partitioned by compiler, so a source
# file there would be read as naming one: the config below enumerates that
# directory to report which compilers are installed, and would list hipsparse.F90
# among them.
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/hipsparse.F90"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/hipsparse/fortran"
        COMPONENT devel)
install(EXPORT hipsparse-fortran-targets
        FILE hipsparse-fortran-targets.cmake
        NAMESPACE roc::
        DESTINATION "${_fortran_pkgdir}"
        COMPONENT devel)

# The two directories the config package has to name, kept out of the template
# so that neither side spells lib/ or include/ literally. A literal lib/fortran
# is wrong the moment CMAKE_INSTALL_LIBDIR is lib64, and the config then hands
# the consumer an IMPORTED_LOCATION that does not exist.
set(HIPSPARSE_FORTRAN_LIBDIR     "${CMAKE_INSTALL_LIBDIR}/fortran")
set(HIPSPARSE_FORTRAN_INCLUDEDIR "${CMAKE_INSTALL_INCLUDEDIR}/fortran")

configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/hipsparse-fortran-config.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/hipsparse-fortran-config.cmake"
  INSTALL_DESTINATION "${_fortran_pkgdir}"
  # PATH_VARS is exactly what these two want: @PACKAGE_INIT@ rewrites each into
  # an absolute path computed from where the config file was FOUND, so the
  # package survives being relocated and gets lib64 right without the template
  # knowing which layout it was built for. HIPSPARSE_FORTRAN_COMPILER_DIR stays a
  # plain @-substitution on purpose -- it is a bare directory NAME, not a path,
  # and PATH_VARS would resolve it as one, under the install prefix.
  PATH_VARS HIPSPARSE_FORTRAN_LIBDIR HIPSPARSE_FORTRAN_INCLUDEDIR)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/hipsparse-fortran-config.cmake"
        DESTINATION "${_fortran_pkgdir}"
        COMPONENT devel)

# The config lands in lib/cmake/hipsparse, BESIDE the C package, because that is
# what would let hipsparse-config.cmake pick it up with a bare
# include(hipsparse-fortran-config.cmake OPTIONAL) once rocm_export_targets grows
# a hook for it: a Fortran binding installed after the C library would then be
# found at find_package(hipsparse) time, and ignored when absent.
#
# rocm_export_targets' existing INCLUDE argument is NOT that hook. It emits a
# bare, non-OPTIONAL include() and emits it BEFORE the targets file
# (ROCMInstallTargets.cmake writes INCLUDE entries first), so a Fortran config
# reached that way would run before roc::hipsparse exists. That change belongs in
# rocm-cmake.
#
# But find_package(hipsparse-fortran): the documented spelling, and the one the
# config's own header shows, searches lib/cmake/hipsparse-fortran*, which
# lib/cmake/hipsparse does not match, so on its own that placement makes the
# direct spelling fail with "could not find a package configuration file". A
# forwarder gives the search somewhere to land without duplicating the real
# config, which would also need its own copy of the targets file and would then
# be a second thing to keep in step.
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

# A version file, so that find_package(hipsparse-fortran 5.1) resolves rather than
# failing with "could not find a configuration file compatible with requested
# version". SameMajorVersion because the module's bind(C) surface tracks the C
# API's compatibility promise.
#
# The version is read from THIS tree's top-level CMakeLists, not from
# hipsparse_VERSION. That variable looks like the obvious source and is a trap: in
# a standalone build it is set by the find_package(hipsparse) above, so it reports
# whatever hipSPARSE happens to be INSTALLED (4.5.0 from a stock /opt/rocm) rather
# than the version of the headers these bindings were generated from. The
# bindings ship with their source tree, so the source tree is the truth.
set(_fortran_version "")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../CMakeLists.txt")
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../CMakeLists.txt" _hipsparse_root)
  # hipSPARSE carries its version on the project() call itself,
  # project(hipsparse VERSION 4.8.0 ...), rather than in a VERSION_STRING
  # variable, so match that form.
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
  # Installed in BOTH package directories, and it has to be. CMake looks for
  # <pkg>-config-version.cmake NEXT TO the <pkg>-config.cmake it actually
  # resolved, not wherever the real one lives. find_package(hipsparse-fortran 5.1)
  # resolves the forwarder in lib/cmake/hipsparse-fortran, so a version file only
  # beside the real config in lib/cmake/hipsparse is never consulted, and the call
  # fails with "could not find a configuration file compatible with requested
  # version" even though the config is right there and the version matches.
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/hipsparse-fortran-config-version.cmake"
          DESTINATION "${_fortran_pkgdir}"
          COMPONENT devel)
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/hipsparse-fortran-config-version.cmake"
          DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/hipsparse-fortran"
          COMPONENT devel)
else()
  # Better no version file than a wrong one: without it an unversioned
  # find_package still succeeds, whereas a version file claiming the wrong
  # number makes a versioned one succeed and link the wrong .mod.
  message(STATUS
    "hipSPARSE: could not determine the version for the Fortran package, so no "
    "hipsparse-fortran-config-version.cmake is installed; "
    "find_package(hipsparse-fortran <version>) will not match.")
endif()
