// optimizer_adagrad.mbt — AdaGrad optimiser (v0.16.2).
//
// Reference: Duchi, Hazan, Singer, "Adaptive Subgradient Methods for
// Online Learning and Stochastic Optimization", JMLR 2011.
//
// AdaGrad is the simplest adaptive optimiser: it accumulates the sum
// of squared gradients into `v` (no exponential moving average, no
// first moment) and divides the gradient by `sqrt(v) + eps`:
//
//   v <- v + g^2
//   param <- param - lr * g / (sqrt(v) + eps)
//
// Because `v` only ever grows, the effective per-parameter learning
// rate monotonically shrinks over time. This is great for sparse
// features (rare parameters get bigger steps early) but can kill
// learning too aggressively on dense problems — RMSprop (v0.16.1)
// addresses this by replacing the cumulative sum with an EMA.
//
// Conventions match the project's bit-exact Float32 contract.

///|
/// AdaGrad state. `v_w` / `v_b` accumulate squared gradients.
pub struct AdaGradState {
  v_w : Array[Float]
  v_b : Array[Float]
}

///|
/// Allocate zero-initialised AdaGrad state.
pub fn adagrad_init(weight_len : Int, bias_len : Int) -> AdaGradState {
  { v_w: Array::make(weight_len, 0.0F), v_b: Array::make(bias_len, 0.0F) }
}

///|
/// One AdaGrad update at raw-array level.
pub fn adagrad_update_arrays(
  weight : Array[Float],
  bias : Array[Float],
  d_weight : Array[Float],
  d_bias : Array[Float],
  state : AdaGradState,
  lr : Float,
  eps : Float,
) -> (Array[Float], Array[Float], AdaGradState) {
  let w2 : Array[Float] = Array::make(weight.length(), 0.0F)
  let new_v_w : Array[Float] = Array::make(weight.length(), 0.0F)
  for i in 0..<weight.length() {
    let g = d_weight[i]
    new_v_w[i] = state.v_w[i] + g * g
    w2[i] = weight[i] - lr * g / (sqrtf(new_v_w[i]) + eps)
  }
  let b2 : Array[Float] = Array::make(bias.length(), 0.0F)
  let new_v_b : Array[Float] = Array::make(bias.length(), 0.0F)
  for i in 0..<bias.length() {
    let g = d_bias[i]
    new_v_b[i] = state.v_b[i] + g * g
    b2[i] = bias[i] - lr * g / (sqrtf(new_v_b[i]) + eps)
  }
  (w2, b2, { v_w: new_v_w, v_b: new_v_b })
}

///|
/// AdaGrad wrapper for `LinearParam`.
pub fn adagrad_update_linear(
  param : LinearParam,
  d_weight : Array[Float],
  d_bias : Array[Float],
  state : AdaGradState,
  lr : Float,
  eps : Float,
) -> (LinearParam, AdaGradState) {
  let (w, b, s2) = adagrad_update_arrays(
    param.weight, param.bias, d_weight, d_bias, state, lr, eps,
  )
  let p2 = { weight: w, bias: b, in_features: param.in_features,
             out_features: param.out_features }
  (p2, s2)
}

///|
/// AdaGrad wrapper for `Conv2dParam`.
pub fn adagrad_update_conv(
  param : Conv2dParam,
  d_weight : Array[Float],
  d_bias : Array[Float],
  state : AdaGradState,
  lr : Float,
  eps : Float,
) -> (Conv2dParam, AdaGradState) {
  let (w, b, s2) = adagrad_update_arrays(
    param.weight, param.bias, d_weight, d_bias, state, lr, eps,
  )
  let p2 = { weight: w, bias: b, c_out: param.c_out, c_in: param.c_in,
             kh: param.kh, kw: param.kw, stride: param.stride, pad: param.pad }
  (p2, s2)
}