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

! Demonstrates the hipFFT library version queries hipfftGetVersion and
! hipfftGetProperty.
!
! hipfftGetVersion takes its output argument as a plain Fortran scalar
! (integer(c_int) :: version), so the variable is passed DIRECTLY, not via
! c_loc. hipfftGetProperty is reached through the generic interface, which
! accepts a target integer(c_int) scalar and applies c_loc internally.
!
! Both calls report the version of the backend FFT library (rocFFT, or cuFFT),
! not of hipFFT itself, and hipfftGetProperty is derived from hipfftGetVersion,
! so this checks the binding rather than the library: every output is poisoned
! with -12345 beforehand, so a binding that never writes through is caught, and
! the packed hipfftGetVersion code must equal major*10000 + minor*100 + patch
! from hipfftGetProperty, so a binding that passes the property selector or the
! output argument wrongly is caught. Any mismatch prints "FAILED! ..." and STOP 1.
program hipfft_version
  use iso_c_binding
  use hip
  use hipfft

  implicit none

  integer(c_int), parameter :: poison = -12345

  integer(c_int), target :: version, major, minor, patch

  write(*,"(a)",advance="no") &
    "-- Running test 'hipFFT version query' - "

  version = poison
  major   = poison
  minor   = poison
  patch   = poison

  ! Output scalar passed directly: the dummy is "integer(c_int) :: version".
  call hipfftCheck(hipfftGetVersion(version))

  call hipfftCheck(hipfftGetProperty(HIPFFT_MAJOR_VERSION, major))
  call hipfftCheck(hipfftGetProperty(HIPFFT_MINOR_VERSION, minor))
  call hipfftCheck(hipfftGetProperty(HIPFFT_PATCH_LEVEL, patch))

  if (version == poison) then
     write(*,*) "FAILED! hipfftGetVersion did not write its output argument"
     STOP 1
  end if

  if (major == poison .or. minor == poison .or. patch == poison) then
     write(*,*) "FAILED! hipfftGetProperty did not write its output argument"
     STOP 1
  end if

  if (version <= 0) then
     write(*,*) "FAILED! implausible backend version code: ", version
     STOP 1
  end if

  if (major < 1 .or. minor < 0 .or. patch < 0) then
     write(*,*) "FAILED! implausible backend version triple: ", major, minor, patch
     STOP 1
  end if

  if (version /= major * 10000 + minor * 100 + patch) then
     write(*,*) "FAILED! hipfftGetVersion code ", version, &
                " disagrees with hipfftGetProperty triple ", major, minor, patch
     STOP 1
  end if

  write(*,"(a,i0,a,i0,a,i0,a,i0,a)") "PASSED! backend FFT library version: ", &
    major, ".", minor, ".", patch, " (code ", version, ")"

end program hipfft_version
