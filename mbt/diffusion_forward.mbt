// diffusion_forward.mbt -- DDPM forward process primitives (v0.113.0).
//
// The forward diffusion process (Ho et al. 2020, "Denoising Diffusion
// Probabilistic Models") is a fixed Markov chain that gradually adds
// Gaussian noise to a clean sample x_0 over T steps:
//
//   q(x_t | x_0) = N(x_t; sqrt(alpha_bar[t]) * x_0, (1 - alpha_bar[t]) I)
//
// where alpha_bar[t] = prod_{s=0..t} (1 - beta[s]). The betas come from
// a chosen schedule (linear or cosine).
//
// Scope of v0.113.0:
//   - DiffusionSchedule struct (betas / alphas / alpha_bars / T).
//   - linear_schedule / cosine_schedule constructors.
//   - q_sample: closed-form forward step.
//   - q_sample_pair: convenience that draws noise and returns (x_t, noise)
//     for training the reverse network.
//
// Reference: Ho et al. 2020 (DDPM); Nichol & Dhariwal 2021 (cosine).

///|
/// DiffusionSchedule: holds the T betas, the derived alphas
/// (alpha_t = 1 - beta_t) and cumulative products alpha_bars, plus a
/// few useful pre-computed square-roots used by the closed-form forward
/// step.
pub struct DiffusionSchedule {
  num_steps : Int
  betas : Array[Float]
  alphas : Array[Float]
  alpha_bars : Array[Float]
  sqrt_alpha_bars : Array[Float]
  sqrt_one_minus_alpha_bars : Array[Float]
}

///|
/// Linear beta schedule from `beta_start` to `beta_end` over T steps
/// (Ho et al. 2020 default: T=1000, beta_start=1e-4, beta_end=0.02).
pub fn linear_schedule(
  num_steps : Int,
  beta_start : Float,
  beta_end : Float,
) -> DiffusionSchedule {
  let betas : Array[Float] = Array::make(num_steps, 0.0F)
  for t in 0..<num_steps {
    // Linearly interpolate beta[t] from beta_start to beta_end.
    let frac = Float::from_int(t) / Float::from_int(num_steps - 1)
    betas[t] = beta_start + (beta_end - beta_start) * frac
  }
  build_schedule(num_steps, betas)
}

///|
/// Cosine beta schedule (Nichol & Dhariwal 2021):
///   alpha_bar(t/T) = cos((t/T + s) / (1 + s) * pi/2)^2
/// `s` is a small offset that controls how fast the schedule drops near
/// t=0 (default s=0.008).
pub fn cosine_schedule(num_steps : Int, s : Float) -> DiffusionSchedule {
  let betas : Array[Float] = Array::make(num_steps, 0.0F)
  let steps : Float = Float::from_int(num_steps)
  for t in 0..<num_steps {
    // t/T in (0, 1]; for t=0 we use the limit so alpha_bar[0] = 1.
    let t_f = if t == 0 { 1.0F / steps } else { Float::from_int(t) / steps }
    let alpha_bar_t = pow_cosf((t_f + s) / (1.0F + s) * 1.5707963F, 2.0F)
    let t_prev_f = if t == 0 { 0.0F } else { Float::from_int(t - 1) / steps }
    let alpha_bar_prev = if t == 0 {
      1.0F
    } else {
      pow_cosf((t_prev_f + s) / (1.0F + s) * 1.5707963F, 2.0F)
    }
    let clipped = if alpha_bar_t < 1.0e-20F { 1.0e-20F } else { alpha_bar_t }
    let b = 1.0F - clipped / alpha_bar_prev
    betas[t] = if b < 1.0e-20F { 1.0e-20F } else { b }
  }
  build_schedule(num_steps, betas)
}

///|
/// helper: cos(x)^p via powf on exp(log(cos(x)) * p).
fn pow_cosf(x : Float, p : Float) -> Float {
  let c = cosf(x)
  let abs_c = if c < 0.0F { -c } else { c }
  expf(logf(abs_c) * p)
}

///|
/// helper: build the full DiffusionSchedule from a flat betas array.
fn build_schedule(
  num_steps : Int,
  betas : Array[Float],
) -> DiffusionSchedule {
  let alphas : Array[Float] = Array::make(num_steps, 0.0F)
  let alpha_bars : Array[Float] = Array::make(num_steps, 0.0F)
  let sqrt_alpha_bars : Array[Float] = Array::make(num_steps, 0.0F)
  let sqrt_one_minus_alpha_bars : Array[Float] = Array::make(num_steps, 0.0F)
  let mut ab = 1.0F
  for t in 0..<num_steps {
    let a = 1.0F - betas[t]
    alphas[t] = a
    ab = ab * a
    alpha_bars[t] = ab
    sqrt_alpha_bars[t] = sqrtf(ab)
    sqrt_one_minus_alpha_bars[t] = sqrtf(1.0F - ab)
  }
  { num_steps, betas, alphas, alpha_bars, sqrt_alpha_bars, sqrt_one_minus_alpha_bars }
}

///|
/// Closed-form forward step: returns x_t ~ q(x_t | x_0). Caller
/// supplies the noise vector; x_0 and noise must have the same length.
pub fn q_sample(
  x0 : Array[Float],
  noise : Array[Float],
  t : Int,
  sched : DiffusionSchedule,
) -> Array[Float] {
  let n = x0.length()
  let out : Array[Float] = Array::make(n, 0.0F)
  let sa = sched.sqrt_alpha_bars[t]
  let sb = sched.sqrt_one_minus_alpha_bars[t]
  for i in 0..<n {
    out[i] = sa * x0[i] + sb * noise[i]
  }
  out
}

///|
/// Convenience: draw standard-normal noise and call `q_sample`. Returns
/// (x_t, noise) so the caller can train the reverse network against the
/// same noise.
pub fn q_sample_pair(
  x0 : Array[Float],
  t : Int,
  sched : DiffusionSchedule,
  rng : Xoshiro,
) -> (Array[Float], Array[Float]) {
  let n = x0.length()
  let noise : Array[Float] = Array::make(n, 0.0F)
  let pairs = n / 2
  let mut i = 0
  while i < pairs * 2 {
    // Box-Muller gives two standard normals at a time (as Double).
    let (z1d, z2d) = box_muller(rng)
    noise[i] = Float::from_double(z1d)
    if i + 1 < n {
      noise[i + 1] = Float::from_double(z2d)
    }
    i = i + 2
  }
  // If n is odd, draw one extra.
  if n % 2 == 1 {
    let (z1d, _) = box_muller(rng)
    noise[n - 1] = Float::from_double(z1d)
  }
  let xt = q_sample(x0, noise, t, sched)
  (xt, noise)
}
