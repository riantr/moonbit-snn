// neuron_inhomogeneous_poisson.mbt — port of
// `refs/SNNModels.jl/src/populations/inhomogeneous_poisson.jl`.
//
// The Julia module exports `InhomogeneousPoisson` / `InhomogeneousPoissonParam`.
// The tests refer to it as `VariablePoisson` / `VariablePoissonParameter`.
// We use the source names (`InhomogeneousPoisson*`).
//
// Integration rule (matches Julia):
//   noise[i] = (noise[i] - re) * (1 - dt/τ) + re
//   where re = rand_uniform() - 0.5
//   Erate = max(r0/2 * max(noise[i] * β, 1.0) + r[i], 0.0)
//   r[i] += (r0 - Erate) / rate_timescale * dt
//   p_spike = 1 - exp(-Erate * dt)
//   fire[i] = rand_uniform() < p_spike

///|
/// InhomogeneousPoissonParam — Julia's `VariablePoissonParameter`.
///
///   β            : noise modulation amplitude (default 0.0)
///   τ            : noise time-constant (default 50.0 ms)
///   r0           : target rate (default 1000 Hz = 1.0 in normalised units)
///   rate_timescale : time-constant for rate adaptation (default 400 ms)
pub(all) struct InhomogeneousPoissonParam {
  beta : Float       // β
  tau : Float        // τ (ms)
  r0 : Float         // r0 (Hz normalised, default 1.0)
  rate_timescale : Float  // ms
}

///|
/// Julia defaults: β=0.0, τ=50ms, r0=1kHz, rate_timescale=400ms.
/// In normalised units, 1 kHz = 1.0.
pub fn InhomogeneousPoissonParam::new() -> InhomogeneousPoissonParam {
  { beta: 0.0F, tau: 50.0F, r0: 1.0F, rate_timescale: 400.0F }
}

///|
/// Custom InhomogeneousPoissonParam with explicit β / τ / r0.
/// Mirrors Julia's `VariablePoissonParameter(β = 0.1, τ = 100ms,
/// r0 = 500Hz)`.
pub fn InhomogeneousPoissonParam::custom(
  beta : Float,
  tau : Float,
  r0 : Float,
) -> InhomogeneousPoissonParam {
  { beta, tau, r0, rate_timescale: 400.0F }
}

///|
/// Inhomogeneous Poisson population (Julia's `VariablePoisson`).
/// Rate at each neuron adapts over `rate_timescale` toward `r0`,
/// with multiplicative Ornstein-Uhlenbeck noise on `β` scale.
pub(all) struct InhomogeneousPoisson {
  n : Int
  param : InhomogeneousPoissonParam
  fire : Array[Bool]
  r : Array[Float]            // current per-neuron rate (Hz normalised)
  noise : Array[Float]        // OU noise cache
  randcache_beta : Array[Float]  // pre-allocated uniform [0, 1) draws
}

///|
/// Construct an InhomogeneousPoisson with `n` neurons and an
/// Xoshiro RNG. All state arrays are zero-initialised except `r`
/// (initialised to `r0`) and `randcache_beta` (initialised to
/// fresh uniform draws).
pub fn InhomogeneousPoisson::new(
  n : Int,
  param : InhomogeneousPoissonParam,
  rng : Xoshiro,
) -> InhomogeneousPoisson {
  let fire : Array[Bool] = Array::make(n, false)
  let r : Array[Float] = Array::make(n, param.r0)
  let noise : Array[Float] = Array::make(n, 0.0F)
  let randcache_beta : Array[Float] = Array::make(n, 0.0F)
  let mut i = 0
  while i < n {
    randcache_beta[i] = next_f32(rng)
    i = i + 1
  }
  { n, param, fire, r, noise, randcache_beta }
}

///|
/// Integrate one time-step of Inhomogeneous Poisson process.
///
/// Steps (matches Julia's `integrate!`):
///   1. Re-draw uniform noise cache.
///   2. Reset `fire[i] = false` for all i.
///   3. For each neuron i:
///      a. `re = randcache_beta[i] - 0.5`
///      b. `noise[i] = (noise[i] - re) * (1 - dt/τ) + re`
///         (Ornstein-Uhlenbeck smoothing toward 0)
///      c. `Erate = max(r0/2 * max(noise[i] * β, 1.0) + r[i], 0.0)`
///      d. `r[i] += (r0 - Erate) / rate_timescale * dt`
///      e. `p_spike = 1 - exp(-Erate * dt)`
///      f. `fire[i] = rand_uniform() < p_spike`
pub fn step_inhomogeneous_poisson(
  p : InhomogeneousPoisson,
  dt : Float,
  rng : Xoshiro,
) -> Unit {
  let n = p.n
  let r0 = p.param.r0
  let beta = p.param.beta
  let tau = p.param.tau
  let rate_ts = p.param.rate_timescale
  // 1. Re-draw uniform noise cache.
  let mut i = 0
  while i < n {
    p.randcache_beta[i] = next_f32(rng)
    p.fire[i] = false
    i = i + 1
  }
  // 2. Per-neuron update.
  let cc_factor = 1.0F - dt / tau
  let mut j = 0
  while j < n {
    let re = p.randcache_beta[j] - 0.5F
    p.noise[j] = (p.noise[j] - re) * cc_factor + re
    // R(x) = max(x, 1.0) for the noise term; then Erate = max(r0/2*R + r, 0).
    let noise_beta_term = if p.noise[j] * beta > 1.0F {
      p.noise[j] * beta
    } else {
      1.0F
    }
    let erate_pre = r0 / 2.0F * noise_beta_term + p.r[j]
    let erate = if erate_pre > 0.0F { erate_pre } else { 0.0F }
    p.r[j] = p.r[j] + (r0 - erate) / rate_ts * dt
    // p_spike = 1 - exp(-erate * dt). For small erate*dt, use expf.
    let p_spike = 1.0F - expf(-erate * dt)
    p.fire[j] = next_f32(rng) < p_spike
    j = j + 1
  }
}