// flatten_backward.mbt 鈥?flatten backward pass.
//
// Flatten forward: out = copy(input)  (no data movement in NCHW
//                                       row-major layout)
// Flatten backward: d_input = copy(d_output)  (same identity map)

///|
/// Flatten backward pass. `d_output` is the upstream gradient with
/// length `n * c * h * w` (the flattened shape). Returns a fresh
/// `d_input` of the same length.
pub fn flatten_backward(
  d_output : Array[Float],
  n : Int,
  c : Int,
  h : Int,
  w : Int,
) -> Array[Float] {
  let n_total = n * c * h * w
  let d_input : Array[Float] = Array::make(n_total, 0.0F)
  for i in 0..<n_total {
    d_input[i] = d_output[i]
  }
  d_input
}