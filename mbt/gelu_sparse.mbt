// gelu_sparse.mbt — Exact GELU activation via `x · Φ(x)` (a.k.a.
// "sparse" GELU in PyTorch).
//
// The exact form uses the cumulative normal distribution:
//
//   GELU(x) = x · Φ(x)
//          = 0.5 · x · (1 + erf(x / √2))
//
// Compare to the tanh-approximation in `gelu.mbt`, which uses
// `tanhf` to avoid the `erff` call:
//
//   GELU_approx(x) ≈ 0.5 · x · (1 + tanh(√(2/π) · (x + 0.044715·x³)))
//
// Both forms are within ~1e-3 of each other on |x| ≤ 5, but the
// exact form is closer to the true mathematical definition and is
// what `F.gelu(approximate='none')` computes in PyTorch.
//
// Forward  : y(x) = 0.5 · x · (1 + erff(x · √(1/2)))
// Backward : dgelu/dx = Φ(x) + x · φ(x)
//          = Φ(x) + x · (1/√(2π)) · expf(-x²/2)

///|
/// Exact GELU scalar via `x · Φ(x)`.
pub fn gelu_sparse(x : Float) -> Float {
  // √(1/2) ≈ 0.7071067811865475
  let c = 0.7071067811865475F
  0.5F * x * (1.0F + erff(x * c))
}

///|
/// Exact GELU element-wise forward. Returns a new array (no mutation).
pub fn gelu_sparse_forward(input : Array[Float]) -> Array[Float] {
  let n = input.length()
  let out : Array[Float] = Array::make(n, 0.0F)
  for i in 0..<n {
    out[i] = gelu_sparse(input[i])
  }
  out
}

///|
/// Exact GELU scalar derivative.
///
/// dgelu/dx = Φ(x) + x · φ(x)
///         = 0.5·(1 + erff(x/√2)) + x · (1/√(2π)) · expf(-x²/2)
pub fn gelu_sparse_grad(x : Float) -> Float {
  let c = 0.7071067811865475F
  let phi_x = 0.5F * (1.0F + erff(x * c))
  let one_over_sqrt_2pi = 0.3989422804014327F
  let pdf_x = one_over_sqrt_2pi * expf(-0.5F * x * x)
  phi_x + x * pdf_x
}

///|
/// Exact GELU backward pass.
pub fn gelu_sparse_backward(
  input : Array[Float],
  d_output : Array[Float],
) -> Array[Float] {
  let n = input.length()
  let d_input : Array[Float] = Array::make(n, 0.0F)
  for i in 0..<n {
    d_input[i] = d_output[i] * gelu_sparse_grad(input[i])
  }
  d_input
}