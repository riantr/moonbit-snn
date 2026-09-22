// relu_backward.mbt 鈥?ReLU backward pass.
//
// ReLU forward:  y[i] = max(input[i], 0)
// ReLU backward: d_input[i] = d_output[i] if input[i] > 0 else 0
//
// `input` is the original pre-activation (forward input), used to
// determine which neurons were active. `d_output` is the upstream
// gradient. Returns a fresh array `d_input` (no aliasing).

///|
/// ReLU backward pass.
///
/// - `input`     : forward input (length n)
/// - `d_output`  : upstream gradient (length n)
/// returns        : `d_input` (length n), new array
pub fn relu_backward(input : Array[Float], d_output : Array[Float]) -> Array[Float] {
  let n = input.length()
  let d_input : Array[Float] = Array::make(n, 0.0F)
  for i in 0..<n {
    if input[i] > 0.0F {
      d_input[i] = d_output[i]
    }
    // else: 0 (already initialised)
  }
  d_input
}