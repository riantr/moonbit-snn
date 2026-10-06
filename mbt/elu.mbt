// elu.mbt -- ELU activation (exponential linear unit), element-wise.
//
// ELU(x) = x            if x > 0
//        = alpha*(e^x - 1)  otherwise
//
// with alpha = 1 (the Keras default that DragonNet's Dense(activation='elu')
// uses). The derivative lives in `elu_backward.mbt`; note that
// `gat_elu_grad` in `gat_backward.mbt` is the same function for alpha = 1,
// kept separate because the GAT stack applies it inside its own mask.

///|
/// ELU forward, element-wise: `out[i] = elu(input[i], alpha)`.
/// Returns a fresh array; the input is never mutated.
pub fn elu_forward(input : Array[Float], alpha~ : Float = 1.0F) -> Array[Float] {
  let n = input.length()
  let out : Array[Float] = Array::make(n, 0.0F)
  for i in 0..<n {
    let v = input[i]
    if v > 0.0F {
      out[i] = v
    } else {
      out[i] = alpha * (expf(v) - 1.0F)
    }
  }
  out
}