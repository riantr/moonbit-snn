// noisy_linear.mbt — NoisyLinear layer (v0.43.0).
//
// Fortunato et al. 2018 "Noisy Networks for Exploration" —
// factorised-Gaussian parametric noise added to a Linear layer's
// weights and biases. Used to replace epsilon-greedy exploration
// in DQN (see v0.43.2 NoisyDQN).
//
// Factorised Gaussian noise (per input/output dimension):
//   ε_i ~ N(0, 1)              for i in [0, in_features)
//   ε_j ~ N(0, 1)              for j in [0, out_features)
//   f(x) = sign(x) · sqrt(|x|)  (paper's reparameterisation; CPU-friendly)
//   ε_w[o, i] = f(ε_i)[i] · f(ε_j)[o]    (rank-1 outer product)
//   ε_b[o]   = f(ε_j)[o]
//
// Effective weight:
//   w[o, i] = weight_mu[o, i] + weight_sigma[o, i] · ε_w[o, i]
//   b[o]   = bias_mu[o]       + bias_sigma[o]     · ε_b[o]
//
// Forward: y[n, o] = sum_i w[o, i] · x[n, i] + b[o]
//
// Reuses `next_f32` (Box-Muller via Xoshiro RNG) for ε_i and ε_j.

///|
/// NoisyLinear parameter container. Stores the mean and std of
/// weights/biases; noise is sampled at forward time.
pub struct NoisyLinearParam {
  // μ_weight : [out_features, in_features]
  weight_mu : Array[Float]
  // σ_weight : [out_features, in_features]  (non-negative)
  weight_sigma : Array[Float]
  // μ_bias : [out_features]
  bias_mu : Array[Float]
  // σ_bias : [out_features]  (non-negative)
  bias_sigma : Array[Float]
  in_features : Int
  out_features : Int
}

///|
/// Build a NoisyLinearParam. `mu_*` / `sigma_*` arrays are not copied.
/// All `sigma_*` entries should be ≥ 0 (typical init: 0.017).
pub fn NoisyLinearParam::new(
  weight_mu : Array[Float],
  weight_sigma : Array[Float],
  bias_mu : Array[Float],
  bias_sigma : Array[Float],
  in_features : Int,
  out_features : Int,
) -> NoisyLinearParam {
  {
    weight_mu,
    weight_sigma,
    bias_mu,
    bias_sigma,
    in_features,
    out_features,
  }
}

///|
/// Init μ / σ weights with uniform in [-1/sqrt(in), +1/sqrt(in)]
/// (PyTorch's NoisyLinear default bound). σ weights init to 0.017.
/// bias μ init to 0, σ init to 0.017.
pub fn NoisyLinearParam::init(
  in_features : Int,
  out_features : Int,
  rng : Xoshiro,
) -> NoisyLinearParam {
  let bound : Float = 1.0F /
    Float::from_int(in_features).to_double().sqrt().to_float()
  let w_mu : Array[Float] = Array::make(out_features * in_features, 0.0F)
  let w_sig : Array[Float] = Array::make(out_features * in_features, 0.017F)
  let b_mu : Array[Float] = Array::make(out_features, 0.0F)
  let b_sig : Array[Float] = Array::make(out_features, 0.017F)
  for k in 0..<w_mu.length() {
    // Uniform in [-bound, +bound] via next_f32.
    w_mu[k] = (next_f32(rng) * 2.0F - 1.0F) * bound
  }
  NoisyLinearParam::new(w_mu, w_sig, b_mu, b_sig, in_features, out_features)
}

///|
/// Factorised-noise reparameterisation: f(x) = sign(x) · sqrt(|x|).
/// Reduces CPU cost vs direct Gaussian sampling (paper §3.1).
pub fn f_sign_sqrt(x : Float) -> Float {
  if x >= 0.0F {
    x.to_double().sqrt().to_float()
  } else {
    -(-x).to_double().sqrt().to_float()
  }
}

///|
/// Sample N standard-normals via Box-Muller (consumes 2·N draws).
pub fn sample_gaussians(n : Int, rng : Xoshiro) -> Array[Float] {
  let out : Array[Float] = Array::make(n, 0.0F)
  let mut k = 0
  while k < n {
    let u1 = next_f32(rng)
    let u2 = next_f32(rng)
    // Box-Muller: z0 = sqrt(-2 ln u1) cos(2π u2)
    let safe_u1 = if u1 < 1.0e-7F { 1.0e-7F } else { u1 }
    let r : Float = (-2.0F *
      Float::from_double(@math.ln(safe_u1.to_double()))).to_double().sqrt().to_float()
    let theta = 2.0F * 3.14159265F * u2
    let z0 : Float = r * Float::from_double(@math.cos(theta.to_double()))
    out[k] = z0
    if k + 1 < n {
      // z1 = sqrt(-2 ln u1) sin(2π u2)
      let z1 : Float = r * Float::from_double(@math.sin(theta.to_double()))
      out[k + 1] = z1
    }
    k = k + 2
  }
  out
}

///|
/// Forward pass with sampled noise. `rng` produces two N(0,1) sets
/// (ε_i of size in_features, ε_j of size out_features) per call;
/// the rest is deterministic.
///
/// `input` : length = n * in_features
/// returns: length = n * out_features
pub fn noisy_linear_forward(
  input : Array[Float],
  n : Int,
  param : NoisyLinearParam,
  rng : Xoshiro,
) -> Array[Float] {
  let in_f = param.in_features
  let out_f = param.out_features
  // Sample ε_i (in_f) and ε_j (out_f).
  let eps_i_raw = sample_gaussians(in_f, rng)
  let eps_j_raw = sample_gaussians(out_f, rng)
  // Apply f(x) = sign(x)·sqrt(|x|).
  let eps_i : Array[Float] = Array::make(in_f, 0.0F)
  let eps_j : Array[Float] = Array::make(out_f, 0.0F)
  for k in 0..<in_f {
    eps_i[k] = f_sign_sqrt(eps_i_raw[k])
  }
  for k in 0..<out_f {
    eps_j[k] = f_sign_sqrt(eps_j_raw[k])
  }
  // Effective weight / bias.
  let out : Array[Float] = Array::make(n * out_f, 0.0F)
  for batch in 0..<n {
    let in_off = batch * in_f
    let out_off = batch * out_f
    for o in 0..<out_f {
      // b[o] = bias_mu[o] + bias_sigma[o] · ε_j[o]
      let mut acc : Float = param.bias_mu[o] + param.bias_sigma[o] * eps_j[o]
      for i in 0..<in_f {
        let w_idx = o * in_f + i
        // w[o, i] = weight_mu + weight_sigma · ε_i[i] · ε_j[o]
        let w = param.weight_mu[w_idx] + param.weight_sigma[w_idx] * eps_i[i] * eps_j[o]
        acc = acc + w * input[in_off + i]
      }
      out[out_off + o] = acc
    }
  }
  out
}

///|
/// Deterministic forward (no noise). Used for the "evaluation"
/// pass where exploration should be off — equivalent to a regular
/// Linear with the μ weights. Matches paper §3.3 "greedy action at
/// evaluation time uses only μ parameters."
pub fn noisy_linear_forward_eval(
  input : Array[Float],
  n : Int,
  param : NoisyLinearParam,
) -> Array[Float] {
  let in_f = param.in_features
  let out_f = param.out_features
  let out : Array[Float] = Array::make(n * out_f, 0.0F)
  for batch in 0..<n {
    let in_off = batch * in_f
    let out_off = batch * out_f
    for o in 0..<out_f {
      let mut acc : Float = param.bias_mu[o]
      for i in 0..<in_f {
        let w_idx = o * in_f + i
        acc = acc + param.weight_mu[w_idx] * input[in_off + i]
      }
      out[out_off + o] = acc
    }
  }
  out
}

///|
/// Number of trainable parameters (μ + σ for weight and bias).
pub fn noisy_linear_param_count(param : NoisyLinearParam) -> Int {
  2 * (param.weight_mu.length() + param.bias_mu.length())
}