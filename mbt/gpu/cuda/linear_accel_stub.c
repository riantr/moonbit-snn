// linear_accel_stub.c -- FFI shim between MoonBit and liblinear_accel.
//
// Compiled by MoonBit's TCC as `native-stub`. The single-precision cuBLAS
// kernel library is built separately by nvcc (see
// build_linear_accel_linux.sh) into a shared object this file dlopens at
// runtime. TCC therefore never links cublas or cudart, so a machine with no
// CUDA toolchain can still build and run the whole tree -- the accelerated
// path becomes an optimisation rather than a requirement.
//
// What differs from moonbit_linalg_gpu's linalg_stub.c, and why
// ---------------------------------------------------------
// 1. Single precision throughout. That project wraps Dgemm/Dgemv/Ddot/Dscal
//    because its consumer is moonbit_doubleML. This tree is Float32 -- 9769
//    Float against 97 Double -- so the wrappers here are the cublasS* family.
//    Accumulating in double would change numerical results, which the
//    bit-exact-Julia-port contract forbids.
//
// 2. Every wrapper falls back to a CPU loop and still returns 0.
//    linalg_stub returns ERR_LIB_NOT_LOADED and leaves the recompute to the
//    MoonBit side. Here the stub does the scalar work itself, so a caller that
//    forgets to check the return value still gets the right answer. That is
//    deliberate: the fallback is a few lines, and it removes a class of bug
//    where a new call site forgets to branch.
//
//    The consequence is that 0 means "correct", not "the GPU did this".
//    la_last_backend() reports which path ran.
//
// 3. Small surface: seven entry points, level-1/2/3 only. No LAPACK. Nothing
//    in this tree has a Cholesky or triangular solve on a hot path, and
//    leaving them out means a machine without cusolverDn still loads.
//
// Backend selection
// -----------------
// LINEAR_ACCEL_LIB names the shared object. Failing that, the probe list covers
// where a build script is likely to leave it: beside the stub, and one or two
// directories up.
//
// Return codes (matching gpu/ffi.mbt and gpu/gemm.mbt):
//   0                     correct result -- GPU or CPU, see la_last_backend
//   -1000 ERR_LIB_NOT_LOADED   the kernel library is not present
//   -2000 ERR_BAD_ARG          negative dimension or null buffer
//
// A cuBLAS failure at run time is NOT propagated. The stub falls through to
// the CPU loop and returns 0, because a status code the caller cannot act on is
// worse than a slower correct answer.

#include <moonbit.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Duplicated from gpu/ffi.mbt rather than included: the kernel library must not
// have to see moonbit.h, and neither should this file's constants depend on a
// MoonBit build.
#define LA_ERR_LIB_NOT_LOADED (-1000)
#define LA_ERR_BAD_ARG (-2000)

// Which path produced the last successful result.
#define LA_BACKEND_NONE (0)
#define LA_BACKEND_CUBLAS (1)
#define LA_BACKEND_CPU (2)

static int g_last_backend = LA_BACKEND_NONE;

// ---------------------------------------------------------------------------
// Platform dynamic loading
// ---------------------------------------------------------------------------

#ifdef _WIN32
  #include <windows.h>
  typedef HMODULE la_hlib_t;
  #define LA_DEFAULT_LIB_NAME "linear_accel.dll"
  static la_hlib_t la_dlopen(const char* path) { return LoadLibraryA(path); }
  static void* la_dlsym(la_hlib_t h, const char* sym) {
    return (void*)GetProcAddress(h, sym);
  }
#else
  #include <dlfcn.h>
  typedef void* la_hlib_t;
  #if defined(__APPLE__)
    #define LA_DEFAULT_LIB_NAME "liblinear_accel.dylib"
  #else
    #define LA_DEFAULT_LIB_NAME "liblinear_accel.so"
  #endif
  static la_hlib_t la_dlopen(const char* path) {
    return dlopen(path, RTLD_NOW | RTLD_LOCAL);
  }
  static void* la_dlsym(la_hlib_t h, const char* sym) {
    (void)h;
    (void)dlerror(); // clear a stale error before asking
    return dlsym(h, sym);
  }
#endif

// ---------------------------------------------------------------------------
// Kernel library surface
// ---------------------------------------------------------------------------
//
// LA_FOREACH_FN is the single source of truth for what must be present. Adding
// an entry here and in the .cu is the whole job: the slot is declared, looked
// up, and dispatched through LA_FN below.

typedef struct la_table {
  la_hlib_t lib;
  int (*face_sgemm)(int, int64_t, int64_t, int64_t, float, const float*,
                    int64_t, const float*, int64_t, float, float*, int64_t);
  int (*face_sgemv)(int, int64_t, int64_t, float, const float*, int64_t,
                    const float*, float, float*);
  int (*face_sdot)(int64_t, const float*, const float*, float*);
  int (*face_snrm2)(int64_t, const float*, float*);
  int (*face_sscal)(int64_t, float, float*);
  int (*face_saxpy)(int64_t, float, const float*, float*);
  int (*face_fill)(int64_t, float, float*);
} la_table;

static la_table g_la;
static int g_la_state = 0; // 0 = not tried, 1 = loaded, -1 = unavailable

// The table above is written out rather than generated by a FOREACH macro.
// An earlier version did generate it, and that was a mistake: the slots are
// named for the operation (face_sgemm) while the exported symbols carry the
// accel_ prefix (accel_sgemm), and a macro cannot strip a prefix from a pasted
// token. The FOREACH version therefore produced face_accel_sgemm, which is
// what the compiler rejected. Seven written-out declarations, and a lookup
// that names each symbol explicitly, are less clever and actually work.
#define LA_FN(name) (g_la.face_##name)

static const char* const kLaSearchNames[] = {
  LA_DEFAULT_LIB_NAME,
  "./" LA_DEFAULT_LIB_NAME,
  "../" LA_DEFAULT_LIB_NAME,
  "../../" LA_DEFAULT_LIB_NAME,
  NULL,
};

static int la_lookup_all(la_table* t) {
  // Looked up one at a time rather than through a FOREACH expansion: the slot
  // names are face_sgemm while the exported symbols are accel_sgemm, and a
  // macro cannot strip a prefix from a pasted token. Spelling out seven
  // lookups is cheaper than the preprocessor trick that would hide a typo in
  // one of them.
#define LA_REQUIRE(sym, slot)   do {     t->slot = (void*)0;     *(void**)(&t->slot) = la_dlsym(t->lib, sym);     if (!t->slot) {       fprintf(stderr, "[gpu] missing symbol: %s\n", sym);       return -1;     }   } while (0)
  LA_REQUIRE("accel_sgemm", face_sgemm);
  LA_REQUIRE("accel_sgemv", face_sgemv);
  LA_REQUIRE("accel_sdot",  face_sdot);
  LA_REQUIRE("accel_snrm2", face_snrm2);
  LA_REQUIRE("accel_sscal", face_sscal);
  LA_REQUIRE("accel_saxpy", face_saxpy);
  LA_REQUIRE("accel_fill",  face_fill);
#undef LA_REQUIRE
  return 0;
}

static int la_try_load_at(la_table* t, const char* path) {
  t->lib = la_dlopen(path);
  if (!t->lib) return -1;
  if (la_lookup_all(t) != 0) {
#ifdef _WIN32
    FreeLibrary(t->lib);
#else
    dlclose(t->lib);
#endif
    t->lib = NULL;
    return -1;
  }
  return 0;
}

// Resolve the kernel library once, then cache the outcome for the process.
static int la_ensure_loaded(void) {
  if (g_la_state == 1) return 0;
  if (g_la_state == -1) return -1;
  g_la_state = -1; // pessimistic until proven otherwise
  memset(&g_la, 0, sizeof(g_la));

  const char* env = getenv("LINEAR_ACCEL_LIB");
  if (env && *env) {
    if (la_try_load_at(&g_la, env) == 0) { g_la_state = 1; return 0; }
    fprintf(stderr,
            "[gpu] LINEAR_ACCEL_LIB=%s did not resolve; using the CPU path.\n",
            env);
    return -1;
  }

  for (int i = 0; kLaSearchNames[i] != NULL; i++) {
    if (la_try_load_at(&g_la, kLaSearchNames[i]) == 0) {
      g_la_state = 1;
      return 0;
    }
  }

  // One line, once per process. The CPU path is a working result, so this is a
  // note rather than an error.
  fprintf(stderr,
          "[gpu] " LA_DEFAULT_LIB_NAME " not found -- the CPU path will be used. "
          "Build it with build_linear_accel_linux.sh, or set "
          "LINEAR_ACCEL_LIB=/abs/path\n");
  return -1;
}

// ---------------------------------------------------------------------------
// CPU reference implementations
// ---------------------------------------------------------------------------
//
// Not apologetic fallbacks: these define what the accelerated path has to agree
// with, and they are what a test compares against. Written for obvious
// correctness rather than speed.

// y[o] = alpha * sum_i A[o][i]*x[i] + beta*y[o], A row-major m-by-k.
static void cpu_sgemv_n(int64_t m, int64_t k, float alpha, const float* a,
                        const float* x, float beta, float* y) {
  for (int64_t o = 0; o < m; o++) {
    float acc = 0.0f;
    for (int64_t i = 0; i < k; i++) acc += a[o * k + i] * x[i];
    y[o] = alpha * acc + beta * y[o];
  }
}

// y[i] = alpha * sum_o A[o][i]*x[o] + beta*y[i] (A read transposed).
static void cpu_sgemv_t(int64_t m, int64_t k, float alpha, const float* a,
                        const float* x, float beta, float* y) {
  for (int64_t i = 0; i < k; i++) {
    float acc = 0.0f;
    for (int64_t o = 0; o < m; o++) acc += a[o * k + i] * x[o];
    y[i] = alpha * acc + beta * y[i];
  }
}

// C[o][col] = alpha * sum_i op(A)[o][i] * B[i][col] + beta * C[o][col], all
// row-major, which is the caller's view.
static void cpu_sgemm(int trans_a, int64_t m, int64_t n, int64_t k, float alpha,
                      const float* a, const float* b, float beta, float* c) {
  for (int64_t o = 0; o < m; o++) {
    for (int64_t col = 0; col < n; col++) {
      float acc = 0.0f;
      if (trans_a == 0) {
        for (int64_t i = 0; i < k; i++) acc += a[o * k + i] * b[i * n + col];
      } else {
        for (int64_t i = 0; i < k; i++) acc += a[i * m + o] * b[i * n + col];
      }
      c[o * n + col] = alpha * acc + beta * c[o * n + col];
    }
  }
}

// ---------------------------------------------------------------------------
// Exported surface
// ---------------------------------------------------------------------------

// Load the kernel library, or report that it is not there.
MOONBIT_FFI_EXPORT int32_t la_try_load(void) {
  if (g_la_state == 1) return 0;
  if (g_la_state == -1) return LA_ERR_LIB_NOT_LOADED;
  return la_ensure_loaded() == 0 ? 0 : LA_ERR_LIB_NOT_LOADED;
}

// LA_BACKEND_CUBLAS or LA_BACKEND_CPU: which path produced the last result.
MOONBIT_FFI_EXPORT int32_t la_last_backend(void) { return g_last_backend; }

// C(M,N) = alpha * op(A) * B + beta * C, row-major. trans_a 0 is A, 1 is A^T.
MOONBIT_FFI_EXPORT
int32_t la_sgemm(int32_t trans_a, int64_t m, int64_t n, int64_t k, float alpha,
                 float* a, int64_t lda, float* b, int64_t ldb, float beta,
                 float* c, int64_t ldc) {
  if (m < 0 || n < 0 || k < 0 || trans_a < 0 || trans_a > 1) return LA_ERR_BAD_ARG;
  if (!a || !b || !c) return LA_ERR_BAD_ARG;
  (void)lda; (void)ldb; (void)ldc; // the CPU path assumes packed rows
  if (la_ensure_loaded() == 0) {
    int rc = LA_FN(sgemm)(trans_a, m, n, k, alpha, a, lda, b, ldb, beta, c, ldc);
    if (rc == 0) { g_last_backend = LA_BACKEND_CUBLAS; return 0; }
    // A cuBLAS failure is not propagated; the CPU path below answers instead.
  }
  cpu_sgemm(trans_a, m, n, k, alpha, a, b, beta, c);
  g_last_backend = LA_BACKEND_CPU;
  return 0;
}

// y = alpha * op(A) * x + beta * y, row-major.
MOONBIT_FFI_EXPORT
int32_t la_sgemv(int32_t trans_a, int64_t m, int64_t n, float alpha, float* a,
                 int64_t lda, float* x, float beta, float* y) {
  if (m < 0 || n < 0 || trans_a < 0 || trans_a > 1) return LA_ERR_BAD_ARG;
  if (!a || !x || !y) return LA_ERR_BAD_ARG;
  (void)lda;
  if (la_ensure_loaded() == 0) {
    int rc = LA_FN(sgemv)(trans_a, m, n, alpha, a, lda, x, beta, y);
    if (rc == 0) { g_last_backend = LA_BACKEND_CUBLAS; return 0; }
  }
  if (trans_a == 0) cpu_sgemv_n(m, n, alpha, a, x, beta, y);
  else             cpu_sgemv_t(m, n, alpha, a, x, beta, y);
  g_last_backend = LA_BACKEND_CPU;
  return 0;
}

// out[0] = dot(x, y) over the first n elements.
MOONBIT_FFI_EXPORT
int32_t la_sdot(int64_t n, float* x, float* y, float* out_dot) {
  if (n < 0 || !x || !y || !out_dot) return LA_ERR_BAD_ARG;
  if (la_ensure_loaded() == 0) {
    int rc = LA_FN(sdot)(n, x, y, out_dot);
    if (rc == 0) { g_last_backend = LA_BACKEND_CUBLAS; return 0; }
  }
  float acc = 0.0f;
  for (int64_t i = 0; i < n; i++) acc += x[i] * y[i];
  out_dot[0] = acc;
  g_last_backend = LA_BACKEND_CPU;
  return 0;
}

// out[0] = ||x||_2 over the first n elements.
MOONBIT_FFI_EXPORT
int32_t la_snrm2(int64_t n, float* x, float* out_nrm) {
  if (n < 0 || !x || !out_nrm) return LA_ERR_BAD_ARG;
  if (la_ensure_loaded() == 0) {
    int rc = LA_FN(snrm2)(n, x, out_nrm);
    if (rc == 0) { g_last_backend = LA_BACKEND_CUBLAS; return 0; }
  }
  float acc = 0.0f;
  for (int64_t i = 0; i < n; i++) acc += x[i] * x[i];
  out_nrm[0] = sqrtf(acc);
  g_last_backend = LA_BACKEND_CPU;
  return 0;
}

// x[i] *= alpha for i in [0, n). In place.
MOONBIT_FFI_EXPORT
int32_t la_sscal(int64_t n, float alpha, float* x) {
  if (n < 0 || !x) return LA_ERR_BAD_ARG;
  if (la_ensure_loaded() == 0) {
    int rc = LA_FN(sscal)(n, alpha, x);
    if (rc == 0) { g_last_backend = LA_BACKEND_CUBLAS; return 0; }
  }
  for (int64_t i = 0; i < n; i++) x[i] *= alpha;
  g_last_backend = LA_BACKEND_CPU;
  return 0;
}

// y[i] += alpha * x[i] for i in [0, n). In place on y.
MOONBIT_FFI_EXPORT
int32_t la_saxpy(int64_t n, float alpha, float* x, float* y) {
  if (n < 0 || !x || !y) return LA_ERR_BAD_ARG;
  if (la_ensure_loaded() == 0) {
    int rc = LA_FN(saxpy)(n, alpha, x, y);
    if (rc == 0) { g_last_backend = LA_BACKEND_CUBLAS; return 0; }
  }
  for (int64_t i = 0; i < n; i++) y[i] += alpha * x[i];
  g_last_backend = LA_BACKEND_CPU;
  return 0;
}

// out[i] = value for i in [0, n). BLAS has no fill primitive, which is why
// this exists at all.
MOONBIT_FFI_EXPORT
int32_t la_fill(int64_t n, float value, float* out) {
  if (n < 0 || !out) return LA_ERR_BAD_ARG;
  if (la_ensure_loaded() == 0) {
    int rc = LA_FN(fill)(n, value, out);
    if (rc == 0) { g_last_backend = LA_BACKEND_CUBLAS; return 0; }
  }
  for (int64_t i = 0; i < n; i++) out[i] = value;
  g_last_backend = LA_BACKEND_CPU;
  return 0;
}
