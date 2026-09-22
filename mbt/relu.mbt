// relu.mbt 鈥?ReLU activation (max(x, 0), element-wise).

///|
/// Apply ReLU element-wise: `out[i] = max(input[i], 0)`. Returns a
/// new array (does not mutate the input).
pub fn relu_forward(input : Array[Float]) -> Array[Float] {
  let n = input.length()
  let out : Array[Float] = Array::make(n, 0.0F)
  for i in 0..<n {
    let v = input[i]
    if v > 0.0F {
      out[i] = v
    }
    // else: leave 0
  }
  out
}