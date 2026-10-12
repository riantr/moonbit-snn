// Surrogate gradients for non-differentiable spike functions.
//
// Bit-exact port of the fast-sigmoid surrogate from
// Zenke & Ganguli 2018 ("The Remarkable Robustness of Surrogate
// Gradient Learning for Spiking Neural Networks"). Reference for the
// analogous SNN.jl training pipeline.
//
// The IF (and LIF) neuron emits a Heaviside step `H(u - Vt)` as the
// forward "spike". The derivative of H is the Dirac delta, which is
// not usable for backprop. We approximate the backward derivative by a
// smoothed surrogate that integrates to ~1 around the threshold.
//
// Fast-sigmoid surrogate:
//   forward (soft)   : s(x)        = x / (1 + β|x|)
//   backward (grad)  : σ'(x)       = 1 / (1 + β|x|)^2
//                      σ'(0)       = 1
//                      σ'(x) → 0   as |x| → ∞
//                      σ'(x)       = σ'(-x)   (symmetric)
//
// All arithmetic is Float32. β is the inverse-slope factor — larger β
// makes the surrogate narrower (more peaky at x=0).

///|
/// Fast-sigmoid surrogate scalar (Zenke & Ganguli 2018, eq. 4).
///
/// σ'(x; β) = 1 / (1 + β·|x|)²
///
/// - σ'(0; β) = 1   (peak)
/// - σ'(x; β) = σ'(-x; β)   (even)
/// - σ'(x; β) → 0 as |x| → ∞
pub fn fast_sigmoid_surrogate(x : Float, beta : Float) -> Float {
  let abs_x = if x < 0.0F { -x } else { x }
  let denom = 1.0F + beta * abs_x
  1.0F / (denom * denom)
}

///|
/// Fast-sigmoid soft forward (Zenke & Ganguli 2018, eq. 3).
///
/// s(x; β) = x / (1 + β·|x|)
///
/// Range: (-1/β, 1/β). At x=0 returns 0; saturates to ±1/β at infinity.
/// Used as a differentiable proxy for Heaviside when a soft spike
/// signal is needed (e.g. attention, soft-decoding). For the hard
/// (0/1) spike used in the IF neuron, use `heaviside_step` below.
pub fn fast_sigmoid_forward(x : Float, beta : Float) -> Float {
  let abs_x = if x < 0.0F { -x } else { x }
  let denom = 1.0F + beta * abs_x
  x / denom
}

///|
/// Hard Heaviside step at `x` with threshold `vt`.
///
/// Returns 1.0 if x > vt, else 0.0. This is the actual forward spike
/// signal emitted by IF / LIF neurons; the gradient of this function
/// is approximated by `fast_sigmoid_surrogate` during BPTT.
pub fn heaviside_step(x : Float, vt : Float) -> Float {
  if x > vt {
    1.0F
  } else {
    0.0F
  }
}

///|
/// Elementwise fast-sigmoid surrogate applied to `u - vt`.
///
/// Returns a fresh `Array[Float]` of the same length as `u_minus_vt`
/// where element `i` is `1 / (1 + β·|u[i] - vt|)²`.
///
/// This is the standard backward pass for BPTT through an IF neuron:
/// for each neuron, the gradient flowing back through the spike
/// emission is multiplied by σ'(u[i] - vt).
pub fn fast_sigmoid_surrogate_array(
  u_minus_vt : Array[Float],
  beta : Float,
) -> Array[Float] {
  let n = u_minus_vt.length()
  let out : Array[Float] = Array::make(n, 0.0F)
  for i in 0..<n {
    let x = u_minus_vt[i]
    let abs_x = if x < 0.0F { -x } else { x }
    let denom = 1.0F + beta * abs_x
    out[i] = 1.0F / (denom * denom)
  }
  out
}

///|
/// Elementwise fast-sigmoid soft forward applied to `u - vt`.
///
/// Returns a fresh `Array[Float]` of the same length as `u_minus_vt`
/// where element `i` is `(u[i] - vt) / (1 + β·|u[i] - vt|)`.
pub fn fast_sigmoid_forward_array(
  u_minus_vt : Array[Float],
  beta : Float,
) -> Array[Float] {
  let n = u_minus_vt.length()
  let out : Array[Float] = Array::make(n, 0.0F)
  for i in 0..<n {
    let x = u_minus_vt[i]
    let abs_x = if x < 0.0F { -x } else { x }
    let denom = 1.0F + beta * abs_x
    out[i] = x / denom
  }
  out
}

///|
/// Combined hard-spike + surrogate-gradient envelope.
///
/// Given membrane potentials `u` and threshold `vt`, returns:
/// - `spike[i]` = H(u[i] - vt)  (forward 0/1 spike)
/// - `grad[i]`  = σ'(u[i] - vt; β)  (backward surrogate)
///
/// Use this in BPTT loops to avoid recomputing `u - vt` twice per
/// layer. The forward / backward are decoupled: the forward signal is
/// the true Heaviside (matches the IF neuron step exactly), while the
/// backward uses the surrogate for gradient flow.
pub struct SpikeSurrogate {
  spike : Array[Float]
  grad : Array[Float]
}

///|
/// Compute both the hard spike and its surrogate gradient in one pass.
pub fn spike_surrogate(
  u : Array[Float],
  vt : Float,
  beta : Float,
) -> SpikeSurrogate {
  let n = u.length()
  let spike : Array[Float] = Array::make(n, 0.0F)
  let grad : Array[Float] = Array::make(n, 0.0F)
  for i in 0..<n {
    let x = u[i] - vt
    // Hard forward (matches IF step_neuron).
    spike[i] = if x > 0.0F { 1.0F } else { 0.0F }
    // Surrogate backward.
    let abs_x = if x < 0.0F { -x } else { x }
    let denom = 1.0F + beta * abs_x
    grad[i] = 1.0F / (denom * denom)
  }
  { spike, grad }
}