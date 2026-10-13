// SPDX-License-Identifier: MIT

// linear_accel_kernels.cu -- Float32 linear-algebra primitives for gpu/.
//
// Compiled by nvcc --shared into liblinear_accel.so / linear_accel.dll /
// liblinear_accel.dylib and loaded at run time by linear_accel_stub.c via
// dlopen / LoadLibrary. The standard MoonBit toolchain compiles the rest of the
// workspace; no fork required.
//
// Single precision throughout. moonbit_linalg_gpu's equivalent wraps the
// cublasD* family for a double-precision consumer; this tree is Float32 (9769
// Float against 97 Double) and he_init seeds weights from Float32 Box-Muller
// draws, so a double accumulation path would change numerical results rather
// than merely being slower.
//
// Layout: buffers arrive as host float*. cuBLAS works on device pointers, so
// each wrapper allocates, copies in, runs, and copies back. Per-call cudaMalloc
// is the simplest contract and is amortised away at the shapes gpu/ targets --
// the fc1 case is 256 MFLOP. A cached pool is the obvious v2 if profiling says
// otherwise.
//
// Row-major from the caller's side. cuBLAS is column-major, so each wrapper
// swaps operand roles rather than transposing data: row-major C = A*B is
// column-major C^T = B^T * A^T.

#include <cuda_runtime.h>
#include <cublas_v2.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
  #define LA_EXPORT __declspec(dllexport)
#else
  #define LA_EXPORT __attribute__((visibility("default")))
#endif

// One process-wide handle, created on first use and never destroyed: teardown
// at process exit buys nothing and risks ordering against CUDA's own atexit
// handlers.
static cublasHandle_t g_handle = nullptr;

static int la_handle(cublasHandle_t* out) {
  if (!g_handle) {
    if (cublasCreate(&g_handle) != CUBLAS_STATUS_SUCCESS) return -1;
  }
  *out = g_handle;
  return 0;
}

// Copy `bytes` from host to a fresh device buffer. nullptr on failure; the
// caller reports that as a non-zero return and the stub runs the CPU loop.
static void* up(const void* host, size_t bytes) {
  void* dev = nullptr;
  size_t n = bytes ? bytes : sizeof(float);
  if (cudaMalloc(&dev, n) != cudaSuccess) return nullptr;
  if (bytes && cudaMemcpy(dev, host, bytes, cudaMemcpyHostToDevice) != cudaSuccess) {
    cudaFree(dev);
    return nullptr;
  }
  return dev;
}

static void down(void* dev, void* host, size_t bytes) {
  if (bytes) cudaMemcpy(host, dev, bytes, cudaMemcpyDeviceToHost);
}

static void kill(void* dev) { if (dev) cudaFree(dev); }

// ---------------------------------------------------------------------------
// accel_sgemm
// ---------------------------------------------------------------------------
//
// C(M,N) = alpha * op(A)(M,K) * B(K,N) + beta * C(M,N), row-major in and out.
//
// Our row-major C(M,N) is their column-major C^T(N,M). Our row-major A(M,K) is
// their A^T(K,M); our row-major B(K,N) is their B^T(N,K). So the call becomes
// C^T(N,M) = op(B)^T(N,K) * op(A)^T(K,M), which is one Sgemm with the operands
// exchanged and no data moved.

extern "C" LA_EXPORT int
accel_sgemm(int trans_a, int64_t m, int64_t n, int64_t k, float alpha,
            const float* a, int64_t lda, const float* b, int64_t ldb,
            float beta, float* c, int64_t ldc) {
  cublasHandle_t h;
  if (la_handle(&h) != 0) return -1;
  (void)lda; (void)ldb; (void)ldc; // packed rows

  const size_t a_b = (size_t)(m * k) * sizeof(float);
  const size_t b_b = (size_t)(k * n) * sizeof(float);
  const size_t c_b = (size_t)(m * n) * sizeof(float);

  float* da = (float*)up(a, a_b);
  float* db = (float*)up(b, b_b);
  float* dc = (float*)up(c, c_b);

  int rc = -1;
  if (da && db && dc) {
    // In the column-major view: the first operand is B^T (their B is ours
    // transposed) and the second is A with the caller's transposed flag.
    cublasOperation_t op0 = CUBLAS_OP_T;
    cublasOperation_t op1 = trans_a ? CUBLAS_OP_N : CUBLAS_OP_T;
    float al = alpha, be = beta;
    cublasStatus_t st = cublasSgemm(h, op0, op1,
                                    (int)n, (int)m, (int)k,
                                    &al, da, (int)n,
                                    db, (int)k,
                                    &be, dc, (int)n);
    if (st == CUBLAS_STATUS_SUCCESS) {
      down(dc, c, c_b);
      rc = 0;
    }
  }
  kill(da); kill(db); kill(dc);
  return rc;
}

// ---------------------------------------------------------------------------
// accel_sgemv
// ---------------------------------------------------------------------------

extern "C" LA_EXPORT int
accel_sgemv(int trans_a, int64_t m, int64_t n, float alpha, const float* a,
            int64_t lda, const float* x, float beta, float* y) {
  cublasHandle_t h;
  if (la_handle(&h) != 0) return -1;
  (void)lda;

  const size_t a_b = (size_t)(m * n) * sizeof(float);
  const int64_t x_len = trans_a ? m : n;
  const int64_t y_len = trans_a ? n : m;
  const size_t x_b = (size_t)x_len * sizeof(float);
  const size_t y_b = (size_t)y_len * sizeof(float);

  float* da = (float*)up(a, a_b);
  float* dx = (float*)up(x, x_b);
  float* dy = (float*)up(y, y_b);

  int rc = -1;
  if (da && dx && dy) {
    // Our row-major A(M,N) is their column-major A^T(N,M).
    cublasOperation_t op = trans_a ? CUBLAS_OP_T : CUBLAS_OP_N;
    float al = alpha, be = beta;
    cublasStatus_t st = cublasSgemv(h, op, (int)n, (int)m, &al, da, (int)n,
                                    dx, &be, dy, (int)1);
    if (st == CUBLAS_STATUS_SUCCESS) {
      down(dy, y, y_b);
      rc = 0;
    }
  }
  kill(da); kill(dx); kill(dy);
  return rc;
}

// ---------------------------------------------------------------------------
// accel_sdot / accel_snrm2
// ---------------------------------------------------------------------------
//
// Both reduce to a host-side scalar: cuBLAS's Sdot and Snrm2 take a host
// pointer for the result, so there is no device round trip for the output.

extern "C" LA_EXPORT int
accel_sdot(int64_t n, const float* x, const float* y, float* out_dot) {
  cublasHandle_t h;
  if (la_handle(&h) != 0) return -1;
  const size_t b = (size_t)n * sizeof(float);
  float* dx = (float*)up(x, b);
  float* dy = (float*)up(y, b);
  int rc = -1;
  if (dx && dy) {
    float res = 0.0f;
    if (cublasSdot(h, (int)n, dx, 1, dy, 1, &res) == CUBLAS_STATUS_SUCCESS) {
      *out_dot = res;
      rc = 0;
    }
  }
  kill(dx); kill(dy);
  return rc;
}

extern "C" LA_EXPORT int
accel_snrm2(int64_t n, const float* x, float* out_nrm) {
  cublasHandle_t h;
  if (la_handle(&h) != 0) return -1;
  float* dx = (float*)up(x, (size_t)n * sizeof(float));
  int rc = -1;
  if (dx) {
    float res = 0.0f;
    if (cublasSnrm2(h, (int)n, dx, 1, &res) == CUBLAS_STATUS_SUCCESS) {
      *out_nrm = res;
      rc = 0;
    }
    kill(dx);
  }
  return rc;
}

// ---------------------------------------------------------------------------
// accel_sscal / accel_saxpy
// ---------------------------------------------------------------------------

extern "C" LA_EXPORT int
accel_sscal(int64_t n, float alpha, float* x) {
  cublasHandle_t h;
  if (la_handle(&h) != 0) return -1;
  const size_t b = (size_t)n * sizeof(float);
  float* dx = (float*)up(x, b);
  int rc = -1;
  if (dx) {
    float al = alpha;
    if (cublasSscal(h, (int)n, &al, dx, 1) == CUBLAS_STATUS_SUCCESS) {
      down(dx, x, b);
      rc = 0;
    }
    kill(dx);
  }
  return rc;
}

extern "C" LA_EXPORT int
accel_saxpy(int64_t n, float alpha, const float* x, float* y) {
  cublasHandle_t h;
  if (la_handle(&h) != 0) return -1;
  const size_t b = (size_t)n * sizeof(float);
  float* dx = (float*)up(x, b);
  float* dy = (float*)up(y, b);
  int rc = -1;
  if (dx && dy) {
    float al = alpha;
    if (cublasSaxpy(h, (int)n, &al, dx, 1, dy, 1) == CUBLAS_STATUS_SUCCESS) {
      down(dy, y, b);
      rc = 0;
    }
  }
  kill(dx); kill(dy);
  return rc;
}

// ---------------------------------------------------------------------------
// accel_fill
// ---------------------------------------------------------------------------
//
// The one hand-written kernel here. BLAS has no constant fill -- every other
// primitive either scales an existing buffer or accumulates into one -- and a
// three-line kernel is cheaper than a cuBLAS call for it. linalg_kernels.cu in
// moonbit_linalg_gpu does the same, in double.

__global__ void fill_kernel(float* out, float value, long long len) {
  long long i = (long long)blockIdx.x * blockDim.x + threadIdx.x;
  if (i < len) out[i] = value;
}

extern "C" LA_EXPORT int
accel_fill(int64_t n, float value, float* out) {
  if (n <= 0) return 0;
  const size_t b = (size_t)n * sizeof(float);
  float* d = nullptr;
  if (cudaMalloc(&d, b) != cudaSuccess) return -1;
  if (cudaMemcpy(d, out, b, cudaMemcpyHostToDevice) != cudaSuccess) {
    kill(d);
    return -1;
  }
  const int threads = 256;
  const unsigned int blocks = (unsigned int)((n + threads - 1) / threads);
  fill_kernel<<<blocks, threads>>>(d, value, (long long)n);
  cudaError_t launch = cudaGetLastError();
  int rc = -1;
  if (launch == cudaSuccess) {
    down(d, out, b);
    rc = 0;
  } else {
    fprintf(stderr, "[gpu] fill_kernel launch failed: %s\n",
            cudaGetErrorString(launch));
  }
  kill(d);
  return rc;
}
