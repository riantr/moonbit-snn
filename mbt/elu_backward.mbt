// elu_backward.mbt -- ELU backward pass.
//
// ELU(x) = x                   if x > 0
//        = alpha*(e^x - 1)     otherwise
//
// ELU'(x) = 1              if x > 0
//         = alpha*e^x      otherwise
//
// `input` is the forward's PRE-activation (elu_forward's argument), not its
// output: at x = 0 the two branches coincide, but reading the post-activation
// would make the mask depend on a value the caller may already have
// transformed. Returns a fresh array; `d_output` is left alone.

///|
/// ELU backward pass.
pub fn elu_backward(
  input : Array[Float],
  d_output : Array[Float],
  alpha~ : Float = 1.0F,
) -> Array[Float] {
  let n = input.length()
  let d_input : Array[Float] = Array::make(n, 0.0F)
  for i in 0..<n {
    let v = input[i]
    let g = if v > 0.0F { 1.0F } else { alpha * expf(v) }
    d_input[i] = d_output[i] * g
  }
  d_input
}