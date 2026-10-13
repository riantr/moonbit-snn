// ffi.mbt — Single-precision BLAS entry points, loaded at runtime.
//
// Design notes, since this file deliberately does not look like ffi.mbt in
// moonbit_linalg_gpu even though the shape is the same.
//
// Why single precision and not double
// -----------------------------------
// linalg_gpu wraps Dgemm/Dgemv/Ddot/Dscal/Dtrsm and cusolverDnDpotrf, all
// Double. Those cannot be reused here even by changing the wrapper: this tree
// is 9769 Float against 97 Double, and he_init seeds weights from Float32
// Box-Muller draws in core/rng.mbt. Accumulating in double would change the
// numerical result, which breaks the bit-exact-Julia-port contract the project
// is built on. So this is cublasSgemm and friends, written fresh.
//
// Backend split
// -------------
// MoonBit rejects `extern "c"` on the wasm, wasm-gc and js backends, and a
// package containing raw FFI cannot be a dependency of one declaring
// supported_targets = "native+wasm+wasm-gc+js" -- the dependency graph has no
// intersection. The project is currently single-target (preferred_target =
// "native"), so this split is not strictly required yet; it is here anyway
// because it costs nothing now and retrofitting it later means touching every
// declaration.
//
// If the library is missing, every stub returns ERR_LIB_NOT_LOADED and
// linear_forward falls through to its existing scalar loop. That is
// deliberately indistinguishable from "no GPU in this machine": the caller
// does not branch on a capability it cannot observe, it just gets the answer
// it would have got anyway.
//
// ABI conventions
// ---------------
//   * `FixedArray[Float]` round-trips to `float*`. The stub writes results in
//     place; the change is visible after the call returns. `#borrow` tells
//     MoonBit we own the buffer and the C side must not keep the reference.
//   * Dimensions are `Int64` even though cuBLAS takes `int`. cuBLAS truncates;
//     passing 64-bit from here keeps the MoonBit signature aligned with the rest
//     of the project rather than encoding a narrowing.
//   * The library path is resolved by the stub from LINEAR_ACCEL_LIB, falling
//     back to a relative probe. No path argument is threaded through the FFI
//     surface, so the same code works whatever the install layout is.
//   * All matrices are row-major from MoonBit's side. cuBLAS is column-major
//     and the stub performs the swap internally.

///|
/// Sentinel: liblinear_accel could not be loaded.
///
/// Negative on purpose. cuBLAS status codes are 0 or positive, so a negative
/// return unambiguously means "the device path is not available here" and can
/// be handled without enumerating vendor codes.
pub const ERR_LIB_NOT_LOADED : Int = -1000

///|
/// Sentinel: a dimension was negative or a buffer was smaller than the shape
/// requires. Distinct from ERR_LIB_NOT_LOADED because a caller that gets this
/// has a bug, not a missing GPU.
pub const ERR_BAD_ARG : Int = -2000

// ===========================================================================
// native: the real FFI surface.
// ===========================================================================

///|
/// Try to load liblinear_accel now. Returns 0 if the shared library was found
/// and every expected symbol resolved; non-zero on any failure. The handle is
/// cached process-wide, so a successful call makes later ones unnecessary.
#cfg(target="native")
extern "c" fn la_try_load() -> Int = "la_try_load"

///|
/// C(M,N) = alpha * A(M,K) * B(K,N) + beta * C(M,N), all row-major.
/// A needs M*K floats, B needs K*N, C needs M*N. `trans_a` non-zero reads A
/// as its transpose instead.
#cfg(target="native")
#borrow(a, b, c)
extern "c" fn la_sgemm(
  trans_a : Int,
  m : Int64,
  n : Int64,
  k : Int64,
  alpha : Float,
  a : FixedArray[Float],
  lda : Int64,
  b : FixedArray[Float],
  ldb : Int64,
  beta : Float,
  c : FixedArray[Float],
  ldc : Int64,
) -> Int = "la_sgemm"

///|
/// y(M) = alpha * A(M,N) * x(N) + beta * y(M), A row-major.
/// `trans_a` non-zero computes y(N) = alpha * A^T * x + beta * y instead.
#cfg(target="native")
#borrow(a, x, y)
extern "c" fn la_sgemv(
  trans_a : Int,
  m : Int64,
  n : Int64,
  alpha : Float,
  a : FixedArray[Float],
  lda : Int64,
  x : FixedArray[Float],
  beta : Float,
  y : FixedArray[Float],
) -> Int = "la_sgemv"

///|
/// out[0] = dot(x, y) over the first `n` elements. A cuBLAS `dot` writes
/// through a pointer because a Float cannot be returned by FFI.
#cfg(target="native")
#borrow(x, y, out_dot)
extern "c" fn la_sdot(
  n : Int64,
  x : FixedArray[Float],
  y : FixedArray[Float],
  out_dot : FixedArray[Float],
) -> Int = "la_sdot"

///|
/// out[0] = ||x||_2 over the first `n` elements.
#cfg(target="native")
#borrow(x, out_nrm)
extern "c" fn la_snrm2(
  n : Int64,
  x : FixedArray[Float],
  out_nrm : FixedArray[Float],
) -> Int = "la_snrm2"

///|
/// x[i] *= alpha for i in [0, n).
#cfg(target="native")
#borrow(x)
extern "c" fn la_sscal(n : Int64, alpha : Float, x : FixedArray[Float]) -> Int = "la_sscal"

///|
/// y[i] += alpha * x[i] for i in [0, n).
#cfg(target="native")
#borrow(x, y)
extern "c" fn la_saxpy(
  n : Int64,
  alpha : Float,
  x : FixedArray[Float],
  y : FixedArray[Float],
) -> Int = "la_saxpy"

///|
/// Fill out[0..n) with `value`. There is no BLAS primitive for this -- it is
/// the reason linalg_gpu carries a hand-written CUDA kernel -- so it is
/// exposed here to keep that kernel out of this file.
#cfg(target="native")
#borrow(out)
extern "c" fn la_fill(
  n : Int64,
  value : Float,
  out : FixedArray[Float],
) -> Int = "la_fill"

// ===========================================================================
// Everything else: same names and signatures, no FFI.
// ===========================================================================

///|
#cfg(not(target="native"))
fn la_try_load() -> Int {
  ERR_LIB_NOT_LOADED
}

///|
#cfg(not(target="native"))
fn la_sgemm(
  trans_a : Int,
  m : Int64,
  n : Int64,
  k : Int64,
  alpha : Float,
  a : FixedArray[Float],
  lda : Int64,
  b : FixedArray[Float],
  ldb : Int64,
  beta : Float,
  c : FixedArray[Float],
  ldc : Int64,
) -> Int {
  let _ = (trans_a, m, n, k, alpha, a, lda, b, ldb, beta, c, ldc)
  ERR_LIB_NOT_LOADED
}

///|
#cfg(not(target="native"))
fn la_sgemv(
  trans_a : Int,
  m : Int64,
  n : Int64,
  alpha : Float,
  a : FixedArray[Float],
  lda : Int64,
  x : FixedArray[Float],
  beta : Float,
  y : FixedArray[Float],
) -> Int {
  let _ = (trans_a, m, n, alpha, a, lda, x, beta, y)
  ERR_LIB_NOT_LOADED
}

///|
#cfg(not(target="native"))
fn la_sdot(
  n : Int64,
  x : FixedArray[Float],
  y : FixedArray[Float],
  out_dot : FixedArray[Float],
) -> Int {
  let _ = (n, x, y, out_dot)
  ERR_LIB_NOT_LOADED
}

///|
#cfg(not(target="native"))
fn la_snrm2(n : Int64, x : FixedArray[Float], out_nrm : FixedArray[Float]) -> Int {
  let _ = (n, x, out_nrm)
  ERR_LIB_NOT_LOADED
}

///|
#cfg(not(target="native"))
fn la_sscal(n : Int64, alpha : Float, x : FixedArray[Float]) -> Int {
  let _ = (n, alpha, x)
  ERR_LIB_NOT_LOADED
}

///|
#cfg(not(target="native"))
fn la_saxpy(
  n : Int64,
  alpha : Float,
  x : FixedArray[Float],
  y : FixedArray[Float],
) -> Int {
  let _ = (n, alpha, x, y)
  ERR_LIB_NOT_LOADED
}

///|
#cfg(not(target="native"))
fn la_fill(n : Int64, value : Float, out : FixedArray[Float]) -> Int {
  let _ = (n, value, out)
  ERR_LIB_NOT_LOADED
}