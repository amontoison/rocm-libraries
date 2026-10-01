# Fortran bindings for rocFFT.
#
# rocfft.F90 is GENERATED from the rocFFT C headers by the rocm-fortran generator
# and committed here, next to the library it binds, so that a header change and
# its binding travel in the same pull request. Do not edit it by hand: regenerate
# it instead.
#
# The bindings are a single self-contained Fortran module, `rocfft`, carrying the
# bind(C) interfaces, the enum constants and the rocfftCheck status helper. It
# declares no derived type of its own -- rocfft.F90 contains no `type, bind(c)`
# at all -- so the C handle types rocfft_plan, rocfft_plan_description,
# rocfft_execution_info, rocfft_field and rocfft_brick each cross the boundary as
# a plain type(c_ptr), which is what the C headers make them. A consumer writes
# one `use rocfft`.
#
# That is the whole of the packaged track. The generator can also emit
# derived-type wrappers, _typed strongly-typed overloads and _dptr device-pointer
# specifics; none of them are part of what is packaged here, and rocfft.F90 is
# generated without docstrings. rocFFT's binding never carried any of the three
# -- no revision of rocfft.F90 in this series matches _typed, _dptr or
# `type, bind(c)` -- so the packaged track drops nothing rocFFT had.
#
# This directory is self-contained: it builds with no dependency on the rocFFT C
# target, because a Fortran binding compiles against nothing (it only declares
# bind(C) interfaces). librocfft is required to LINK a final executable, and is
# carried as the target's link interface for that reason.

cmake_minimum_required(VERSION 3.19)

# Configured directly (not as a subdirectory of the rocFFT build), this directory
# is its own project, so that the bindings can be built and packaged on their
# own. In-tree, rocFFT's own project() is already in effect.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(rocfft-fortran LANGUAGES NONE)
endif()

# ON by default but GUARDED: a site with no Fortran compiler must still configure
# and build the C library. The monorepo root already probes once and publishes
# ROCM_HAVE_FORTRAN; a standalone rocFFT build falls back to its own probe.
#
# "${UNIX}" rather than ON, because Windows is not such a site: ROCm ships no
# Fortran compiler there at all. TheRock puts flang in LLVM_ENABLE_PROJECTS
# only in the non-WIN32 branch of compiler/pre_hook_amd-llvm.cmake, so the
# amdflang preference below finds nothing, the probe falls through to whatever
# gfortran happens to be on PATH, and enable_language(Fortran) then fails to
# identify it. A default and not a veto: -DBUILD_FORTRAN_BINDINGS=ON still
# works wherever a usable compiler exists.
option(BUILD_FORTRAN_BINDINGS "Build the rocFFT Fortran bindings" "${UNIX}")

# The bindings' own test suite, and the Fortran clients where a library has
# them: one switch rather than two neighbouring ones. ON by default, and
# guarded the same way as the bindings, so asking for them when the bindings
# were not built is a skip rather than an error.
option(BUILD_FORTRAN_CLIENTS "Build the rocFFT Fortran clients and binding tests" ON)

# BUILD_FORTRAN_BINDINGS and BUILD_FORTRAN_CLIENTS are ONE cache entry shared by
# every library in the monorepo, which is what you want for the common case:
# -DBUILD_FORTRAN_BINDINGS=OFF from the top turns the whole Fortran track off in
# a single flag. It is not what you want when one library has to be singled out,
# so each library also honours a name of its own, which overrides the shared
# default here.
#
# That override is the last word for the BINDINGS only. In-tree the binding
# tests are GPU test executables and belong to rocFFT's client/test track, so the
# library's own client switch is a floor they cannot rise above: see the gate
# further down, which turns BUILD_FORTRAN_CLIENTS off in a library-only build
# whatever ROCFFT_BUILD_FORTRAN_CLIENTS says.
if(DEFINED ROCFFT_BUILD_FORTRAN_BINDINGS)
  set(BUILD_FORTRAN_BINDINGS ${ROCFFT_BUILD_FORTRAN_BINDINGS})
endif()
if(DEFINED ROCFFT_BUILD_FORTRAN_CLIENTS)
  set(BUILD_FORTRAN_CLIENTS ${ROCFFT_BUILD_FORTRAN_CLIENTS})
endif()

# Which array-argument overloads the module exposes. One tri-state stands in for
# the two macros the generated source is guarded with (USE_ASSUMED_SHAPE /
# USE_ASSUMED_RANK), which are mutually exclusive: an assumed-rank dummy is not
# distinguishable by rank from the per-rank specifics, so assumed-rank REPLACES
# the per-rank variants rather than adding to them.
#
#   none           the raw type(c_ptr) surface only (Fortran 2003)
#   assumed-shape  + the per-rank array overloads (Fortran 2008), the default,
#                  and what the bindings shipped in ROCm are built with
#   assumed-rank   + one dimension(..) overload in their place (Fortran 2018)
#
# Array overloads are generated for the subset of routines the generator
# supports; every other routine exposes the plain type(c_ptr) interface under
# every tier. For rocFFT that subset is rocfft_plan_create and
# rocfft_plan_description_set_data_layout: they are the only named generic
# interfaces in rocfft.F90, the only place the USE_ASSUMED_* guards appear, and
# the upper two tiers add specifics to them alone. The tier is still worth
# choosing, because the arguments those specifics take as Fortran arrays are the
# integer(c_size_t) length, offset and stride vectors a caller builds on the
# host.
set(FORTRAN_ARRAY_INTERFACES "assumed-shape" CACHE STRING
    "Array-argument overloads to expose: none, assumed-shape, or assumed-rank")
set_property(CACHE FORTRAN_ARRAY_INTERFACES PROPERTY STRINGS
             none assumed-shape assumed-rank)

# Report to the parent scope whether the bindings were really built. Distinct
# from ROCM_HAVE_FORTRAN, which reports only that a compiler exists: the
# compiler can be present while this directory is switched off.
# Only when there IS a parent: standalone, this directory is the top-level scope
# and PARENT_SCOPE warns under -Wdev rather than being a silent no-op.
if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(ROCFFT_HAVE_FORTRAN_BINDINGS FALSE PARENT_SCOPE)
endif()

if(NOT BUILD_FORTRAN_BINDINGS)
  return()
endif()

# Probing for a Fortran compiler is not free, and every library in a monorepo
# build would otherwise repeat it, so the answer is computed ONCE and published
# as ROCM_HAVE_FORTRAN. That name is deliberately neither rocm-libraries' nor
# rocm-systems': a library from one monorepo is regularly built inside the other,
# and a repo-specific name would make it read the wrong variable, or none. Here
# it is this repo's root that publishes it on the way in; rocm-systems, which has
# no root CMakeLists, publishes the same name from shared/cmake/ROCmFortran.cmake.
#
# ROCM_LIBS_HAVE_FORTRAN is what this repo's root publishes TODAY
# (CMakeLists.txt:11), so it is honoured as a legacy spelling,
# second and never over the new name, until the root is switched over. It is a bridge, not an
# alias: once the root sets ROCM_HAVE_FORTRAN this branch is dead and should go.
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
  message(STATUS "rocFFT: no Fortran compiler found, skipping the Fortran bindings")
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

add_library(rocfft_fortran STATIC rocfft.F90)
# roc:: is the namespace rocFFT already exports roc::rocfft under, so the
# binding is reachable the same way the C library is.
add_library(roc::rocfft_fortran ALIAS rocfft_fortran)

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
#
# This mapping MUST mirror the one in rocfft-fortran-config.cmake.in exactly.
# That file recomputes the same name for the CONSUMER's compiler and compares
# the two, and the two cannot share code, because the config runs in a consumer's
# build, long after this one is gone. Let them drift and the config rejects
# every consumer with a compiler mismatch that does not exist.
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
# ROCFFT_FORTRAN_COMPILER_DIR itself, for example to gfortran-13.3.0.
set(ROCFFT_FORTRAN_COMPILER_DIR "${_fc_name}" CACHE STRING
    "Per-compiler subdirectory the .mod and the archive install under")

set(_moddir "${CMAKE_CURRENT_BINARY_DIR}/include")
set_target_properties(rocfft_fortran PROPERTIES
  Fortran_MODULE_DIRECTORY "${_moddir}"
  # rocfft.F90 is uppercase: the array overloads sit behind preprocessor guards,
  # so the source must go through the C preprocessor.
  Fortran_PREPROCESS ON
  # Free form is the .F90 default, but stating it lets CMake emit whichever flag
  # the compiler in use expects rather than relying on the extension.
  Fortran_FORMAT FREE
  POSITION_INDEPENDENT_CODE ON)

# Translate the tri-state into the macro the generated source is guarded with.
# The option value and the macro name are the same word, and the two upper tiers
# are mutually exclusive: an assumed-rank dummy is not distinguishable by rank
# from the per-rank specifics, so the two cannot legally coexist in one generic
# and each tier selects itself. `none` defines neither and leaves the raw
# type(c_ptr) surface.
#
# The branch below therefore defines exactly one macro and never both. Both at
# once is nevertheless not an error: the two guarded regions of rocfft.F90 -- the
# module-procedure list inside each generic, and the bodies in the contains
# section -- agree on precedence and test USE_ASSUMED_RANK first, so assumed-rank
# wins and the assumed-shape specifics are simply not emitted. amdflang -cpp
# -DUSE_ASSUMED_SHAPE=1 -DUSE_ASSUMED_RANK=1 -fsyntax-only rocfft.F90 succeeds,
# and -E under both macros is byte-identical to -E under USE_ASSUMED_RANK alone.
# The tri-state still never asks for both; that is a well-defined state rather
# than a supported one.
if(FORTRAN_ARRAY_INTERFACES STREQUAL "assumed-shape")
  target_compile_definitions(rocfft_fortran PRIVATE USE_ASSUMED_SHAPE=1)
elseif(FORTRAN_ARRAY_INTERFACES STREQUAL "assumed-rank")
  target_compile_definitions(rocfft_fortran PRIVATE USE_ASSUMED_RANK=1)
endif()

# No line-length flag: the generator wraps the emitted source at 112 columns,
# inside the 132 that free-form Fortran guarantees, so every compiler reads it
# without a vendor-specific option.

target_include_directories(rocfft_fortran PUBLIC
  "$<BUILD_INTERFACE:${_moddir}>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/fortran/${ROCFFT_FORTRAN_COMPILER_DIR}>")

# Link interface only: needed to link an executable, not to build this archive.
# In-tree roc::rocfft comes from rocFFT's own library/src. Standalone it has to
# be looked for, and testing for the target without ever calling find_package
# would mean it is never found: the archive would then export no link interface
# at all, and every consumer would hit undefined references to rocfft_setup and
# friends despite linking roc::rocfft_fortran exactly as documented.
#
# rocfft-config.cmake does find_dependency(hip), and hip-config.cmake reaches
# FindThreads, which hard-errors unless C or CXX is enabled. This directory has
# only Fortran, so C is enabled first; it compiles none of the sources here.
# QUIET, because a site with no installed ROCm can still legitimately build the
# bindings: they declare bind(C) interfaces and compile against nothing.
if(NOT TARGET roc::rocfft)
  if(NOT CMAKE_C_COMPILER_LOADED AND NOT CMAKE_CXX_COMPILER_LOADED)
    enable_language(C)
  endif()
  find_package(rocfft QUIET)
endif()

# Whether the config package installed below must make its consumer find the C
# package. It must exactly when the archive really carries roc::rocfft in its
# link interface, and must not when it does not: a binding built with no rocFFT
# in sight exports nothing to link, so demanding find_dependency(rocfft) from the
# consumer would impose a dependency this package does not have, on precisely the
# machines that do not have it. Written as the literal TRUE or FALSE, never left
# empty, because the template substitutes it straight into an if() condition.
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

# The binding tests below are GPU runtime executables, so in-tree they belong to
# the client/test track and follow the library's own switch: a library-only build
# has no business producing test binaries, and would be made to link a GPU
# runtime it never asked for. The condition is the one rocFFT guards
# add_subdirectory( clients ) with, so the two cannot disagree; the root resolves
# those three switches before it reaches this directory precisely so that this
# test reads the same values the clients guard will. It is a floor, not a
# preference: nothing a user can pass turns the tests back on in a library-only
# build.
#
# Placed after the early returns above rather than beside the option(), so that a
# configure which never builds anything Fortran -- BUILD_FORTRAN_BINDINGS=OFF, or
# a host with no Fortran compiler -- does not announce a decision about tests it
# was never going to reach.
#
# Standalone, this directory IS the build and the tests are the point, so the
# library's switch does not exist and BUILD_FORTRAN_CLIENTS stands on its own.
# Setting it as a plain variable is deliberate: the cache entry is one switch
# shared by every library in the monorepo, so turning it off here has to be
# scoped to this directory and must not be written back.
if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR
   AND BUILD_FORTRAN_CLIENTS
   AND NOT (BUILD_CLIENTS_SAMPLES OR BUILD_CLIENTS_TESTS OR BUILD_CLIENTS_BENCH))
  message(STATUS
    "rocFFT: clients are not being built, skipping the Fortran binding tests.")
  set(BUILD_FORTRAN_CLIENTS OFF)
endif()

if(BUILD_FORTRAN_CLIENTS)
  # Registers this directory's tests. ctest walks the chain of
  # CTestTestfile.cmake down from a build root, and CMake writes that chain only
  # for directories where testing was enabled, starting at the top, so in-tree
  # the root has to enable it too -- which it does, on the same three client
  # switches the gate above reads.
  enable_testing()
  add_subdirectory(test)
endif()

include(CMakePackageConfigHelpers)

# COMPONENT devel on every rule below, because everything this directory
# installs -- a static archive, a set of .mod files, the generated source and
# the CMake package -- is a development artifact: nothing here is needed to RUN
# a program, only to build one against the bindings. devel is also what
# rocm-cmake would have chosen: rocm_install_targets and rocm_install default to
# it. Without it these files land in the runtime component instead.
#
# Plain install(), not rocm_install(): this directory must also configure
# standalone, where rocm-cmake was never loaded and those commands do not exist.
install(TARGETS rocfft_fortran
        EXPORT rocfft-fortran-targets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}/fortran/${ROCFFT_FORTRAN_COMPILER_DIR}"
                COMPONENT devel)
install(DIRECTORY "${_moddir}/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/fortran/${ROCFFT_FORTRAN_COMPILER_DIR}"
        COMPONENT devel
        FILES_MATCHING PATTERN "*.mod")

# The generated source, so that a user on a compiler ROCm ships no .mod for can
# rebuild the binding without fetching a monorepo to get one file. Source
# obtained separately can also be generated from a different ROCm than the
# installed librocfft, which surfaces as an undefined symbol at link time or as
# an interface that silently differs from the one the caller meant; source that
# ships with the library cannot drift from it.
#
# Under share/, not include/. The .mod is compiler-specific and this is not, and
# every entry under include/fortran/ is partitioned by compiler, so a source file
# there would be read as naming one: the config below enumerates that directory
# to report which compilers are installed, and would list rocfft.F90 among them.
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/rocfft.F90"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/rocfft/fortran"
        COMPONENT devel)
install(EXPORT rocfft-fortran-targets
        FILE rocfft-fortran-targets.cmake
        NAMESPACE roc::
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/rocfft"
        COMPONENT devel)

# The two directories the config package has to name, kept out of the template
# so that neither side spells lib/ or include/ literally. A literal lib/fortran
# is wrong the moment CMAKE_INSTALL_LIBDIR is lib64, and the config then hands
# the consumer an IMPORTED_LOCATION that does not exist.
set(ROCFFT_FORTRAN_LIBDIR     "${CMAKE_INSTALL_LIBDIR}/fortran")
set(ROCFFT_FORTRAN_INCLUDEDIR "${CMAKE_INSTALL_INCLUDEDIR}/fortran")

# lib/cmake/rocfft is where rocFFT's own rocm_export_targets() puts
# rocfft-config.cmake, so the Fortran package lands beside the C one and a single
# CMAKE_PREFIX_PATH entry finds both.
configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/rocfft-fortran-config.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/rocfft-fortran-config.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/rocfft"
  # PATH_VARS is exactly what these two want: @PACKAGE_INIT@ rewrites each into
  # an absolute path computed from where the config file was FOUND, so the
  # package survives being relocated and gets lib64 right without the template
  # knowing which layout it was built for. ROCFFT_FORTRAN_COMPILER_DIR stays a
  # plain @-substitution on purpose -- it is a bare directory NAME, not a path,
  # and PATH_VARS would resolve it as one, under the install prefix.
  PATH_VARS ROCFFT_FORTRAN_LIBDIR ROCFFT_FORTRAN_INCLUDEDIR)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/rocfft-fortran-config.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/rocfft"
        COMPONENT devel)

# That placement is what lets rocfft-config.cmake pick the bindings up with a
# bare include(rocfft-fortran-config.cmake OPTIONAL): a Fortran binding installed
# after the C library is then found at find_package(rocfft) time, and ignored
# when absent.
#
# But find_package(rocfft-fortran), the other documented spelling, searches
# lib/cmake/rocfft-fortran*, which lib/cmake/rocfft does not match, so on its own
# that placement makes the direct spelling fail with "could not find a package
# configuration file". A forwarder gives the search somewhere to land without
# duplicating the real config, which would also need its own copy of the targets
# file and would then be a second thing to keep in step.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/forward/rocfft-fortran-config.cmake"
"# Generated forwarder. The real config lives beside the rocFFT C package, in
# lib/cmake/rocfft, so that rocfft-config.cmake can include it as a sibling. This
# file only exists so that find_package(rocfft-fortran) resolves to the same
# place.
include(\"\${CMAKE_CURRENT_LIST_DIR}/../rocfft/rocfft-fortran-config.cmake\")
")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/forward/rocfft-fortran-config.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/rocfft-fortran"
        COMPONENT devel)

# A version file, so that find_package(rocfft-fortran 1.0) resolves rather than
# failing with "could not find a configuration file compatible with requested
# version". SameMajorVersion because the module's bind(C) surface tracks the C
# API's compatibility promise.
#
# The version is read from THIS tree's top-level CMakeLists, not from
# rocfft_VERSION. That variable looks like the obvious source and is a trap: in a
# standalone build it is set by the find_package(rocfft) above, so it reports
# whatever rocFFT happens to be INSTALLED rather than the version of the headers
# these bindings were generated from. The bindings ship with their source tree,
# so the source tree is the truth.
set(_fortran_version "")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../CMakeLists.txt")
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../CMakeLists.txt" _rocfft_root)
  # rocFFT does NOT spell the version as a literal anywhere: it sets the three
  # components and composes VERSION_STRING from them, so a regex looking for
  # "x.y.z" matches nothing. Read the components and recompose. Note the spaces
  # inside the parentheses: rocFFT writes set ( ROCFFT_VERSION_MAJOR "1" ).
  #
  # Success is judged on the whole match being non-empty, NOT on the captured
  # group being truthy: the minor component is the string "0", one of CMake's
  # false constants, so if(NOT CMAKE_MATCH_1) treats a perfectly good match as a
  # failure and no version file is installed at all.
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
  # Installed in BOTH package directories, and it has to be. CMake looks for
  # <pkg>-config-version.cmake NEXT TO the <pkg>-config.cmake it actually
  # resolved, not wherever the real one lives. find_package(rocfft-fortran 1.0)
  # resolves the forwarder in lib/cmake/rocfft-fortran, so a version file only
  # beside the real config in lib/cmake/rocfft is never consulted, and the call
  # fails with "could not find a configuration file compatible with requested
  # version" even though the config is right there and the version matches.
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/rocfft-fortran-config-version.cmake"
          DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/rocfft"
          COMPONENT devel)
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/rocfft-fortran-config-version.cmake"
          DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/rocfft-fortran"
          COMPONENT devel)
else()
  # Better no version file than a wrong one: without it an unversioned
  # find_package still succeeds, whereas a version file claiming the wrong number
  # makes a versioned one succeed and link the wrong .mod.
  message(STATUS
    "rocFFT: could not determine the version for the Fortran package, so no "
    "rocfft-fortran-config-version.cmake is installed; "
    "find_package(rocfft-fortran <version>) will not match.")
endif()
