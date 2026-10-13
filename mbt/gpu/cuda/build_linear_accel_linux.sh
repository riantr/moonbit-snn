#!/usr/bin/env bash
#
# build_linear_accel_linux.sh -- compile linear_accel_kernels.cu into a shared
# library that gpu/ffi.mbt reaches at run time.
#
# Output: this directory / liblinear_accel.so (or .dylib on macOS), which
# linear_accel_stub.c dlopens.
#
# Why a script and not a native-stub dependency
# ---------------------------------------------
# The stub that MoonBit's TCC compiles knows how to dlopen this library and
# resolve each symbol. It never links cublas, cudart or cusolverDn. That
# decoupling is what keeps the CUDA toolchain optional: `moon check` works on a
# machine with no nvcc, and the accelerated path simply reports itself absent.
# moonbit_linalg_gpu reaches the same conclusion for the same reason.
#
# Env overrides:
#   NVCC          compiler, default nvcc
#   GENCODE       -gencode flags, default a spread that covers common cards
#   OUT           output path, default alongside this script
#
# Usage:
#   ./build_linear_accel_linux.sh

set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
src="$here/linear_accel_kernels.cu"

NVCC="${NVCC:-nvcc}"

if [[ "$(uname -s)" == "Darwin" ]]; then
  default_out="liblinear_accel.dylib"
  # Float32 BLAS gains nothing from a narrow compute capability, so the default
  # is just the architectures a Mac might have.
  default_gencode="-gencode arch=compute_75,code=sm_75"
else
  default_out="liblinear_accel.so"
  default_gencode="-gencode arch=compute_70,code=sm_70 \
                   -gencode arch=compute_75,code=sm_75 \
                   -gencode arch=compute_80,code=sm_80 \
                   -gencode arch=compute_86,code=sm_86 \
                   -gencode arch=compute_89,code=sm_89"
fi

out="${OUT:-$here/$default_out}"
gencode="${GENCODE:-$default_gencode}"

if ! command -v "$NVCC" >/dev/null 2>&1; then
  cat >&2 <<EOF
error: '$NVCC' not found.

gpu/ works without CUDA -- linear_accel_stub.c falls back to a CPU loop and
prints one line saying so. To build the accelerated library, install the CUDA
toolkit and make sure nvcc is on PATH, or set NVCC to its full path.
EOF
  exit 127
fi

echo "nvcc      : $("$NVCC" --version | tail -1)"
echo "source    : $src"
echo "output    : $out"
echo "gencode   : $gencode"

# shellcheck disable=SC2086
"$NVCC" --shared --optimize -O3 -Xcompiler -fPIC $gencode -o "$out" "$src"

echo
echo "built $out"
echo
echo "The stub probes ./, ../ and ../../ from its own location, so the file is"
echo "already findable if you leave it here. To put it elsewhere:"
echo "  export LINEAR_ACCEL_LIB=/abs/path/$(basename "$out")"
