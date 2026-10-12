// linear.mbt 鈥?linear (fully-connected / dense) layer forward.
//
// y[n, out] = sum_in weight[out, in] * x[n, in] + bias[out]
//
// Layout: row-major flat `Array[Float]`.
//
//   Input:  [n, in_features]            length = n * in_features
//   Weight: [out_features, in_features] length = out_features * in_features
//   Bias:   [out_features]              length = out_features
//   Output: [n, out_features]           length = n * out_features

///|
/// Linear (Dense) parameter container. `weight` is laid out as
/// `[out_features, in_features]` row-major; `bias` is `[out_features]`.
pub struct LinearParam {
  weight : Array[Float]
  bias : Array[Float]
  in_features : Int
  out_features : Int
}

///|
/// Build a LinearParam from raw arrays. Does NOT copy the arrays.
pub fn LinearParam::new(
  weight : Array[Float],
  bias : Array[Float],
  in_features : Int,
  out_features : Int,
) -> LinearParam {
  { weight, bias, in_features, out_features }
}

///|
/// Forward pass for a linear (dense) layer.
///
/// `input` : length = n * in_features
/// returns : length = n * out_features
pub fn linear_forward(
  input : Array[Float],
  n : Int,
  param : LinearParam,
) -> Array[Float] {
  let in_f = param.in_features
  let out_f = param.out_features
  let out : Array[Float] = Array::make(n * out_f, 0.0F)
  for batch in 0..<n {
    let in_batch_off = batch * in_f
    let out_batch_off = batch * out_f
    for o in 0..<out_f {
      let w_off = o * in_f
      let mut acc : Float = param.bias[o]
      let mut i = 0
      while i < in_f {
        acc = acc + param.weight[w_off + i] * input[in_batch_off + i]
        i = i + 1
      }
      out[out_batch_off + o] = acc
    }
  }
  out
}