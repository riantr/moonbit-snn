// tensor_ops.mbt 鈥?matmul + softmax + log_softmax (v0.13.1).
//
// All ops are forward-only and operate on Tensor structs (or
// elementwise float arrays for softmax).

///|
/// 2D matrix multiply: `c[m, n] = sum_k a[m, k] * b[k, n]`.
/// Inputs `a` is shape `[m, k]`, `b` is shape `[k, n]`. Returns a
/// fresh `Tensor` of shape `[m, n]`. Naive O(m*n*k) loop.
pub fn tensor_matmul(a : Tensor, b : Tensor) -> Tensor {
  if a.shape.length() != 2 || b.shape.length() != 2 {
    abort("tensor_matmul: both inputs must be 2D")
  }
  let m = a.shape[0]
  let k = a.shape[1]
  let k2 = b.shape[0]
  let n = b.shape[1]
  if k != k2 {
    abort("tensor_matmul: shape mismatch a[\{m},\{k}] x b[\{k2},\{n}]")
  }
  let out_data : Array[Float] = Array::make(m * n, 0.0F)
  for i in 0..<m {
    let a_row_off = i * k
    let out_row_off = i * n
    for j in 0..<n {
      let mut acc : Float = 0.0F
      for k_i in 0..<k {
        acc = acc + a.data[a_row_off + k_i] * b.data[k_i * n + j]
      }
      out_data[out_row_off + j] = acc
    }
  }
  { data: out_data, shape: [m, n] }
}

///|
/// Softmax along the last axis of a 2D Tensor. Numerically stable:
/// subtracts max(x) before exp.
///   `out[i, j] = exp(x[i, j] - max_j(x[i, j])) / sum_j exp(...)`
pub fn tensor_softmax(x : Tensor) -> Tensor {
  if x.shape.length() != 2 {
    abort("tensor_softmax: input must be 2D")
  }
  let m = x.shape[0]
  let n = x.shape[1]
  let out : Array[Float] = Array::make(m * n, 0.0F)
  for i in 0..<m {
    let row_off = i * n
    // Compute max over the row for stability.
    let mut mx : Float = x.data[row_off]
    for j in 1..<n {
      if x.data[row_off + j] > mx { mx = x.data[row_off + j] }
    }
    // Compute exp(x - max) and sum.
    let mut sum : Float = 0.0F
    for j in 0..<n {
      // We do the exp via repeated multiplication as an approximation,
      // since the project's libm FFI for `expf` isn't called here for
      // portability. Instead, use the Taylor series via Horner; for
      // small (x - max) values this is fine. For larger values, use
      // a polynomial approximation. For correctness against torch,
      // we'd want libm. We approximate via Math.
      let v = x.data[row_off + j] - mx
      // exp(v) using libm via `@math.exp` is Double; convert.
      let approx = (math_exp_f32(v))
      out[row_off + j] = approx
      sum = sum + approx
    }
    // Normalise.
    let inv_sum = 1.0F / sum
    for j in 0..<n {
      out[row_off + j] = out[row_off + j] * inv_sum
    }
  }
  { data: out, shape: [m, n] }
}

///|
/// Log-softmax along the last axis: `out[i, j] = x[i, j] - max_j(x) - log(sum_k exp(...))`.
/// Numerically stable.
pub fn tensor_log_softmax(x : Tensor) -> Tensor {
  if x.shape.length() != 2 {
    abort("tensor_log_softmax: input must be 2D")
  }
  let m = x.shape[0]
  let n = x.shape[1]
  let out : Array[Float] = Array::make(m * n, 0.0F)
  for i in 0..<m {
    let row_off = i * n
    let mut mx : Float = x.data[row_off]
    for j in 1..<n {
      if x.data[row_off + j] > mx { mx = x.data[row_off + j] }
    }
    // Compute log-sum-exp = max + log(sum exp(x - max)).
    let mut sum : Float = 0.0F
    for j in 0..<n {
      let v = x.data[row_off + j] - mx
      sum = sum + math_exp_f32(v)
    }
    let lse = mx + math_log_f32(sum)
    for j in 0..<n {
      out[row_off + j] = x.data[row_off + j] - lse
    }
  }
  { data: out, shape: [m, n] }
}

///|
/// Float32 exp via libm FFI (matches Julia's `exp(Float32, x)`).
pub fn math_exp_f32(x : Float) -> Float {
  expf(x)
}

///|
/// Float32 log via libm FFI (matches Julia's `log(Float32, x)`).
pub fn math_log_f32(x : Float) -> Float {
  logf(x)
}