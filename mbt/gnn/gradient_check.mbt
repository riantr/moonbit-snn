// gradient_check.mbt 鈥?numerical gradient check helper.
//
// Compares an analytical gradient against the central-difference
// numerical gradient:
//   d_numerical[i] = (f(x + eps*e_i) - f(x - eps*e_i)) / (2*eps)
//
// Used by backward-pass tests to verify correctness.

///|
/// Compute the central-difference numerical gradient of a scalar loss
/// `loss(input)` (computed by `forward`) along each coordinate of `input`.
///
/// `forward` is a closure that takes an input array and returns a
/// scalar (sum-of-outputs loss).
///
/// Returns a fresh array of the same length as `input`.
pub fn numerical_gradient(
  input : Array[Float],
  eps : Float,
  forward : (Array[Float]) -> Float,
) -> Array[Float] {
  let n = input.length()
  let grad : Array[Float] = Array::make(n, 0.0F)
  for i in 0..<n {
    let x_orig = input[i]
    // f(x + eps)
    input[i] = x_orig + eps
    let f_plus = forward(input)
    // f(x - eps)
    input[i] = x_orig - eps
    let f_minus = forward(input)
    // restore
    input[i] = x_orig
    grad[i] = (f_plus - f_minus) / (2.0F * eps)
  }
  grad
}

///|
/// Compare two gradients. Returns the max absolute difference and the
/// index of the worst component. Useful for asserting tolerance.
pub fn max_abs_diff(a : Array[Float], b : Array[Float]) -> (Float, Int) {
  let n = if a.length() < b.length() { a.length() } else { b.length() }
  let mut max_diff = 0.0F
  let mut max_idx = 0
  let mut i = 0
  while i < n {
    let d = (a[i] - b[i]).abs()
    if d > max_diff {
      max_diff = d
      max_idx = i
    }
    i = i + 1
  }
  (max_diff, max_idx)
}