// gelu_backward.mbt — GELU backward pass.
//
// GELU forward (tanh approximation):
//   inner  = √(2/π) · (x + 0.044715 · x³)
//   t      = tanhf(inner)
//   y      = 0.5 · x · (1 + t)
//
// dgelu/dx = ∂y/∂x
//          = 0.5·(1 + t)                                  [product rule, 2nd term]
//          + 0.5·x · sech²(inner) · d(inner)/dx         [chain rule]
//          = 0.5·(1 + t)
//          + 0.5·x · (1 - t²) · √(2/π) · (1 + 3·0.044715·x²)

///|
/// GELU scalar derivative dgelu/dx (tanh approximation).
pub fn gelu_grad(x : Float) -> Float {
  let x2 = x * x
  let x3 = x2 * x
  let inner = 0.7978845608028654F * (x + 0.044715F * x3)
  let t = tanhf(inner)
  let sech2 = 1.0F - t * t
  let d_inner_dx = 0.7978845608028654F * (1.0F + 3.0F * 0.044715F * x2)
  0.5F * (1.0F + t) + 0.5F * x * sech2 * d_inner_dx
}

///|
/// GELU backward pass. `input` is the original forward input;
/// `d_output` is the upstream gradient. Returns `d_input` (new array).
pub fn gelu_backward(
  input : Array[Float],
  d_output : Array[Float],
) -> Array[Float] {
  let n = input.length()
  let d_input : Array[Float] = Array::make(n, 0.0F)
  for i in 0..<n {
    d_input[i] = d_output[i] * gelu_grad(input[i])
  }
  d_input
}