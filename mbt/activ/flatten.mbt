// flatten.mbt 鈥?flatten an NCHW tensor to (N, C*H*W).
//
// Because the project uses NCHW row-major flat `Array[Float]`, the
// memory layout is already [n, c, h, w] where index
//   `n * C*H*W + c*H*W + h*W + w`
// can be re-read as index
//   `n * C*H*W + (c*H*W + h*W + w)`
// i.e. (n, C*H*W) row-major. The data does not need to be moved.
// This function exists to make the shape change explicit at the API
// level and to handle the trivial case where the caller wants a fresh
// owned array.

///|
/// Flatten NCHW `[n, c, h, w]` to `(n, c*h*w)`. The output array is a
/// COPY of the input (so the caller can mutate it freely).
pub fn flatten_forward(
  input : Array[Float],
  n : Int,
  c : Int,
  h : Int,
  w : Int,
) -> Array[Float] {
  let n_out = n * c * h * w
  let out : Array[Float] = Array::make(n_out, 0.0F)
  for i in 0..<n_out {
    out[i] = input[i]
  }
  out
}