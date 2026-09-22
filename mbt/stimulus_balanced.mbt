// stimulus_balanced.mbt — BalancedStimulus (feedback-driven inhomogeneous
// Poisson generator) port of SNNModels.jl/src/stimuli/balanced.jl.
//
// The Julia reference provides a "balanced" Poisson input: each post-synaptic
// neuron receives both excitatory (:ge) and inhibitory (:gi) spike input whose
// instantaneous rate is driven by a low-pass filtered random walk. The model
// is intended for E/I balanced state experiments (e.g. `balanced.jl`).
//
// Bit-exact note: the Poisson sampler here uses Knuth's algorithm with our
// Xoshiro RNG (same as PoissonStimulusIF). Julia's `Distributions.Poisson
// {Float32}(λ).rand` uses its own implementation; sequences will not be
// bit-exact. Expected per-step counts (`rate_internal * dt`) and qualitative
// dynamics match.
//
// Unit convention: `rate`/`r0` is stored as rate per ms (`rate_Hz * hz`)
// matching the rest of the codebase. Default `r0 = 1.0` corresponds to
// 1 kHz (since `khz = 1.0F` and `hz = 0.001F`).
//
// Algorithm (per step, mirrors Julia's `stimulate!(p, param, time, dt)`):
//   1. Inhibitory input (inhomogeneous Poisson with rate r0*kIE):
//        gi[n] += w * wIE * k, where k ~ Poisson(r0*kIE*dt)
//   2. Excitatory input (rate-adapting, per-neuron):
//        re = rand_uniform() - 0.5F                  // [-0.5, 0.5)
//        cc = 1 - dt/τ                               // low-pass coefficient
//        noise[i] = (noise[i] - re) * cc + re
//        nb = clamp(noise[i]*β, 0, 1)
//        Erate = max(0, r0/2 * nb + r[i])             // R(., 0.0)
//        r[i] += (r0 - Erate) / 400ms * dt            // rate adaptation
//        ge[i] += w * k, where k ~ Poisson(Erate*dt)

///|
/// Parameter set for `BalancedStimulus`. Mirrors Julia's `BalancedParameter`.
///
/// `kIE` — scale for inhibitory rate (multiplies `r0` to get the
///         inhomogeneous Poisson rate for the inhibitory channel).
/// `beta` — amplitude of the low-pass-filtered noise injected into the
///          excitatory firing rate.
/// `tau` — time constant (ms) of the noise filter.
/// `r0` — baseline firing rate (stored in internal units: rate per ms).
/// `w` — per-spike conductance scaling for the post-synaptic receptors.
/// `wIE` — additional scaling for inhibitory spikes.
/// `same_input` — when `true`, all neurons share the same noise/rate trace.
pub(all) struct BalancedParameter {
  kIE : Float
  beta : Float
  tau : Float
  r0 : Float
  w : Float
  wIE : Float
  same_input : Bool
}

///|
/// Construct a `BalancedParameter` matching Julia's defaults.
///
/// All parameters are keyword-optional. Defaults:
///   - `kIE=1.0`, `beta=0.0`, `tau=50ms`, `r0=1.0*khz`, `w=1.0`,
///     `wIE=1.0`, `same_input=false`.
pub fn BalancedParameter::new(
  kIE? : Float = 1.0F,
  beta? : Float = 0.0F,
  tau? : Float = 50.0F,
  r0? : Float = 1.0F * khz,
  w? : Float = 1.0F,
  wIE? : Float = 1.0F,
  same_input? : Bool = false,
) -> BalancedParameter {
  { kIE, beta, tau, r0, w, wIE, same_input }
}

///|
/// `BalancedStimulus` — feedback-driven Poisson source targeting an IF
/// (or any population with `glu` / `gaba` receptor fields).
///
/// State (per neuron):
///   - `r`     — current excitatory firing rate (internal units, per ms).
///   - `noise` — low-pass-filtered random walk driving `r` toward `r0`.
///   - `fire`  — per-neuron spike record for the current step.
///
/// The constructor picks up the post-synaptic `glu` / `gaba` arrays by
/// reference, so subsequent `stimulate_balanced` calls update the
/// receptors in-place (just like `PoissonStimulusIF`).
pub(all) struct BalancedStimulus {
  param : BalancedParameter
  n : Int
  ge : Array[Float]
  gi : Array[Float]
  fire : Array[Bool]
  r : Array[Float]
  noise : Array[Float]
  rng : Xoshiro
}

///|
/// Construct a `BalancedStimulus` targeting an IF population on `:ge` /
/// `:gi` receptors. `seed` controls the Xoshiro RNG used to draw uniform
/// noise and Poisson counts.
pub fn BalancedStimulus::new(
  pop : IF,
  sym_e? : String = "ge",
  sym_i? : String = "gi",
  param? : BalancedParameter = BalancedParameter::new(),
  seed? : UInt64 = 42UL,
) -> BalancedStimulus {
  let n = pop.n
  let r0_internal = param.r0
  let r = Array::make(n, r0_internal)
  let noise = Array::make(n, 0.0F)
  // sym_e / sym_i are accepted for API symmetry with PoissonStimulusIF,
  // but the receptor routing is fixed at glu/gaba for the balanced model.
  let _ = sym_e
  let _ = sym_i
  {
    param,
    n,
    ge: pop.glu,
    gi: pop.gaba,
    fire: Array::make(n, false),
    r,
    noise,
    rng: Xoshiro::new(seed),
  }
}

///|
/// One stimulation step. Updates `fire`, `r`, `noise`, and adds samples
/// to `ge` / `gi` arrays. See file header for the algorithm.
pub fn stimulate_balanced(
  s : BalancedStimulus,
  time : Float,
  dt : Float,
) -> Unit {
  let _ = time
  let n = s.n
  let param = s.param
  let kIE = param.kIE
  let beta = param.beta
  let tau = param.tau
  let r0 = param.r0 // internal rate per ms
  let w = param.w
  let wIE = param.wIE

  // 1. Inhibitory spikes (homogeneous Poisson at rate r0*kIE).
  let inh_lambda = r0 * kIE * dt
  for k in 0..<n {
    s.fire[k] = false
    if inh_lambda <= 0.0F {
      continue
    }
    let m = sample_poisson(s.rng, inh_lambda)
    if m > 0 {
      s.gi[k] = s.gi[k] + w * Float::from_int(m) * wIE
    }
  }

  // 2. Excitatory spikes (rate adapts via low-pass noise).
  //    For same_input=true, drive one shared trace and broadcast; for
  //    same_input=false (default), each neuron has its own trace.
  let cc = 1.0F - dt / tau
  if param.same_input {
    // Shared trace: draw one uniform, update r[0] / noise[0], then
    // sample Poisson once and broadcast the contribution to all ge[i].
    let re = next_f64(s.rng).to_float() - 0.5F
    s.noise[0] = (s.noise[0] - re) * cc + re
    let mut nb = s.noise[0] * beta
    if nb > 1.0F {
      nb = 1.0F
    }
    if nb < 0.0F {
      nb = 0.0F
    }
    let mut erate = r0 / 2.0F * nb + s.r[0]
    if erate < 0.0F {
      erate = 0.0F
    }
    s.r[0] = s.r[0] + (r0 - erate) / 400.0F * dt
    let exc_lambda = erate * dt
    let m = sample_poisson(s.rng, exc_lambda)
    if m > 0 {
      let add = w * Float::from_int(m)
      for i in 0..<n {
        s.ge[i] = s.ge[i] + add
        s.fire[i] = true
      }
    }
  } else {
    for i in 0..<n {
      let re = next_f64(s.rng).to_float() - 0.5F
      s.noise[i] = (s.noise[i] - re) * cc + re
      let mut nb = s.noise[i] * beta
      if nb > 1.0F {
        nb = 1.0F
      }
      if nb < 0.0F {
        nb = 0.0F
      }
      let mut erate = r0 / 2.0F * nb + s.r[i]
      if erate < 0.0F {
        erate = 0.0F
      }
      s.r[i] = s.r[i] + (r0 - erate) / 400.0F * dt
      let exc_lambda = erate * dt
      let m = sample_poisson(s.rng, exc_lambda)
      if m > 0 {
        s.ge[i] = s.ge[i] + w * Float::from_int(m)
        s.fire[i] = true
      }
    }
  }
}

///|
/// Reset per-neuron state (`r`, `noise`, `fire`) to the constructor
/// defaults without re-allocating the arrays. Useful between
/// independent simulation runs against the same IF population.
pub fn reset_balanced(s : BalancedStimulus) -> Unit {
  let r0 = s.param.r0
  for k in 0..<s.n {
    s.r[k] = r0
    s.noise[k] = 0.0F
    s.fire[k] = false
  }
}
