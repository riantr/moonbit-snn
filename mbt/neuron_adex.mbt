// AdEx (Adaptive Exponential Integrate-and-Fire) neuron — bit-exact
// port of SNNModels.jl/src/populations/generized_if/adex.jl.
//
// Julia reference:
//   src/populations/generized_if/adex.jl
//   src/populations/spike/postspike.jl
//
// Float32 contract: every arithmetic operation uses `Float` (Float32).
// The exponential term uses `expf` (libm) for bit-exact match with
// Julia's `exp(Float32, x)`.

///|
/// AdExParameter — biophysical constants of an AdEx neuron.
pub struct AdExParameter {
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
/// Default AdExParameter, matching Julia's `AdExParameter()`.
pub fn AdExParameter::new() -> AdExParameter {
  // Julia defaults:
  //   C = 281pF
  //   gl = 40nS
  //   Vt = -50mV
  //   Vr = -70.6mV
  //   El = -70.6mV
  //   τm = C / gl
  //   R = nS / gl
  //   ΔT = 2mV
  //   τw = 144ms
  //   a = 4nS
  //   b = 80.5pA
  //
  // All in Float32 (units normalised to 1.0).
  let c : Float = 281.0F
  let gl : Float = 40.0F
  let tm : Float = 281.0F / 40.0F
  let r : Float = 1.0F / 40.0F
  { c, gl, vt: -50.0F, vr: -70.6F, el: -70.6F, tm, r, dt_slope: 2.0F,
    tw: 144.0F, a: 4.0F, b: 80.5F }
}

///|
/// AdExParameter with a custom reset potential `vr`. Matches Julia's
/// `AdExParameter(; Vr = -50mV)`. Other fields use defaults.
pub fn AdExParameter::with_vr(vr : Float) -> AdExParameter {
  let c : Float = 281.0F
  let gl : Float = 40.0F
  let tm : Float = 281.0F / 40.0F
  let r : Float = 1.0F / 40.0F
  { c, gl, vt: -50.0F, vr, el: -70.6F, tm, r, dt_slope: 2.0F,
    tw: 144.0F, a: 4.0F, b: 80.5F }
}

///|
/// AdExParameter with full custom tm / vt / vr / el / r fields.
/// Matches Julia's `AdExParameter(; tm, vt, vr, el, r)`. Other
/// fields use defaults. Required for non-default Litwin-Kumar-Doiron
/// 2014 parameters (300pF/15nS=20ms tm, -70mV El, -52mV Vt, -60mV Vr,
/// R=1/15nS).
pub fn AdExParameter::custom(
  tm~ : Float,
  vt~ : Float,
  vr~ : Float,
  el~ : Float,
  r~ : Float,
) -> AdExParameter {
  { c: tm * r * 0.0F + 281.0F, gl: 1.0F / r, vt, vr, el, tm, r,
    dt_slope: 2.0F, tw: 144.0F, a: 4.0F, b: 80.5F }
}

///|
/// AdEx PostSpike — adds `At` (threshold jump) and `τA` (threshold decay)
/// on top of the IF PostSpike fields.
pub struct AdExPostSpike {
  at : Float
  tau_a : Float
  ap_membrane : Float
  tabs_const : Float
  up : Float
}

///|
pub fn AdExPostSpike::new() -> AdExPostSpike {
  // Julia: PostSpike{Float32}(; At = 0mV, τA = 10ms, AP_membrane = 10.0f0mV,
  //                       τabs = 1ms, up = 1ms)
  { at: 0.0F, tau_a: 10.0F, ap_membrane: 10.0F, tabs_const: 1.0F, up: 1.0F }
}

///|
/// AdExPostSpike with custom `at` (after-spike threshold jump) and
/// `tau_a` (threshold time constant). Matches Julia's
/// `PostSpike(; At = 10mV, τA = 10ms)`.
pub fn AdExPostSpike::with_at(at : Float, tau_a : Float) -> AdExPostSpike {
  { at, tau_a, ap_membrane: 10.0F, tabs_const: 1.0F, up: 1.0F }
}

///|
/// AdEx neuron state — a population of N adaptive exponential LIF neurons.
pub struct AdEx {
  param : AdExParameter
  spike : AdExPostSpike
  n : Int
  v : Array[Float]
  w : Array[Float]
  fire : Array[Bool]
  threshold : Array[Float]
  tabs : Array[Int]
  i : Array[Float]
  syn_curr : Array[Float]
  // Synapse state
  ge : Array[Float]
  gi : Array[Float]
  he : Array[Float]
  hi : Array[Float]
  glu : Array[Float]
  gaba : Array[Float]
  gsyn_e : Array[Float]
  gsyn_i : Array[Float]
  e_e : Float
  e_i : Float
  tre : Float
  tde : Float
  tri : Float
  tdi : Float
}

///|
/// Construct a new AdEx population with `n` neurons, using the
/// default PostSpike (At=0mV, τA=10ms).
pub fn AdEx::new(n : Int, param : AdExParameter, rng : Xoshiro) -> AdEx {
  AdEx::new_with_spike(n, param, AdExPostSpike::new(), rng)
}

///|
/// Construct a new AdEx population with a custom PostSpike.
pub fn AdEx::new_with_spike(
  n : Int,
  param : AdExParameter,
  spike : AdExPostSpike,
  rng : Xoshiro,
) -> AdEx {
  let v = Array::make(n, 0.0F)
  let spread = param.vt - param.vr
  for k in 0..<n {
    v[k] = param.vr + next_f32(rng) * spread
  }
  let w : Array[Float] = Array::make(n, 0.0F)
  let fire : Array[Bool] = Array::make(n, false)
  let threshold : Array[Float] = Array::make(n, param.vt)
  // Julia: tabs = ones(Int, N) → 1 for every neuron
  let tabs : Array[Int] = Array::make(n, 1)
  let i : Array[Float] = Array::make(n, 0.0F)
  let syn_curr : Array[Float] = Array::make(n, 0.0F)
  let ge : Array[Float] = Array::make(n, 0.0F)
  let gi : Array[Float] = Array::make(n, 0.0F)
  let he : Array[Float] = Array::make(n, 0.0F)
  let hi : Array[Float] = Array::make(n, 0.0F)
  let glu : Array[Float] = Array::make(n, 0.0F)
  let gaba : Array[Float] = Array::make(n, 0.0F)
  let gsyn_e : Array[Float] = Array::make(n, 1.0F)
  let gsyn_i : Array[Float] = Array::make(n, 1.0F)
  { param, spike, n, v, w, fire, threshold, tabs, i, syn_curr,
    ge, gi, he, hi, glu, gaba, gsyn_e, gsyn_i,
    e_e: 0.0F, e_i: -75.0F,
    tre: 1.0F, tde: 6.0F, tri: 0.5F, tdi: 2.0F }
}

///|
/// Update the AdEx neuron state for one step.
/// Bit-exact port of `update_neuron!` for AdEx{Float32}.
pub fn step_adex(p : AdEx, dt : Float) -> Unit {
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

///|
/// Re-export the synapse helpers for AdEx.
pub fn adex_step_synapses(p : AdEx, dt : Float) -> Unit {
  let n = p.n
  for i in 0..<n {
    p.he[i] = p.he[i] + p.glu[i]
    p.hi[i] = p.hi[i] + p.gaba[i]
    p.ge[i] = p.ge[i] + dt * (-p.ge[i] / p.tde + p.he[i])
    p.he[i] = p.he[i] + dt * (-p.he[i] / p.tre)
    p.gi[i] = p.gi[i] + dt * (-p.gi[i] / p.tdi + p.hi[i])
    p.hi[i] = p.hi[i] + dt * (-p.hi[i] / p.tri)
  }
  for i in 0..<n {
    p.glu[i] = 0.0F
    p.gaba[i] = 0.0F
  }
  ()
}

///|
pub fn adex_synaptic_current(p : AdEx) -> Unit {
  let n = p.n
  for i in 0..<n {
    p.syn_curr[i] = p.ge[i] * (p.v[i] - p.e_e) * p.gsyn_e[i] +
                    p.gi[i] * (p.v[i] - p.e_i) * p.gsyn_i[i]
  }
  ()
}

// =========================================================================
// AdEx heterogeneous variant — per-neuron Vector{Float} parameters
// Matches Julia's `AdExParameter{Vector{Float32}}` + `update_neuron!` for
// AdEx{Float32} = Float, AdEx{Vector{Float32}} = per-ne Vector fields.
// Each neuron j has its own vt/vr/el/tm/r/dt_slope/tw/a/b.
// =========================================================================

///|
/// AdExParameterHet — per-neuron heterogeneous AdEx parameters.
/// Each field is `Array[Float]` of length N (post-construction).
/// Matches Julia's `AdExParameter{Vector{Float32}}` fields.
/// `c` and `gl` stay scalar (only used to derive tm/r, not stored).
pub(all) struct AdExParameterHet {
  vt : Array[Float]       // per-ne membrane potential threshold
  vr : Array[Float]       // per-ne reset potential
  el : Array[Float]       // per-ne resting potential
  tm : Array[Float]       // per-ne membrane time constant
  r : Array[Float]        // per-ne membrane resistance
  dt_slope : Array[Float] // per-ne exponential slope
  tw : Array[Float]       // per-ne adaptation time constant
  a : Array[Float]        // per-ne subthreshold adaptation
  b : Array[Float]        // per-ne spike-triggered adaptation
}

///|
/// AdExParameterHet::homogeneous — fills all per-ne arrays with
/// the same scalar value. Useful as a baseline before per-ne draws.
pub fn AdExParameterHet::homogeneous(
  n : Int,
  p : AdExParameter,
) -> AdExParameterHet {
  let vt_arr : Array[Float] = Array::make(n, p.vt)
  let vr_arr : Array[Float] = Array::make(n, p.vr)
  let el_arr : Array[Float] = Array::make(n, p.el)
  let tm_arr : Array[Float] = Array::make(n, p.tm)
  let r_arr : Array[Float] = Array::make(n, p.r)
  let dt_slope_arr : Array[Float] = Array::make(n, p.dt_slope)
  let tw_arr : Array[Float] = Array::make(n, p.tw)
  let a_arr : Array[Float] = Array::make(n, p.a)
  let b_arr : Array[Float] = Array::make(n, p.b)
  { vt: vt_arr, vr: vr_arr, el: el_arr, tm: tm_arr, r: r_arr,
    dt_slope: dt_slope_arr, tw: tw_arr, a: a_arr, b: b_arr }
}

///|
/// AdExHet — population with per-neuron heterogeneous AdEx parameters.
/// Same synaptic state layout as AdEx (ge/gi/he/hi/glu/gaba/gsyn_e/gsyn_i).
pub struct AdExHet {
  param : AdExParameterHet
  spike : AdExPostSpike
  n : Int
  v : Array[Float]
  w : Array[Float]
  fire : Array[Bool]
  threshold : Array[Float]
  tabs : Array[Int]
  i : Array[Float]
  syn_curr : Array[Float]
  ge : Array[Float]
  gi : Array[Float]
  he : Array[Float]
  hi : Array[Float]
  glu : Array[Float]
  gaba : Array[Float]
  gsyn_e : Array[Float]
  gsyn_i : Array[Float]
  e_e : Float
  e_i : Float
  tre : Float
  tde : Float
  tri : Float
  tdi : Float
}

///|
/// Construct a heterogeneous AdEx population with per-neuron params.
pub fn AdExHet::new(
  n : Int,
  param : AdExParameterHet,
  rng : Xoshiro,
) -> AdExHet {
  let v : Array[Float] = Array::make(n, 0.0F)
  // Init: v[i] = param.vr[i] + rand * (param.vt[i] - param.vr[i]).
  let mut k = 0
  while k < n {
    let spread = param.vt[k] - param.vr[k]
    v[k] = param.vr[k] + next_f32(rng) * spread
    k = k + 1
  }
  let w : Array[Float] = Array::make(n, 0.0F)
  let fire : Array[Bool] = Array::make(n, false)
  let threshold : Array[Float] = Array::make(n, 0.0F)
  let mut k2 = 0
  while k2 < n {
    threshold[k2] = param.vt[k2]
    k2 = k2 + 1
  }
  let tabs : Array[Int] = Array::make(n, 1)
  let i : Array[Float] = Array::make(n, 0.0F)
  let syn_curr : Array[Float] = Array::make(n, 0.0F)
  let ge : Array[Float] = Array::make(n, 0.0F)
  let gi : Array[Float] = Array::make(n, 0.0F)
  let he : Array[Float] = Array::make(n, 0.0F)
  let hi : Array[Float] = Array::make(n, 0.0F)
  let glu : Array[Float] = Array::make(n, 0.0F)
  let gaba : Array[Float] = Array::make(n, 0.0F)
  let gsyn_e : Array[Float] = Array::make(n, 1.0F)
  let gsyn_i : Array[Float] = Array::make(n, 1.0F)
  { param, spike: AdExPostSpike::new(), n, v, w, fire, threshold,
    tabs, i, syn_curr, ge, gi, he, hi, glu, gaba, gsyn_e, gsyn_i,
    e_e: 0.0F, e_i: -75.0F,
    tre: 1.0F, tde: 6.0F, tri: 0.5F, tdi: 2.0F }
}

///|
/// Update the heterogeneous AdEx neuron state for one step.
/// Bit-exact port of Julia's `update_neuron!` for
/// `AdEx{Float32} = Vector{Float32}`:
///   v[i] = ifelse(fire[i], vr[i], v[i])
///   tabs[i] -= 1; if tabs[i] > 0 continue
///   w[i] += dt * (a[i] * (v[i] - el[i]) - w[i]) / τw[i]
///   v[i] += dt * (-(v[i] - el[i]) + ΔT[i]*exp((v[i]-θ[i])/ΔT[i])
///              - R[i] * (syn_curr[i] + w[i]) + R[i] * I[i]) / τm[i]
///   θ[i] += dt * (Vt[i] - θ[i]) / τA      (τA scalar from spike)
///   fire[i] = v[i] >= 0
///   ...
pub fn step_adex_het(p : AdExHet, dt : Float) -> Unit {
  let n = p.n
  let spike = p.spike
  let at = spike.at
  let tau_a = spike.tau_a
  let tabs_const = spike.tabs_const
  let tabs_steps : Int = (tabs_const / dt).to_int()

  let mut i = 0
  while i < n {
    let vr_i = p.param.vr[i]
    p.v[i] = if p.fire[i] { vr_i } else { p.v[i] }

    p.fire[i] = false
    p.tabs[i] = p.tabs[i] - 1
    if p.tabs[i] > 0 {
      i = i + 1
      continue
    }

    let a_i = p.param.a[i]
    let el_i = p.param.el[i]
    let tw_i = p.param.tw[i]
    p.w[i] = p.w[i] + dt * (a_i * (p.v[i] - el_i) - p.w[i]) / tw_i

    let exp_term = if p.param.dt_slope[i] < 0.0F {
      0.0F
    } else {
      p.param.dt_slope[i] * expf(
        (p.v[i] - p.threshold[i]) / p.param.dt_slope[i],
      )
    }
    let tm_i = p.param.tm[i]
    let r_i = p.param.r[i]
    p.v[i] = p.v[i] +
      dt *
      (
        -(p.v[i] - el_i) + exp_term - r_i * p.syn_curr[i] - r_i * p.w[i] +
        r_i * p.i[i]
      ) / tm_i

    p.threshold[i] = p.threshold[i] + dt * (p.param.vt[i] - p.threshold[i]) / tau_a

    p.fire[i] = p.v[i] >= 0.0F
    p.v[i] = if p.fire[i] { 20.0F } else { p.v[i] }
    let b_i = p.param.b[i]
    p.w[i] = if p.fire[i] { p.w[i] + b_i } else { p.w[i] }
    p.threshold[i] = if p.fire[i] { p.threshold[i] + at } else { p.threshold[i] }
    p.tabs[i] = if p.fire[i] { tabs_steps } else { p.tabs[i] }

    i = i + 1
  }
  ()
}
