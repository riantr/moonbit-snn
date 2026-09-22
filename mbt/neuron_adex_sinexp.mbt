// AdExSinExp — Adaptive Exponential Integrate-and-Fire neuron with
// SINGLE-EXPONENTIAL synapses (instead of double-exponential).
//
// Julia reference:
//   SNNModels.jl/src/populations/synapse/synapses/SingleExpSynapse.jl
//   LKD2014SingleExp in SNNUtils.jl/src/models/lkd2014.jl uses this
//   parameterisation (τe=6ms, τi=2ms).
//
// Float32 contract: every arithmetic uses `Float` (Float32); expf
// for the exponential term (matches Julia's exp(Float32, x)).
//
// Bit-exact ordering: same as AdEx (Brette-Gerstner 2005) for the
// neuron dynamics; the only difference is in `adex_sinexp_step_synapses`
// which does single-exponential ge/gi decay instead of double-exponential
// (no separate rise variables he/hi).
//
// Difference vs AdEx:
//   - AdEx (double-exp): he/hi are rise conductances that buffer
//     spikes from glu/gaba before being integrated into ge/gi.
//   - AdExSinExp: no he/hi. glu is added directly to ge (and gaba to gi).
//   Julia formula (SingleExpSynapse):
//     ge[i] += glu[i]
//     gi[i] += gaba[i]
//     ge[i] += dt * (-ge[i] / τe)
//     gi[i] += dt * (-gi[i] / τi)
//     glu[i] = 0
//     gaba[i] = 0

///|
/// AdExSinExpParameter — same as AdExParameter (the neuron model
/// is identical; only the synapse model differs). We keep it as a
/// separate type to make `param: AdExSinExpParameter` a useful
/// documentation marker and to match Julia's `AdExSinExpParameter`.
pub(all) struct AdExSinExpParameter {
  c : Float
  gl : Float
  vt : Float
  vr : Float
  el : Float
  tm : Float
  r : Float
  dt_slope : Float
  tw : Float
  a : Float
  b : Float
}

///|
/// Default AdExSinExpParameter, matching Julia's `AdExSinExpParameter()`.
/// All values identical to AdExParameter defaults.
pub fn AdExSinExpParameter::new() -> AdExSinExpParameter {
  let c : Float = 281.0F
  let gl : Float = 40.0F
  let tm : Float = 281.0F / 40.0F
  let r : Float = 1.0F / 40.0F
  { c, gl, vt: -50.0F, vr: -70.6F, el: -70.6F, tm, r, dt_slope: 2.0F,
    tw: 144.0F, a: 4.0F, b: 80.5F }
}

///|
/// AdExSinExpParameter with LKD 2014 defaults (El=-70mV, Vt=-52mV,
/// Vr=-60mV, At=10mV). Matches the LKD2014SingleExp AdEx config in
/// SNNUtils.jl/src/models/lkd2014.jl.
pub fn AdExSinExpParameter::lkd_adex() -> AdExSinExpParameter {
  let c : Float = 300.0F
  let gl : Float = 15.0F
  let tm : Float = c / gl
  let r : Float = 1.0F / gl
  { c, gl, vt: -52.0F, vr: -60.0F, el: -70.0F, tm, r, dt_slope: 2.0F,
    tw: 144.0F, a: 4.0F, b: 80.5F }
}

///|
/// AdEx SinExp neuron state — a population of N adaptive exponential
/// LIF neurons with single-exponential synaptic dynamics.
pub struct AdExSinExp {
  param : AdExSinExpParameter
  spike : AdExPostSpike
  n : Int
  v : Array[Float]
  w : Array[Float]
  fire : Array[Bool]
  threshold : Array[Float]
  tabs : Array[Int]
  i : Array[Float]
  syn_curr : Array[Float]
  // SinExp synapse state (no he/hi — single exponential).
  ge : Array[Float]
  gi : Array[Float]
  glu : Array[Float]
  gaba : Array[Float]
  gsyn_e : Array[Float]
  gsyn_i : Array[Float]
  // Reversal potentials.
  e_e : Float
  e_i : Float
  // SinExp time constants.
  tau_e : Float
  tau_i : Float
}

///|
/// Construct a new AdExSinExp population with `n` neurons, default
/// PostSpike (At=0mV, τA=10ms) and LKD-like defaults
/// (E_e=0mV, E_i=-75mV, τe=6ms, τi=2ms).
pub fn AdExSinExp::new(
  n : Int,
  param : AdExSinExpParameter,
  rng : Xoshiro,
) -> AdExSinExp {
  AdExSinExp::new_with_spike(n, param, AdExPostSpike::new(), rng)
}

///|
/// Construct a new AdExSinExp population with a custom PostSpike.
pub fn AdExSinExp::new_with_spike(
  n : Int,
  param : AdExSinExpParameter,
  spike : AdExPostSpike,
  rng : Xoshiro,
) -> AdExSinExp {
  let v = Array::make(n, 0.0F)
  let spread = param.vt - param.vr
  for k in 0..<n {
    v[k] = param.vr + next_f32(rng) * spread
  }
  let w : Array[Float] = Array::make(n, 0.0F)
  let fire : Array[Bool] = Array::make(n, false)
  let threshold : Array[Float] = Array::make(n, param.vt)
  let tabs : Array[Int] = Array::make(n, 1)
  let i : Array[Float] = Array::make(n, 0.0F)
  let syn_curr : Array[Float] = Array::make(n, 0.0F)
  let ge : Array[Float] = Array::make(n, 0.0F)
  let gi : Array[Float] = Array::make(n, 0.0F)
  let glu : Array[Float] = Array::make(n, 0.0F)
  let gaba : Array[Float] = Array::make(n, 0.0F)
  let gsyn_e : Array[Float] = Array::make(n, 1.0F)
  let gsyn_i : Array[Float] = Array::make(n, 1.0F)
  { param, spike, n, v, w, fire, threshold, tabs, i, syn_curr,
    ge, gi, glu, gaba, gsyn_e, gsyn_i,
    e_e: 0.0F, e_i: -75.0F,
    tau_e: 6.0F, tau_i: 2.0F }
}

///|
/// Update the AdExSinExp synapse state for one step.
/// Bit-exact port of Julia's `update_synapses!` for SingleExpSynapse:
///   ge[i] += glu[i]
///   gi[i] += gaba[i]
///   ge[i] += dt * (-ge[i] / τe)
///   gi[i] += dt * (-gi[i] / τi)
///   glu[i] = 0
///   gaba[i] = 0
pub fn adex_sinexp_step_synapses(p : AdExSinExp, dt : Float) -> Unit {
  let n = p.n
  let tau_e = p.tau_e
  let tau_i = p.tau_i
  // Consume glu/gaba into ge/gi, then apply single-exponential decay.
  for i in 0..<n {
    p.ge[i] = p.ge[i] + p.glu[i]
    p.gi[i] = p.gi[i] + p.gaba[i]
    p.ge[i] = p.ge[i] + dt * (-p.ge[i] / tau_e)
    p.gi[i] = p.gi[i] + dt * (-p.gi[i] / tau_i)
  }
  for i in 0..<n {
    p.glu[i] = 0.0F
    p.gaba[i] = 0.0F
  }
  ()
}

///|
/// Compute synaptic current: syn_curr = ge * (v - E_e) * gsyn_e
///                                + gi * (v - E_i) * gsyn_i
/// Same formula as the DoubleExp AdEx variant — the synaptic
/// dynamics differ, but the resulting current is identical.
pub fn adex_sinexp_synaptic_current(p : AdExSinExp) -> Unit {
  let n = p.n
  for i in 0..<n {
    p.syn_curr[i] = p.ge[i] * (p.v[i] - p.e_e) * p.gsyn_e[i] +
                    p.gi[i] * (p.v[i] - p.e_i) * p.gsyn_i[i]
  }
  ()
}

///|
/// Update the AdExSinExp neuron state for one step.
/// Bit-exact port of `step_adex` (AdEx integration is identical;
/// only the synapse step function differs).
pub fn step_adex_sinexp(p : AdExSinExp, dt : Float) -> Unit {
  let n = p.n
  let p_ = p.param
  let tm = p_.tm
  let vt = p_.vt
  let vr = p_.vr
  let el = p_.el
  let r = p_.r
  let dt_slope = p_.dt_slope
  let tw = p_.tw
  let a = p_.a
  let b = p_.b
  let at = p.spike.at
  let tau_a = p.spike.tau_a
  let tabs_const = p.spike.tabs_const
  let tabs_steps : Int = (tabs_const / dt).to_int()

  for i in 0..<n {
    // Reset membrane potential after spike (from previous step)
    p.v[i] = if p.fire[i] { vr } else { p.v[i] }

    // Absolute refractory period
    p.fire[i] = false
    p.tabs[i] = p.tabs[i] - 1
    if p.tabs[i] > 0 {
      continue
    }

    // Adaptation current
    p.w[i] = p.w[i] + dt * (a * (p.v[i] - el) - p.w[i]) / tw

    // Membrane potential: leakage + exponential + synapses + adaptation + ext I
    let exp_term = if dt_slope < 0.0F {
      0.0F
    } else {
      dt_slope * expf((p.v[i] - p.threshold[i]) / dt_slope)
    }
    p.v[i] = p.v[i] +
      dt *
      (
        -(p.v[i] - el) + exp_term - r * p.syn_curr[i] - r * p.w[i] +
        r * p.i[i]
      ) / tm

    // Threshold dynamics
    p.threshold[i] = p.threshold[i] + dt * (vt - p.threshold[i]) / tau_a

    // Spike detection
    p.fire[i] = p.v[i] >= 0.0F
    p.v[i] = if p.fire[i] { 20.0F } else { p.v[i] }
    p.w[i] = if p.fire[i] { p.w[i] + b } else { p.w[i] }
    p.threshold[i] = if p.fire[i] { p.threshold[i] + at } else { p.threshold[i] }
    p.tabs[i] = if p.fire[i] { tabs_steps } else { p.tabs[i] }
  }
  ()
}

// =========================================================================
// MonitorAdExSinExp — minimal monitor for AdExSinExp populations.
// Mirrors MonitorAdEx but for the SinExp variant. The Monitor struct in
// sim.mbt is hardcoded to IF (it stores `pop : IF`), so for AdExSinExp
// we provide a separate type. Same idea as MonitorAdEx for AdEx.
// =========================================================================

///|
/// A monitor for an AdExSinExp population. Records :v / :fire / :w.
pub struct MonitorAdExSinExp {
  pop : AdExSinExp
  sym : String
  data : Array[Float]
  times : Array[Float]
  neuron : Int
}

///|
pub fn MonitorAdExSinExp::new_v(pop : AdExSinExp, neuron : Int) -> MonitorAdExSinExp {
  { pop, sym: "v", data: [], times: [], neuron }
}

///|
pub fn MonitorAdExSinExp::new_fire(pop : AdExSinExp, neuron : Int) -> MonitorAdExSinExp {
  { pop, sym: "fire", data: [], times: [], neuron }
}

///|
/// Take one snapshot, appending to `data` and `times`.
pub fn record_one_sinexp(m : MonitorAdExSinExp, t : Float) -> Unit {
  let v = if m.sym == "v" {
    m.pop.v[m.neuron]
  } else if m.sym == "fire" {
    if m.pop.fire[m.neuron] { 1.0F } else { 0.0F }
  } else if m.sym == "w" {
    m.pop.w[m.neuron]
  } else {
    0.0F
  }
  m.data.push(v)
  m.times.push(t)
}
