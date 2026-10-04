!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
! Copyright (C) 2026 Advanced Micro Devices, Inc. All rights reserved.
!
! SPDX-License-Identifier: MIT
!
! Permission is hereby granted, free of charge, to any person obtaining a copy
! of this software and associated documentation files (the "Software"), to deal
! in the Software without restriction, including without limitation the rights
! to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
! copies of the Software, and to permit persons to whom the Software is
! furnished to do so, subject to the following conditions:
!
! The above copyright notice and this permission notice shall be included in
! all copies or substantial portions of the Software.
!
! THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
! IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
! FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
! AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
! LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
! OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
! THE SOFTWARE.
!
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

! TEMPORARY. Delete this file when ROCm/rocm-systems#11923 lands and ROCm ships a
! HIP Fortran binding. The replacement is one CMake change in
! clients/CMakeLists.txt: drop this source, find_package(hip-fortran) and link
! hip::hip_fortran instead. The `use hip` lines in the client shims do NOT
! change. They simply start resolving to the real module, which declares these
! same enumerators with these same values.
!
! Why it exists. The hand-written hipblas_module.f90 that this series removes
! defined two modules, hipblas and hipblas_enums, and hipblas_enums re-exported
! HIP's hipDataType enumerators. The generated hipblas module does not: a
! hipDataType belongs to HIP, not to hipBLAS, and the generated interfaces type
! those arguments as plain integer(c_int). That is the right call, but it leaves
! the client shims in clients/include/hipblas_fortran_blas{,_64}.f90, which
! declare their datatype arguments as integer(kind(HIP_R_16F)), with no source
! for the name. 57 scopes across those two files are affected.
!
! Build-only, and deliberately so. This module is compiled into
! hipblas_fortran_client and is NOT installed, NOT exported and NOT packaged, so
! no second hip.mod can ever appear in an installed prefix and collide with the
! real HIP binding. The only place the two could coexist is inside this build,
! where the include order is ours to control.
!
! Scope. HIP's sixteen hipDataType enumerators of the contiguous block 0..15, of
! which the hipBLAS clients use two: HIP_R_16F in the shims and HIP_R_32F in the
! gemm_ex sample. Values are copied verbatim from the HIP binding, so the swap
! above is observably a no-op.
! Deliberately NOT the whole hipDataType enumeration: a partial stub that is
! obviously a stub is easier to delete than a plausible copy of HIP's.

module hip
  implicit none
  public

  enum, bind(c)
    enumerator :: HIP_R_32F = 0
    enumerator :: HIP_R_64F = 1
    enumerator :: HIP_R_16F = 2
    enumerator :: HIP_R_8I = 3
    enumerator :: HIP_C_32F = 4
    enumerator :: HIP_C_64F = 5
    enumerator :: HIP_C_16F = 6
    enumerator :: HIP_C_8I = 7
    enumerator :: HIP_R_8U = 8
    enumerator :: HIP_C_8U = 9
    enumerator :: HIP_R_32I = 10
    enumerator :: HIP_C_32I = 11
    enumerator :: HIP_R_32U = 12
    enumerator :: HIP_C_32U = 13
    enumerator :: HIP_R_16BF = 14
    enumerator :: HIP_C_16BF = 15
  end enum

end module hip
