// gelu.mbt — Gaussian Error Linear Unit activation (transformer FFN).
//
// GELU is the standard activation in BERT, GPT, ViT and most modern
// Transformer architectures. Two forms exist:
//
//   Exact       : GELU(x) = x · Φ(x) = 0.5·x·(1 + erf(x/√2))
//   Approximate : GELU(x) ≈ 0.5·x·(1 + tanh(√(2/π)·(x + 0.044715·x³)))
//
// We use the tanh-approximation because:
//   - Matches PyTorch's `F.gelu(approximate='tanh')` (the default in
//     most transformer code).
//   - Uses `tanhf` from libm (already FFI'd in `math_native.mbt`),
//     no need for `erff`.
//   - Cheaper than `erf` and adequate for Float32 precision.
//
// Forward  : y[i] = 0.5·x[i]·(1 + tanhf(inner[i]))
//   inner[i] = √(2/π) · (x[i] + 0.044715 · x[i]³)
// Backward : d_input[i] = d_output[i] · dgelu/dx(x[i])
//   dgelu/dx = 0.5·(1 + tanh(inner))
//            + 0.5·x·sech²(inner)·√(2/π)·(1 + 3·0.044715·x²)

///|
/// GELU scalar (tanh approximation).
///
/// `gelu(0) = 0` exactly. `gelu(x)` ≈ `x` for large positive `x`,
/// `gelu(x)` ≈ 0 for large negative `x`.
pub fn gelu(x : Float) -> Float {
  let x2 = x * x
  let x3 = x2 * x
  // √(2/π) ≈ 0.7978845608028654
  let inner = 0.7978845608028654F * (x + 0.044715F * x3)
  let t = tanhf(inner)
  0.5F * x * (1.0F + t)
}

///|
/// GELU element-wise forward. Returns a new array (does not mutate).
pub fn gelu_forward(input : Array[Float]) -> Array[Float] {
  let n = input.length()
  let out : Array[Float] = Array::make(n, 0.0F)
  for i in 0..<n {
    out[i] = gelu(input[i])
  }
  out
}