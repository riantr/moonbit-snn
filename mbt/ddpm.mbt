// ddpm.mbt -- DDPM composite: schedule + score network (v0.115.0).
//
// A Denoising Diffusion Probabilistic Model (Ho et al. 2020) pairs the
// fixed forward process (DiffusionSchedule) with a learned reverse
// process parameterized by a ScoreNetwork. This file ships:
//
//   - DDPM struct (DiffusionSchedule + ScoreNetwork + input_dim).
//   - ddpm_loss: simple MSE between predicted and true noise.
//   - ddpm_sample_step: one reverse step x_t -> x_{t-1}.
//   - ddpm_sample: full reverse chain starting from pure Gaussian noise.
//   - ddpm_predict_x0: derive x_0 prediction from epsilon prediction.
//
// The reverse step is the DDPM parameterisation:
//
//   x_{t-1} = (1/sqrt(alpha[t])) * (x_t - (1 - alpha[t]) / sqrt(1 - alpha_bar[t]) * eps_theta(x_t, t))
//             + sigma[t] * z,   z ~ N(0, I),   sigma[t] = sqrt(beta[t])
//
// (with sigma[t] = 0 at t = 0).
//
// Reference: Ho et al. 2020.

///|
/// DDPM: composite of a forward schedule and a learned score network.
pub struct DDPM {
  input_dim : Int
  schedule : DiffusionSchedule
  net : ScoreNetwork
}

///|
/// Build a fresh DDPM. `schedule` is created by `linear_schedule` or
/// `cosine_schedule`; `net` is a ScoreNetwork matching `input_dim`.
pub fn DDPM::new(
  input_dim : Int,
  schedule : DiffusionSchedule,
  net : ScoreNetwork,
) -> DDPM {
  { input_dim, schedule, net }
}

///|
/// MSE loss between predicted noise (from the score network) and the
/// true noise. Returns the mean squared error.
pub fn ddpm_loss(
  ddpm : DDPM,
  x0 : Array[Float],
  t : Int,
  noise : Array[Float],
) -> Float {
  let (xt, _) = q_sample_pair(x0, t, ddpm.schedule, Xoshiro::new(0UL))
  // Use the supplied noise instead of regenerating it.
  let sa = ddpm.schedule.sqrt_alpha_bars[t]
  let sb = ddpm.schedule.sqrt_one_minus_alpha_bars[t]
  let n = ddpm.input_dim
  let xt_supplied : Array[Float] = Array::make(n, 0.0F)
  for i in 0..<n {
    xt_supplied[i] = sa * x0[i] + sb * noise[i]
  }
  let _ = xt
  let pred = score_network_predict(ddpm.net, xt_supplied, t)
  let mut sum = 0.0F
  for i in 0..<n {
    let d = pred[i] - noise[i]
    sum = sum + d * d
  }
  sum / Float::from_int(n)
}

///|
/// Predict x_0 from x_t and the epsilon prediction. Useful for
/// visualisation and analysis, even though training optimises eps.
pub fn ddpm_predict_x0(
  ddpm : DDPM,
  xt : Array[Float],
  t : Int,
  noise_pred : Array[Float],
) -> Array[Float] {
  let sa = ddpm.schedule.sqrt_alpha_bars[t]
  let sb = ddpm.schedule.sqrt_one_minus_alpha_bars[t]
  let n = ddpm.input_dim
  let out : Array[Float] = Array::make(n, 0.0F)
  for i in 0..<n {
    out[i] = (xt[i] - sb * noise_pred[i]) / sa
  }
  out
}

///|
/// One reverse step: x_t -> x_{t-1} (Ho et al. 2020 parameterisation).
/// At t = 0, no extra noise is added.
pub fn ddpm_sample_step(
  ddpm : DDPM,
  xt : Array[Float],
  t : Int,
  rng : Xoshiro,
) -> Array[Float] {
  let n = ddpm.input_dim
  let eps = score_network_predict(ddpm.net, xt, t)
  let sa = ddpm.schedule.alphas[t]
  let sa_bar = ddpm.schedule.alpha_bars[t]
  let sa_bar_prev = if t == 0 { 1.0F } else { ddpm.schedule.alpha_bars[t - 1] }
  let beta_t = ddpm.schedule.betas[t]
  // mean = (1/sqrt(alpha_t)) * (x_t - (1 - alpha_t)/sqrt(1 - alpha_bar_t) * eps)
  let coef = (1.0F - sa) / sqrtf(1.0F - sa_bar)
  let mean : Array[Float] = Array::make(n, 0.0F)
  let inv_sqrt_alpha = 1.0F / sqrtf(sa)
  for i in 0..<n {
    mean[i] = inv_sqrt_alpha * (xt[i] - coef * eps[i])
  }
  // sigma_t^2 = beta_tilde_t = (1 - alpha_bar_{t-1}) / (1 - alpha_bar_t) * beta_t
  let sigma_sq = if t == 0 { 0.0F } else {
    (1.0F - sa_bar_prev) / (1.0F - sa_bar) * beta_t
  }
  let sigma = sqrtf(sigma_sq)
  let out : Array[Float] = Array::make(n, 0.0F)
  if t == 0 {
    for i in 0..<n {
      out[i] = mean[i]
    }
  } else {
    let pairs = n / 2
    let mut i = 0
    while i < pairs * 2 {
      let (z1d, z2d) = box_muller(rng)
      let z1 = Float::from_double(z1d)
      let z2 = Float::from_double(z2d)
      out[i] = mean[i] + sigma * z1
      if i + 1 < n {
        out[i + 1] = mean[i + 1] + sigma * z2
      }
      i = i + 2
    }
    if n % 2 == 1 {
      let (z1d, _) = box_muller(rng)
      let z1 = Float::from_double(z1d)
      out[n - 1] = mean[n - 1] + sigma * z1
    }
  }
  out
}

///|
/// Full reverse chain: pure Gaussian noise -> x_0. Returns x_0.
pub fn ddpm_sample(
  ddpm : DDPM,
  rng : Xoshiro,
) -> Array[Float] {
  let n = ddpm.input_dim
  let mut xt : Array[Float] = Array::make(n, 0.0F)
  let pairs = n / 2
  let mut i = 0
  while i < pairs * 2 {
    let (z1d, z2d) = box_muller(rng)
    xt[i] = Float::from_double(z1d)
    if i + 1 < n {
      xt[i + 1] = Float::from_double(z2d)
    }
    i = i + 2
  }
  if n % 2 == 1 {
    let (z1d, _) = box_muller(rng)
    xt[n - 1] = Float::from_double(z1d)
  }
  let mut t = ddpm.schedule.num_steps - 1
  while t >= 0 {
    xt = ddpm_sample_step(ddpm, xt, t, rng)
    t = t - 1
  }
  xt
}
