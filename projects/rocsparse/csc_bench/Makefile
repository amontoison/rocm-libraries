# ROCSPARSE_DIR: a rocSPARSE build or install with BUILD_WITH_SPTRSV_CSC=ON.
#   build tree:   make ROCSPARSE_DIR=<rocm-libraries>/projects/rocsparse/build/<dir>
#   install tree: make ROCSPARSE_DIR=/opt/rocm  (or the install prefix)
# GPU_ARCH: e.g. gfx942 (MI300), gfx90a (MI200), gfx1101.
ROCM_PATH     ?= /opt/rocm
ROCSPARSE_DIR ?= $(ROCM_PATH)
GPU_ARCH      ?= gfx942

# A build tree has the headers in include/ and the library in library/; an install tree in include/ and lib/.
INC := -I$(ROCSPARSE_DIR)/include
LIB := $(firstword $(wildcard $(ROCSPARSE_DIR)/library/librocsparse.so) $(ROCSPARSE_DIR)/lib/librocsparse.so)
LIBDIR := $(dir $(LIB))

bench_sptrsv_csc: bench_sptrsv_csc.cpp
	$(ROCM_PATH)/bin/hipcc -O2 -std=c++17 --offload-arch=$(GPU_ARCH) $(INC) $< -o $@ \
	    -L$(LIBDIR) -lrocsparse -Wl,-rpath,$(LIBDIR)

clean:
	rm -f bench_sptrsv_csc

.PHONY: clean
