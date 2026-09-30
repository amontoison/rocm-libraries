# ########################################################################
# Copyright (C) 2026 Advanced Micro Devices, Inc. All rights reserved.
#
# SPDX-License-Identifier: MIT
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell cop-
# ies of the Software, and to permit persons to whom the Software is furnished
# to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IM-
# PLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
# FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
# COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
# IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNE-
# CTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
#
# ########################################################################

# Cray toolchain. ftn and cc are the Cray compiler wrappers, which select the underlying compiler from the loaded PrgEnv module.
#
# Usage, from projects/rocblas/fortran:
#   cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/cray.cmake
#
# The compilers are looked up on PATH. Only the Fortran one actually matters
# here: the bindings are pure Fortran, and the C entry points they bind to come
# from librocblas at link time.
#
# Free form and C preprocessing are requested by the CMakeLists via the
# Fortran_FORMAT and Fortran_PREPROCESS target properties, so CMake emits
# whichever flag this compiler expects and none is hardcoded here. No
# line-length flag is needed either: the generated source wraps at 112 columns,
# inside the 132 the free-form standard guarantees.

set(CMAKE_Fortran_COMPILER ftn CACHE FILEPATH "Fortran compiler")
set(CMAKE_C_COMPILER       cc CACHE FILEPATH "C compiler")
