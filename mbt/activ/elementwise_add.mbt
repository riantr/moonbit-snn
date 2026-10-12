// elementwise_add.mbt — Elementwise Add primitive (v0.20.0).
//
// Used as the core of ResNet's residual block: `out = F(x) + x` (or
// `out = F(x) + W_s(x)` where `W_s` is a projection shortcut).
//
// Conventions:
//   - `a` and `b` must have identical shape.
//   - `add_forward` allocates a fresh array (no aliasing).
//   - `add_backward` returns two INDEPENDENT copies of the upstream
//     gradient — the caller is responsible for adding them to any
//     pre-existing gradient buffers (e.g. d_skip).

///|
/// Elementwise forward: `out[i] = a[i] + b[i]`. Both inputs must
/// have the same length.
pub fn add_forward(a : Array[Float], b : Array[Float]) -> Array[Float] {
  let n = a.length()
  let out : Array[Float] = Array::make(n, 0.0F)
  for i in 0..<n {
    out[i] = a[i] + b[i]
  }
  out
}

///|
/// Elementwise backward. Returns two INDEPENDENT copies of `d_output`,
/// one for each operand of `add_forward`. The caller adds each to the
/// downstream parameter gradient as needed (or treats them as the
/// final d_input for the respective operand).
pub fn add_backward(d_output : Array[Float]) -> (Array[Float], Array[Float]) {
  let n = d_output.length()
  let d_a : Array[Float] = Array::make(n, 0.0F)
  let d_b : Array[Float] = Array::make(n, 0.0F)
  for i in 0..<n {
    d_a[i] = d_output[i]
    d_b[i] = d_output[i]
  }
  (d_a, d_b)
}