# Changelog for hipRAND

Documentation for hipRAND is available at
[https://rocm.docs.amd.com/projects/hipRAND/en/latest/](https://rocm.docs.amd.com/projects/hipRAND/en/latest/).

## hipRAND 3.4.0 for ROCm 7.14

### Added

* gfx1250 support
* Generated Fortran bindings for hipRAND, as a single self-contained `hiprand` module
  (`use hiprand`, link `hip::hiprand_fortran`). Built by default when a Fortran compiler is
  available; controlled by `BUILD_FORTRAN_BINDINGS`, with `BUILD_FORTRAN_CLIENTS` and the
  tri-state `FORTRAN_ARRAY_INTERFACES`. The package is found with
  `find_package(hiprand-fortran)` and installs per compiler under
  `<CMAKE_INSTALL_LIBDIR>/fortran/<compiler>` and
  `<CMAKE_INSTALL_INCLUDEDIR>/fortran/<compiler>`.
  The module exports one `bind(C)` interface for each of the 30 host entry points
  `hiprand.h` declares, plus the enum constants and the `hiprandCheck` status helper. It
  declares no derived types: the opaque handles `hiprandGenerator_t` and
  `hiprandDiscreteDistribution_t` are passed as `type(c_ptr)`, and the
  derived-type wrappers, the strongly-typed `_typed` overloads and the `_dptr`
  device-pointer specifics are not part of the packaged track. Array overloads are
  generated for the subset of routines the generator supports; every other routine exposes
  the plain `type(c_ptr)` interface under every tier.
  These bindings are new in this release, so no previously shipped Fortran API is affected.
  The binding tests live in `fortran/test/`, beside the binding rather than under `test/`.
  They are 23 programs, one per (generator, distribution) pair, plus the FRUIT suite that
  used to test the hand-written wrapper from `test/fortran/`: it has been moved here and
  ported to `use hiprand` and `use hip`, and it checks the status code of every generator
  lifecycle and seeding call. Every one of the 24 needs a GPU and is labelled `gpu`, so
  `ctest -LE gpu` on a machine without one skips them all.

### Removed

* The deprecated hand-written Fortran wrapper (`library/src/fortran/hiprand_m.f90`), its
  private HIP module (`library/src/fortran/hip/`) and its `BUILD_FORTRAN_WRAPPER` option,
  superseded by the generated bindings above. The wrapper was deprecated in favour of
  hipfort in hipRAND 3.0.0 (ROCm 7.0) and was never shipped enabled, so
  `hiprand_FORTRAN_FOUND` was already `NOTFOUND` and
  `hiprand_FORTRAN_SRC_DIRS` was never set in a released package. Source builds that passed
  `-DBUILD_FORTRAN_WRAPPER=ON` are affected and should move to `use hiprand`. The FRUIT
  suite that tested the wrapper is not removed; see above.

## Since last release ROCm 7.12

### Added

* hiprand.dll now contains embedded file version metadata.

## hipRAND 3.2.0 for ROCm 7.12

### Added

* gfx1150,gfx1152 and gfx1153 support
* Added a new cmake option, `ROCRAND_FETCH_METHOD`, which allows you to specify how you would like to fetch rocRAND.
  * It may be set to one of the following:
    * `PACKAGE` - (default) searches for a preinstalled packaged version of the dependency. If it is not found, the build will fall back using option `DOWNLOAD`, below.
    * `DOWNLOAD` - downloads the dependency from the rocm-libraries repository. If git >= 2.25 is present, this option uses a sparse checkout that avoids downloading more than it needs to. If not, the whole monorepo is downloaded (this may take some time).
    * `MONOREPO` - this options is intended to be used if you are building hipCUB from within a copy of the rocm-libraries repository that you have cloned (and therefore already contains rocRAND). When selected, the build will try find the dependency in the local repository tree. If it cannot be found, the build will attempt to use git to perform a sparse-checkout of rocRAND. If that also fails, it will fall back to using the `DOWNLOAD` option described above.
    
  * The existing `DEPENDENCIES_FORCE_DOWNLOAD` CMake option has been renamed `EXTERNAL_DEPS_FORCE_DOWNLOAD` and no longer affects rocRAND.

## hipRAND 3.1.0 for ROCm 7.1

### Resolved issues

* Updated error handling for several hipRAND unit tests to accomodate the new hipGetLastError behaviour that was introduced in ROCm 7.0.
As of ROCm 7.0, the internal error state is cleared on each call to `hipGetLastError` rather than on every HIP API call.

## hipRAND 3.0.0 for ROCm 7.0

### Added

* gfx950 support

### Changed

* Deprecated hipRAND's Fortran API in favor of hipfort.

### Removed

* Removed C++14 support, only C++17 is supported.

## hipRAND 2.12.0 for ROCm 6.4.0

### Changed

* When building hipRAND on Windows, use `HIP_PATH` (instead of the former `HIP_DIR`) to specify the path to the HIP SDK installation.
  * When building with the `rmake.py` script, if `HIP_PATH` is not set, it will default to `C:\hip`.

### Resolved issues

* Fixed an issue that was causing hipRAND build failures on Windows when the HIP SDK was installed to a location with a path that contains spaces.

## hipRAND-2.11.1 for ROCm 6.2.4

### Added

* GFX1151 Support

## hipRAND 2.11.0 for ROCm 6.2.0

### Added

* Added support for setting generator output ordering in C and C++ API
* `hiprandCreateGeneratorHost` dispatches to the host generator in the rocRAND backend instead of returning with `HIPRAND_STATUS_NOT_IMPLEMENTED`
* Added the option to create a host generator to the Fortran wrapper
* Added the option to create a host generator to the Python wrapper

### Changed

* Updated the default value for the `-a` argument from `rmake.py` to `gfx906:xnack-,gfx1030,gfx1100,gfx1101,gfx1102,gfx1151,gfx1200,gfx1201`.
* For internal testing with HMM the environment variable `ROCRAND_USE_HMM` was used in previous
  versions, it is now changed to `HIPRAND_USE_HMM`.
* The device API documentation is improved in this version.
* Static library: moved all internal symbols to namespaces to avoid potential symbol name collisions when linking.

### Removed

* Removed the option to build hipRAND as a submodule to rocRAND
* Removed references to, and workarounds for, the deprecated `hcc`
* Support for finding rocRAND based on the environment variable `ROCRAND_DIR` has been removed
  `ROCRAND_PATH` can be used instead.

### Resolved issues

* Fixed an issue in `rmake.py` where the list storing cmake options would contain individual characters instead of a full string of options.
* Build error when using Clang++ directly due to unsupported references to `amdgpu-target`

## hipRAND-2.10.17 for ROCm 5.6.0

### Fixes

* Fixed benchmark and unit test builds on Windows

## hipRAND-2.10.16 for ROCm 5.5.0

### Additions

* rocRAND backend support for Sobol 64, Scrambled Sobol 32 and 64, and MT19937
* `hiprandGenerateLongLong` for generating 64-bit uniformly distributed integers with Sobol 64 and
  Scrambled Sobol 64
* Accessor methods for Sobol 32 and 64 direction vectors and constants:
  * Enum `hiprandDirectionVectorSet_t` for direction vector set selection
  * `hiprandGetDirectionVectors32(...)`
  * `hiprandGetDirectionVectors32(...)`
  * `hiprandGetScrambleConstants32(...)`
  * `hiprandGetScrambleConstants32(...)`

### Changes

* Python 2.7 is no longer officially supported.

## hipRAND for ROCm 5.2.0

### Additions

* Backward compatibility for the deprecated `#include <hiprand.h>` using wrapper header files
* Packages for test and benchmark executables on all supported operating systems using CPack

## hipRAND for ROCm 5.0.0

### Additions

* Initial split from rocRAND
