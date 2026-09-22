// optimizer_sgd.mbt — SGD + SGD-with-momentum (v0.15.0).
//
// All optimisers operate on raw `(Array[Float], Array[Float])` tuples for
// (weight, bias). Layer-specific wrappers (`sgd_update_linear` /
// `sgd_update_conv`) delegate to the array-level helpers, copying the
// resulting arrays into a fresh parameter struct (params are immutable).
//
// Conventions:
//   - `weight[i] -= lr * d_weight[i]` (vanilla SGD)
//   - SGD-with-momentum (Polyak / Sutskever):
//       v <- momentum * v + d_weight
//       weight <- weight - lr * v
//     Equivalently `v <- momentum * v + d_weight` then `weight -= lr * v`,
//     which is the PyTorch convention (NOT the original Sutskever-2013
//     `weight -= lr * (momentum * v + grad)` form).
//   - Bias updates use the same rule, kept independent from weight
//     velocity (no shared buffer).
//   - All state is Float32; we follow the project's bit-exact contract.

///|
/// Vanilla SGD: returns a fresh `(weight, bias)` pair after applying
/// `weight -= lr * d_weight` and `bias -= lr * d_bias` element-wise.
pub fn sgd_update_arrays(
  weight : Array[Float],
  bias : Array[Float],
  d_weight : Array[Float],
  d_bias : Array[Float],
  lr : Float,
) -> (Array[Float], Array[Float]) {
  let w2 : Array[Float] = Array::make(weight.length(), 0.0F)
  for i in 0..<weight.length() {
    w2[i] = weight[i] - lr * d_weight[i]
  }
  let b2 : Array[Float] = Array::make(bias.length(), 0.0F)
  for i in 0..<bias.length() {
    b2[i] = bias[i] - lr * d_bias[i]
  }
  (w2, b2)
}

///|
/// SGD-with-momentum state. `v_w` and `v_b` are velocity buffers
/// matching the shape of `weight` and `bias` respectively.
pub struct SGDMomentumState {
  v_w : Array[Float]
  v_b : Array[Float]
}

///|
/// Allocate zero-initialised momentum state for a parameter of given
/// `weight_len` and `bias_len`.
pub fn sgd_momentum_init(weight_len : Int, bias_len : Int) -> SGDMomentumState {
  { v_w: Array::make(weight_len, 0.0F), v_b: Array::make(bias_len, 0.0F) }
}

///|
/// One SGD-with-momentum step. Mutates and returns the velocity state
/// alongside the updated `(weight, bias)` arrays.
///
///   v_w <- momentum * v_w + d_weight
///   v_b <- momentum * v_b + d_bias
///   weight <- weight - lr * v_w
///   bias   <- bias   - lr * v_b
pub fn sgd_momentum_update_arrays(
  weight : Array[Float],
  bias : Array[Float],
  d_weight : Array[Float],
  d_bias : Array[Float],
  state : SGDMomentumState,
  lr : Float,
  momentum : Float,
) -> (Array[Float], Array[Float], SGDMomentumState) {
  // Update velocity.
  let new_v_w : Array[Float] = Array::make(weight.length(), 0.0F)
  for i in 0..<weight.length() {
    new_v_w[i] = momentum * state.v_w[i] + d_weight[i]
  }
  let new_v_b : Array[Float] = Array::make(bias.length(), 0.0F)
  for i in 0..<bias.length() {
    new_v_b[i] = momentum * state.v_b[i] + d_bias[i]
  }
  // Apply step.
  let w2 : Array[Float] = Array::make(weight.length(), 0.0F)
  for i in 0..<weight.length() {
    w2[i] = weight[i] - lr * new_v_w[i]
  }
  let b2 : Array[Float] = Array::make(bias.length(), 0.0F)
  for i in 0..<bias.length() {
    b2[i] = bias[i] - lr * new_v_b[i]
  }
  (w2, b2, { v_w: new_v_w, v_b: new_v_b })
}

///|
/// SGD wrapper for `LinearParam`. Returns a fresh `LinearParam` with
/// updated weights / biases.
pub fn sgd_update_linear(
  param : LinearParam,
  d_weight : Array[Float],
  d_bias : Array[Float],
  lr : Float,
) -> LinearParam {
  let (w, b) = sgd_update_arrays(param.weight, param.bias, d_weight, d_bias, lr)
  { weight: w, bias: b, in_features: param.in_features, out_features: param.out_features }
}

///|
/// SGD wrapper for `Conv2dParam`. Returns a fresh `Conv2dParam` with
/// updated weights / biases.
pub fn sgd_update_conv(
  param : Conv2dParam,
  d_weight : Array[Float],
  d_bias : Array[Float],
  lr : Float,
) -> Conv2dParam {
  let (w, b) = sgd_update_arrays(param.weight, param.bias, d_weight, d_bias, lr)
  { weight: w, bias: b, c_out: param.c_out, c_in: param.c_in,
    kh: param.kh, kw: param.kw, stride: param.stride, pad: param.pad }
}

///|
/// SGD-with-momentum wrapper for `LinearParam`.
pub fn sgd_momentum_update_linear(
  param : LinearParam,
  d_weight : Array[Float],
  d_bias : Array[Float],
  state : SGDMomentumState,
  lr : Float,
  momentum : Float,
) -> (LinearParam, SGDMomentumState) {
  let (w, b, s2) = sgd_momentum_update_arrays(
    param.weight, param.bias, d_weight, d_bias, state, lr, momentum,
  )
  let p2 = { weight: w, bias: b, in_features: param.in_features,
             out_features: param.out_features }
  (p2, s2)
}

///|
/// SGD-with-momentum wrapper for `Conv2dParam`.
pub fn sgd_momentum_update_conv(
  param : Conv2dParam,
  d_weight : Array[Float],
  d_bias : Array[Float],
  state : SGDMomentumState,
  lr : Float,
  momentum : Float,
) -> (Conv2dParam, SGDMomentumState) {
  let (w, b, s2) = sgd_momentum_update_arrays(
    param.weight, param.bias, d_weight, d_bias, state, lr, momentum,
  )
  let p2 = { weight: w, bias: b, c_out: param.c_out, c_in: param.c_in,
             kh: param.kh, kw: param.kw, stride: param.stride, pad: param.pad }
  (p2, s2)
}