// CurrentNoise — noisy current stimulus with exponential decay.
// Bit-exact port of SNNModels.jl.
//
// Julia reference:
//   src/stimuli/current.jl
//
// Julia's stimulate! for CurrentNoise:
//   rand!(I_dist, randcache)   # refresh random cache
//   for i in neurons:
//     I[i] = (I_base[i] + randcache[i]) * (1 - α[i]) + I[i] * α[i]
//
// The α factor is the "current decay": when α=0, I follows I_base + noise
// directly; when α>0, I mixes the new I_base+noise with the previous I
// value (low-pass filter).
//
// Float32 contract: every arithmetic uses `Float` (Float32).

///|
/// CurrentNoise — per-neuron base + noise + decay parameters.
pub struct CurrentNoise {
  n : Int
  i_base : Array[Float]
  noise_sigma : Float
  alpha : Float
}

///|
/// Default CurrentNoise (all zeros: no base current, no noise,
/// α=0 = instantaneous tracking).
pub fn CurrentNoise::new(n : Int) -> CurrentNoise {
  { n, i_base: Array::make(n, 0.0F), noise_sigma: 0.0F, alpha: 0.0F }
}

///|
/// Custom CurrentNoise with explicit base current, noise σ, and α decay.
pub fn CurrentNoise::custom(
  n : Int,
  i_base : Float,
  noise_sigma : Float,
  alpha : Float,
) -> CurrentNoise {
  { n, i_base: Array::make(n, i_base), noise_sigma, alpha }
}

///|
/// stimulate_noise — apply the current-noise step to an external current
/// buffer.
///
///   .I[t+1] = (I_base + N(0, noise_sigma)) * (1 - α) + I[t] * α
///
/// Caller supplies the target `i_target : Array[Float]` (the population's
/// `I` field). Random noise is drawn from the Xoshiro RNG via Box-Muller.
pub fn stimulate_noise(
  param : CurrentNoise,
  i_target : Array[Float],
  rng : Xoshiro,
) -> Unit {
  let n = param.n
  let i_base = param.i_base
  let noise_sigma = param.noise_sigma
  let alpha = param.alpha
  let one_minus_alpha = 1.0F - alpha
  let mut k : Int = 0
  while k < n {
    let noise = box_muller_unit(rng) * noise_sigma
    let i_new = (i_base[k] + noise) * one_minus_alpha + i_target[k] * alpha
    ignore(i_target.set(k, i_new))
    k = k + 1
  }
}

///|
/// box_muller_unit — single N(0, 1) sample from two uniform Float32 draws.
fn box_muller_unit(rng : Xoshiro) -> Float {
  let u1 = next_f64(rng)
  let u1_safe = if u1 < 1.0e-15 { 1.0e-15 } else { u1 }
  let u2 = next_f64(rng)
  let r : Double = (-2.0 * @math.ln(u1_safe)).sqrt()
  let theta : Double = 2.0 * 3.141592653589793 * u2
  Float::from_double(r * @math.cos(theta))
}