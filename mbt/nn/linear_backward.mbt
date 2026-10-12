// linear_backward.mbt 鈥?Linear (dense) backward pass.
//
// Linear forward:  y[n, o] = bias[o] + sum_i weight[o, i] * x[n, i]
//
// Linear backward:
//   d_input[n, i]  = sum_o weight[o, i] * d_output[n, o]
//   d_weight[o, i] = sum_n d_output[n, o] * x[n, i]
//   d_bias[o]      = sum_n d_output[n, o]
//
// All three return fresh arrays.

///|
/// Linear backward pass. Returns `(d_input, d_weight, d_bias)`.
pub fn linear_backward(
  input : Array[Float],
  d_output : Array[Float],
  n : Int,
  param : LinearParam,
) -> (Array[Float], Array[Float], Array[Float]) {
  let in_f = param.in_features
  let out_f = param.out_features
  let d_input : Array[Float] = Array::make(n * in_f, 0.0F)
  let d_weight : Array[Float] = Array::make(out_f * in_f, 0.0F)
  let d_bias : Array[Float] = Array::make(out_f, 0.0F)
  for batch in 0..<n {
    let in_batch_off = batch * in_f
    let out_batch_off = batch * out_f
    for o in 0..<out_f {
      let d_o = d_output[out_batch_off + o]
      // Accumulate d_bias.
      d_bias[o] = d_bias[o] + d_o
      let w_off = o * in_f
      for i in 0..<in_f {
        // d_weight[o, i] += d_output[n, o] * input[n, i]
        let in_v = input[in_batch_off + i]
        d_weight[w_off + i] = d_weight[w_off + i] + d_o * in_v
        // d_input[n, i] += weight[o, i] * d_output[n, o]
        let w_v = param.weight[w_off + i]
        d_input[in_batch_off + i] = d_input[in_batch_off + i] + w_v * d_o
      }
    }
  }
  (d_input, d_weight, d_bias)
}