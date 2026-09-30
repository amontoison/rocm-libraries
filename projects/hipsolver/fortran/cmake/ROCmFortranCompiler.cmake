# Copyright Advanced Micro Devices, Inc., or its affiliates.
# SPDX-License-Identifier: MIT

# Chooses the Fortran compiler the ROCm Fortran bindings are built with.
#
# This is a module rather than an inline block because the choice has to be
# expressed from two places. fortran/CMakeLists.txt needs it before its own
# check_language(Fortran), which is the standalone path; hipSOLVER's root
# CMakeLists needs it above project(), before it falls back to a bare gfortran
# and long before the enable_language(Fortran) that freezes the answer for the
# whole tree. Two copies of a compiler-selection policy drift apart, and the
# drift is silent: the build succeeds either way, and the only visible
# difference is which per-compiler directory the artifacts end up in.
#
# Including this file twice is harmless: the macro is idempotent, and so is
# redefining it.

# Prefer ROCm's own amdflang over whatever CMake would otherwise find first.
# check_language(Fortran) and enable_language(Fortran) both take the first
# compiler on the search path, which on a ROCm CI image is the system gfortran,
# and that is how the bindings ended up as the one component of ROCm built by a
# different toolchain from everything else: installed under fortran/gfortran/
# while the documentation promises fortran/amdflang/, and exercising a compiler
# the product does not ship with.
#
# amdflang sits beside the clang already compiling this project, so the answer
# is next to CMAKE_CXX_COMPILER when there is one; ROCM_PATH, as a variable or
# in the environment, covers the configures where there is not. Searched with
# NO_DEFAULT_PATH so a stray amdflang elsewhere on PATH cannot win over the ROCm
# being built against.
#
# An explicit CMAKE_Fortran_COMPILER, or FC in the environment, always wins:
# building these bindings with gfortran is a supported thing to do, and a .mod
# is compiler-specific, so whoever asked for a compiler meant it. That test is
# also what makes the macro idempotent, and what makes a second call after the
# language is enabled do nothing at all.
macro(rocm_fortran_prefer_amdflang)
  if(NOT CMAKE_Fortran_COMPILER AND NOT DEFINED ENV{FC})
    # rocm-cmake's ROCMChecks arms a variable_watch on the CACHE entry
    # CMAKE_Fortran_COMPILER, to catch a project setting a toolchain variable
    # behind the toolchain file's back. Writing that entry is exactly what this
    # macro does, and the watch cannot tell a deliberate compiler choice apart
    # from a violation, so it reports this one: a stray twelve-line banner by
    # default, and a hard configure failure under -DROCM_ERROR_TOOLCHAIN_VAR=ON
    # (or ROCMCHECKS_ERROR_TOOLCHAIN_VAR in the environment), which is a switch
    # CI turns on.
    #
    # So silence the checker across the write and put it back afterwards, the
    # same save/set/restore rocRAND already uses in cmake/Dependencies.cmake.
    # Plain variables rather than cache ones: they shadow the cache for this
    # scope only, which is what the watch callback reads, and the user's cache
    # entries are left exactly as they were. Both names have to be cleared,
    # because the callback escalates to SEND_ERROR on ROCM_ERROR_TOOLCHAIN_VAR
    # before it ever looks at the warn flag.
    set(_rocm_fc_warn_toolchain_var "${ROCM_WARN_TOOLCHAIN_VAR}")
    set(_rocm_fc_error_toolchain_var "${ROCM_ERROR_TOOLCHAIN_VAR}")
    set(ROCM_WARN_TOOLCHAIN_VAR OFF)
    set(ROCM_ERROR_TOOLCHAIN_VAR OFF)

    set(_rocm_fc_hints)
    foreach(_rocm_fc_cc "${CMAKE_CXX_COMPILER}" "${CMAKE_C_COMPILER}"
                        "${CMAKE_HIP_COMPILER}")
      if(_rocm_fc_cc)
        get_filename_component(_rocm_fc_dir "${_rocm_fc_cc}" DIRECTORY)
        list(APPEND _rocm_fc_hints "${_rocm_fc_dir}")
      endif()
    endforeach()
    # The environment as well as the CMake variable, because the two are not
    # the same thing and the environment is the one that is usually set:
    # ROCM_PATH becomes a CMake variable only once something in the tree has
    # defined it, and hipSOLVER's root calls this macro before that point; a
    # standalone configure of fortran/ never defines it at all. Left out, the
    # hint list holds compiler directories alone, and is empty outright on a
    # configure that has chosen no C or C++ compiler yet -- NO_DEFAULT_PATH then
    # guarantees the search finds nothing, in exactly the case the hints are
    # there for.
    foreach(_rocm_fc_root "${ROCM_PATH}" "$ENV{ROCM_PATH}")
      if(_rocm_fc_root)
        list(APPEND _rocm_fc_hints "${_rocm_fc_root}/lib/llvm/bin"
                                   "${_rocm_fc_root}/bin")
      endif()
    endforeach()
    if(_rocm_fc_hints)
      find_program(_rocm_amdflang NAMES amdflang HINTS ${_rocm_fc_hints}
                   NO_DEFAULT_PATH)
      # Confirm it runs before committing to it. A ROCm tree can carry a
      # dangling amdflang symlink, and neither check_language() nor
      # enable_language() would point back here: both simply take the variable
      # once it is set, so the failure would surface far later with no hint of
      # where the choice was made.
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

    set(ROCM_WARN_TOOLCHAIN_VAR "${_rocm_fc_warn_toolchain_var}")
    set(ROCM_ERROR_TOOLCHAIN_VAR "${_rocm_fc_error_toolchain_var}")
    unset(_rocm_fc_warn_toolchain_var)
    unset(_rocm_fc_error_toolchain_var)
    unset(_rocm_fc_hints)
    unset(_rocm_fc_cc)
    unset(_rocm_fc_dir)
    unset(_rocm_fc_root)
  endif()
endmacro()
