// optimizer_adam.mbt — Adam optimiser (v0.15.1).
//
// Reference: Kingma & Ba, "Adam: A Method for Stochastic Optimization",
// ICLR 2015 (arXiv:1412.6980).
//
// Standard update rule (with bias correction):
//
//   m <- beta1 * m + (1 - beta1) * g
//   v <- beta2 * v + (1 - beta2) * g^2
//   m_hat <- m / (1 - beta1^t)
//   v_hat <- v / (1 - beta2^t)
//   param <- param - lr * m_hat / (sqrt(v_hat) + eps)
//
// State `step` is a 1-based step counter (incremented before the update,
// so the first step uses `t = 1`). Default hyper-parameters:
//   - beta1 = 0.9
//   - beta2 = 0.999
//   - eps   = 1e-8
//   - lr    = 1e-3
//
// Conventions match the project's bit-exact Float32 contract.

///|
/// Adam state. `m_w / v_w` track first / second moments of `d_weight`;
/// `m_b / v_b` do the same for `d_bias`. `step` is 1-based and is
/// incremented implicitly inside `adam_update_arrays` — the caller must
/// pass the state returned from the previous call into the next call.
/// (We do NOT mutate the input state in-place because MoonBit struct
/// fields share memory across function boundaries, which would corrupt
/// any previously-returned state object.)
pub struct AdamState {
  m_w : Array[Float]
  v_w : Array[Float]
  m_b : Array[Float]
  v_b : Array[Float]
  step : Int
}

///|
/// Allocate zero-initialised Adam state for a parameter of given
/// `weight_len` / `bias_len`. Initial `step = 0` (no bias correction yet).
pub fn adam_init(weight_len : Int, bias_len : Int) -> AdamState {
  {
    m_w: Array::make(weight_len, 0.0F),
    v_w: Array::make(weight_len, 0.0F),
    m_b: Array::make(bias_len, 0.0F),
    v_b: Array::make(bias_len, 0.0F),
    step: 0,
  }
}

///|
/// Pure accessor: returns `state.step + 1`. Used internally by
/// `adam_update_arrays` and exposed for callers that want to inspect
/// the next step counter without mutating the state.
pub fn adam_next_step(state : AdamState) -> Int {
  state.step + 1
}

///|
/// One Adam update step at raw-array level. Returns the updated
/// `(weight, bias)` arrays plus a new `AdamState` with the incremented
/// step counter. The input `state` is read-only; pass the returned
/// state into the next call.
pub fn adam_update_arrays(
  weight : Array[Float],
  bias : Array[Float],
  d_weight : Array[Float],
  d_bias : Array[Float],
  state : AdamState,
  lr : Float,
  beta1 : Float,
  beta2 : Float,
  eps : Float,
) -> (Array[Float], Array[Float], AdamState) {
  let t = state.step + 1
  // Bias-correction denominators.
  let bc1 = 1.0F - pow_beta(beta1, t)
  let bc2 = 1.0F - pow_beta(beta2, t)
  // Update weight moments + apply.
  let w2 : Array[Float] = Array::make(weight.length(), 0.0F)
  let new_m_w : Array[Float] = Array::make(weight.length(), 0.0F)
  let new_v_w : Array[Float] = Array::make(weight.length(), 0.0F)
  for i in 0..<weight.length() {
    let g = d_weight[i]
    new_m_w[i] = beta1 * state.m_w[i] + (1.0F - beta1) * g
    new_v_w[i] = beta2 * state.v_w[i] + (1.0F - beta2) * g * g
    let m_hat = new_m_w[i] / bc1
    let v_hat = new_v_w[i] / bc2
    w2[i] = weight[i] - lr * m_hat / (sqrt_f32(v_hat) + eps)
  }
  // Update bias moments + apply.
  let b2 : Array[Float] = Array::make(bias.length(), 0.0F)
  let new_m_b : Array[Float] = Array::make(bias.length(), 0.0F)
  let new_v_b : Array[Float] = Array::make(bias.length(), 0.0F)
  for i in 0..<bias.length() {
    let g = d_bias[i]
    new_m_b[i] = beta1 * state.m_b[i] + (1.0F - beta1) * g
    new_v_b[i] = beta2 * state.v_b[i] + (1.0F - beta2) * g * g
    let m_hat = new_m_b[i] / bc1
    let v_hat = new_v_b[i] / bc2
    b2[i] = bias[i] - lr * m_hat / (sqrt_f32(v_hat) + eps)
  }
  (
    w2,
    b2,
    { m_w: new_m_w, v_w: new_v_w, m_b: new_m_b, v_b: new_v_b, step: t },
  )
}

///|
/// Adam wrapper for `LinearParam`.
pub fn adam_update_linear(
  param : LinearParam,
  d_weight : Array[Float],
  d_bias : Array[Float],
  state : AdamState,
  lr : Float,
  beta1 : Float,
  beta2 : Float,
  eps : Float,
) -> (LinearParam, AdamState) {
  let (w, b, s2) = adam_update_arrays(
    param.weight, param.bias, d_weight, d_bias, state, lr, beta1, beta2, eps,
  )
  let p2 = { weight: w, bias: b, in_features: param.in_features,
             out_features: param.out_features }
  (p2, s2)
}

///|
/// Adam wrapper for `Conv2dParam`.
pub fn adam_update_conv(
  param : Conv2dParam,
  d_weight : Array[Float],
  d_bias : Array[Float],
  state : AdamState,
  lr : Float,
  beta1 : Float,
  beta2 : Float,
  eps : Float,
) -> (Conv2dParam, AdamState) {
  let (w, b, s2) = adam_update_arrays(
    param.weight, param.bias, d_weight, d_bias, state, lr, beta1, beta2, eps,
  )
  let p2 = { weight: w, bias: b, c_out: param.c_out, c_in: param.c_in,
             kh: param.kh, kw: param.kw, stride: param.stride, pad: param.pad }
  (p2, s2)
}

// ---------------------------------------------------------------------------
// Float32 helpers: pow(beta, t) and sqrt.
// Implemented as plain Float32 loops + libm FFI (no `Float::pow` builtin
// in moonbitlang/core/math).
// ---------------------------------------------------------------------------

///|
/// Compute `beta^t` for Float32 `beta` and Int `t >= 0` via
/// exponentiation by squaring. Returns 1.0 when `t == 0`.
fn pow_beta(beta : Float, t : Int) -> Float {
  let mut result = 1.0F
  let mut b = beta
  let mut n = t
  while n > 0 {
    if (n & 1) == 1 {
      result = result * b
    }
    b = b * b
    n = n >> 1
  }
  result
}

///|
/// Float32 sqrt via libm. We already have `math_sqrt_f32` exposed in
// `math_native.mbt`; reuse it. If unavailable, fall back to a Newton
// iteration (see below).
pub extern "C" fn sqrtf(x : Float) -> Float = "sqrtf"

///|
fn sqrt_f32(x : Float) -> Float {
  sqrtf(x)
}