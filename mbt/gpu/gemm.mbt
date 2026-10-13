// gemm.mbt — Checked wrappers over the FFI surface in ffi.mbt.
//
// Every wrapper returns an Int. Read it like this:
//
//   0                            success
//   ERR_LIB_NOT_LOADED  (-1000)  liblinear_accel is not present on this
//                                machine -- fall back to the CPU path
//   ERR_BAD_ARG        (-2000)  caller bug: negative dimension, or a buffer
//                                shorter than the shape requires
//   >0                             cuBLAS status code (see cublas_v2.h)
//
// The -1000 case is the one that matters. It is indistinguishable from
// "GPU library not on PATH", which is the point: a caller branches on `rc != 0`
// and uses its existing scalar loop, and never has to know whether a device
// exists.
//
// Nothing here allocates. Buffers are the caller's, and the stub writes results
// in place, so a hot path can reuse the same arrays across calls.

///|
/// C(M,N) = alpha * A(M,K) * B(K,N) + beta * C(M,N), row-major throughout.
///
/// `trans_a` non-zero multiplies by A's transpose, so callers computing
/// X·W^T (as nn/linear_forward does) can pass it instead of transposing a
/// buffer first.
///
/// Required sizes: `a` >= M*K, `b` >= K*N, `c` >= M*N. `lda`, `ldb` and `ldc`
/// are the leading dimensions and should be the row stride -- pass the column
/// count for a packed matrix.
pub fn sgemm(
  m : Int,
  n : Int,
  k : Int,
  a : FixedArray[Float],
  b : FixedArray[Float],
  c : FixedArray[Float],
  trans_a? : Int = 0,
  alpha? : Float = 1.0F,
  beta? : Float = 0.0F,
  lda? : Int = 0,
  ldb? : Int = 0,
  ldc? : Int = 0,
) -> Int {
  if m < 0 || n < 0 || k < 0 || trans_a < 0 || trans_a > 1 {
    return ERR_BAD_ARG
  }
  // Rows of A are K when transposed and M otherwise; the element count is the
  // same either way, which is what the bounds check needs.
  if a.length() < m * k || b.length() < k * n || c.length() < m * n {
    return ERR_BAD_ARG
  }
  let m64 = m.to_int64()
  let n64 = n.to_int64()
  let k64 = k.to_int64()
  let lda64 = (if lda > 0 { lda } else { n }).to_int64()
  let ldb64 = (if ldb > 0 { ldb } else { k }).to_int64()
  let ldc64 = (if ldc > 0 { ldc } else { n }).to_int64()
  la_sgemm(
    trans_a, m64, n64, k64, alpha, a, lda64, b, ldb64, beta, c, ldc64,
  )
}

///|
/// y = alpha * op(A) * x + beta * y, row-major.
///
/// `trans_a` 0 computes y(M) = alpha * A(M,N) * x(N) + beta * y(M);
/// `trans_a` 1 computes y(N) = alpha * A^T * x(M) + beta * y(N).
///
/// `x` needs N (or M when transposed) elements, `y` needs M (or N).
pub fn sgemv(
  m : Int,
  n : Int,
  a : FixedArray[Float],
  x : FixedArray[Float],
  y : FixedArray[Float],
  trans_a? : Int = 0,
  alpha? : Float = 1.0F,
  beta? : Float = 0.0F,
  lda? : Int = 0,
) -> Int {
  if m < 0 || n < 0 || trans_a < 0 || trans_a > 1 {
    return ERR_BAD_ARG
  }
  if a.length() < m * n {
    return ERR_BAD_ARG
  }
  let len_x = if trans_a == 0 { n } else { m }
  let len_y = if trans_a == 0 { m } else { n }
  if x.length() < len_x || y.length() < len_y {
    return ERR_BAD_ARG
  }
  let lda64 = (if lda > 0 { lda } else { n }).to_int64()
  la_sgemv(
    trans_a,
    m.to_int64(),
    n.to_int64(),
    alpha,
    a,
    lda64,
    x,
    beta,
    y,
  )
}

///|
/// out[0] = sum of x[i]*y[i] over i in [0, n).
///
/// `out` must have room for one Float. cuBLAS returns the dot product through
/// a pointer because a Float cannot cross the FFI boundary as a return value.
pub fn sdot(n : Int, x : FixedArray[Float], y : FixedArray[Float], out : FixedArray[Float]) -> Int {
  if n < 0 || x.length() < n || y.length() < n || out.length() < 1 {
    return ERR_BAD_ARG
  }
  la_sdot(n.to_int64(), x, y, out)
}

///|
/// out[0] = sqrt(sum of x[i]^2) over i in [0, n).
pub fn snrm2(n : Int, x : FixedArray[Float], out : FixedArray[Float]) -> Int {
  if n < 0 || x.length() < n || out.length() < 1 {
    return ERR_BAD_ARG
  }
  la_snrm2(n.to_int64(), x, out)
}

///|
/// x[i] *= alpha for i in [0, n). In place.
pub fn sscal(n : Int, x : FixedArray[Float], alpha? : Float = 1.0F) -> Int {
  if n < 0 || x.length() < n {
    return ERR_BAD_ARG
  }
  la_sscal(n.to_int64(), alpha, x)
}

///|
/// y[i] += alpha * x[i] for i in [0, n). In place on `y`.
pub fn saxpy(
  n : Int,
  x : FixedArray[Float],
  y : FixedArray[Float],
  alpha? : Float = 1.0F,
) -> Int {
  if n < 0 || x.length() < n || y.length() < n {
    return ERR_BAD_ARG
  }
  la_saxpy(n.to_int64(), alpha, x, y)
}

///|
/// out[i] = value for i in [0, n). In place.
///
/// Exists because BLAS has no fill primitive -- every BLAS operation either
/// scales an existing buffer or accumulates into one. linalg_gpu writes a CUDA
/// kernel for this; here it is one FFI call into the same stub.
pub fn fill(n : Int, value : Float, out : FixedArray[Float]) -> Int {
  if n < 0 || out.length() < n {
    return ERR_BAD_ARG
  }
  la_fill(n.to_int64(), value, out)
}