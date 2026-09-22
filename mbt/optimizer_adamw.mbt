// optimizer_adamw.mbt — AdamW optimiser (v0.16.0).
//
// Reference: Loshchilov & Hutter, "Decoupled Weight Decay Regularization",
// ICLR 2019 (arXiv:1711.05101).
//
// AdamW differs from Adam by decoupling the weight-decay term from the
// gradient-based update. Adam conflates L2 regularisation with the
// gradient (adding `lambda * param` to `g` before the m/v accumulation),
// which interacts poorly with the adaptive moments. AdamW instead adds
// the decay directly to the parameter update:
//
//   m <- beta1 * m + (1 - beta1) * g
//   v <- beta2 * v + (1 - beta2) * g^2
//   m_hat <- m / (1 - beta1^t)
//   v_hat <- v / (1 - beta2^t)
//   param <- param - lr * ( m_hat / (sqrt(v_hat) + eps)
//                          + weight_decay * param )
//
// State shape matches `AdamState`; step is incremented inside the
// update (caller threads the returned state forward, as with Adam).
// `weight_decay` is a Float32 hyperparameter; setting it to 0 makes
// AdamW behaviourally equivalent to Adam (modulo the bias-correction
// convention — both implementations match the PyTorch default).

///|
/// AdamW state. Identical shape to `AdamState`; `step` is 1-based and
/// tracked implicitly via the returned state (not mutated in place).
pub struct AdamWState {
  m_w : Array[Float]
  v_w : Array[Float]
  m_b : Array[Float]
  v_b : Array[Float]
  step : Int
}

///|
/// Allocate zero-initialised AdamW state for a parameter of given
/// `weight_len` / `bias_len`. `step` starts at 0 (bias correction
/// uses `t = state.step + 1`).
pub fn adamw_init(weight_len : Int, bias_len : Int) -> AdamWState {
  {
    m_w: Array::make(weight_len, 0.0F),
    v_w: Array::make(weight_len, 0.0F),
    m_b: Array::make(bias_len, 0.0F),
    v_b: Array::make(bias_len, 0.0F),
    step: 0,
  }
}

///|
/// One AdamW update at raw-array level. Returns the updated
/// `(weight, bias)` arrays plus the new state with `step` incremented.
/// `weight_decay` is applied additively to the parameter update
/// (`param <- param - lr*(...) + lr*weight_decay*param`), matching the
/// PyTorch `optim.AdamW` convention.
pub fn adamw_update_arrays(
  weight : Array[Float],
  bias : Array[Float],
  d_weight : Array[Float],
  d_bias : Array[Float],
  state : AdamWState,
  lr : Float,
  beta1 : Float,
  beta2 : Float,
  eps : Float,
  weight_decay : Float,
) -> (Array[Float], Array[Float], AdamWState) {
  let t = state.step + 1
  let bc1 = 1.0F - pow_beta(beta1, t)
  let bc2 = 1.0F - pow_beta(beta2, t)
  let w2 : Array[Float] = Array::make(weight.length(), 0.0F)
  let new_m_w : Array[Float] = Array::make(weight.length(), 0.0F)
  let new_v_w : Array[Float] = Array::make(weight.length(), 0.0F)
  for i in 0..<weight.length() {
    let g = d_weight[i]
    new_m_w[i] = beta1 * state.m_w[i] + (1.0F - beta1) * g
    new_v_w[i] = beta2 * state.v_w[i] + (1.0F - beta2) * g * g
    let m_hat = new_m_w[i] / bc1
    let v_hat = new_v_w[i] / bc2
    // Decoupled weight decay: applied additively to the *update*, not
    // to the gradient (this is what distinguishes AdamW from Adam+L2).
    let update = m_hat / (sqrtf(v_hat) + eps) + weight_decay * weight[i]
    w2[i] = weight[i] - lr * update
  }
  let b2 : Array[Float] = Array::make(bias.length(), 0.0F)
  let new_m_b : Array[Float] = Array::make(bias.length(), 0.0F)
  let new_v_b : Array[Float] = Array::make(bias.length(), 0.0F)
  for i in 0..<bias.length() {
    let g = d_bias[i]
    new_m_b[i] = beta1 * state.m_b[i] + (1.0F - beta1) * g
    new_v_b[i] = beta2 * state.v_b[i] + (1.0F - beta2) * g * g
    let m_hat = new_m_b[i] / bc1
    let v_hat = new_v_b[i] / bc2
    let update = m_hat / (sqrtf(v_hat) + eps) + weight_decay * bias[i]
    b2[i] = bias[i] - lr * update
  }
  (
    w2,
    b2,
    { m_w: new_m_w, v_w: new_v_w, m_b: new_m_b, v_b: new_v_b, step: t },
  )
}

///|
/// AdamW wrapper for `LinearParam`.
pub fn adamw_update_linear(
  param : LinearParam,
  d_weight : Array[Float],
  d_bias : Array[Float],
  state : AdamWState,
  lr : Float,
  beta1 : Float,
  beta2 : Float,
  eps : Float,
  weight_decay : Float,
) -> (LinearParam, AdamWState) {
  let (w, b, s2) = adamw_update_arrays(
    param.weight, param.bias, d_weight, d_bias, state,
    lr, beta1, beta2, eps, weight_decay,
  )
  let p2 = { weight: w, bias: b, in_features: param.in_features,
             out_features: param.out_features }
  (p2, s2)
}

///|
/// AdamW wrapper for `Conv2dParam`.
pub fn adamw_update_conv(
  param : Conv2dParam,
  d_weight : Array[Float],
  d_bias : Array[Float],
  state : AdamWState,
  lr : Float,
  beta1 : Float,
  beta2 : Float,
  eps : Float,
  weight_decay : Float,
) -> (Conv2dParam, AdamWState) {
  let (w, b, s2) = adamw_update_arrays(
    param.weight, param.bias, d_weight, d_bias, state,
    lr, beta1, beta2, eps, weight_decay,
  )
  let p2 = { weight: w, bias: b, c_out: param.c_out, c_in: param.c_in,
             kh: param.kh, kw: param.kw, stride: param.stride, pad: param.pad }
  (p2, s2)
}