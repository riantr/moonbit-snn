// optimizer_rmsprop.mbt — RMSprop optimiser (v0.16.1).
//
// Reference: G. Hinton, "Neural Networks for Machine Learning" lecture
// 6e (unpublished, 2012). Common modern form:
//
//   v <- alpha * v + (1 - alpha) * g^2
//   param <- param - lr * g / (sqrt(v) + eps)
//
// Unlike Adam, RMSprop has no first moment (`m`) and no bias correction
// — it scales the gradient by an exponential moving average of its
// squared magnitude. With `alpha = 0` it degenerates to plain SGD with
// per-parameter adaptive learning rate; with `alpha -> 1` it approaches
// pure `1/sqrt(g^2 + eps)` scaling (which is essentially AdaDelta's
// root form).
//
// Conventions match the project's bit-exact Float32 contract.

///|
/// RMSprop state. `v_w` / `v_b` are exponential moving averages of the
/// squared gradient for `weight` / `bias`. No step counter — RMSprop
/// has no bias correction term.
pub struct RMSpropState {
  v_w : Array[Float]
  v_b : Array[Float]
}

///|
/// Allocate zero-initialised RMSprop state.
pub fn rmsprop_init(weight_len : Int, bias_len : Int) -> RMSpropState {
  { v_w: Array::make(weight_len, 0.0F), v_b: Array::make(bias_len, 0.0F) }
}

///|
/// One RMSprop update at raw-array level.
pub fn rmsprop_update_arrays(
  weight : Array[Float],
  bias : Array[Float],
  d_weight : Array[Float],
  d_bias : Array[Float],
  state : RMSpropState,
  lr : Float,
  alpha : Float,
  eps : Float,
) -> (Array[Float], Array[Float], RMSpropState) {
  let w2 : Array[Float] = Array::make(weight.length(), 0.0F)
  let new_v_w : Array[Float] = Array::make(weight.length(), 0.0F)
  for i in 0..<weight.length() {
    let g = d_weight[i]
    new_v_w[i] = alpha * state.v_w[i] + (1.0F - alpha) * g * g
    w2[i] = weight[i] - lr * g / (sqrtf(new_v_w[i]) + eps)
  }
  let b2 : Array[Float] = Array::make(bias.length(), 0.0F)
  let new_v_b : Array[Float] = Array::make(bias.length(), 0.0F)
  for i in 0..<bias.length() {
    let g = d_bias[i]
    new_v_b[i] = alpha * state.v_b[i] + (1.0F - alpha) * g * g
    b2[i] = bias[i] - lr * g / (sqrtf(new_v_b[i]) + eps)
  }
  (w2, b2, { v_w: new_v_w, v_b: new_v_b })
}

///|
/// RMSprop wrapper for `LinearParam`.
pub fn rmsprop_update_linear(
  param : LinearParam,
  d_weight : Array[Float],
  d_bias : Array[Float],
  state : RMSpropState,
  lr : Float,
  alpha : Float,
  eps : Float,
) -> (LinearParam, RMSpropState) {
  let (w, b, s2) = rmsprop_update_arrays(
    param.weight, param.bias, d_weight, d_bias, state, lr, alpha, eps,
  )
  let p2 = { weight: w, bias: b, in_features: param.in_features,
             out_features: param.out_features }
  (p2, s2)
}

///|
/// RMSprop wrapper for `Conv2dParam`.
pub fn rmsprop_update_conv(
  param : Conv2dParam,
  d_weight : Array[Float],
  d_bias : Array[Float],
  state : RMSpropState,
  lr : Float,
  alpha : Float,
  eps : Float,
) -> (Conv2dParam, RMSpropState) {
  let (w, b, s2) = rmsprop_update_arrays(
    param.weight, param.bias, d_weight, d_bias, state, lr, alpha, eps,
  )
  let p2 = { weight: w, bias: b, c_out: param.c_out, c_in: param.c_in,
             kh: param.kh, kw: param.kw, stride: param.stride, pad: param.pad }
  (p2, s2)
}