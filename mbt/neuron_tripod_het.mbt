// neuron_tripod_het.mbt — Tripod with heterogeneous per-neuron
// AdEx parameters. Mirrors AdExHet (v0.10.9) and Julia's
// `Tripod(..., NMDA=EyalNMDA, param=AdExParameter{Vector{Float32}})`.
//
// Per-neuron fields: vt/vr/el/tm/r/dt_slope/tw/a/b/c/gl. Dendrite
// geometry (d1/d2) stays shared — Julia's TripodHet uses a single
// Dendrite struct across the population. Heun predictor-corrector
// integration reads per-ne parameters inside the per-neuron loop.
//
// Bit-exact note: this matches Julia's `Tripod{Vector{Float32}}`
// behavior. The soma params vary per neuron but dendrite params +
// AdExPostSpike + Heun correction order are shared across neurons
// (same as Julia).

///|
/// TripodHet — Tripod with heterogeneous per-neuron AdEx soma params.
/// Same compartments (:soma + :d1 + :d2) and same Heun integration as
/// `Tripod`, but each neuron reads its own AdExParameterHet fields.
pub struct TripodHet {
  // Soma (AdEx) parameters — per-neuron arrays.
  soma_param : AdExParameterHet
  soma_spike : AdExPostSpike
  // Soma-membrane derived scalars (per-neuron). Stored explicitly so
  // we don't recompute c = tm/(1000*r) inside the inner loop.
  c : Array[Float]
  gl : Array[Float]
  // Dendrite passive parameters (d1 + d2) — shared across neurons.
  d1 : Dendrite
  d2 : Dendrite
  // External input currents.
  i_s : Array[Float]
  i_d1 : Array[Float]
  i_d2 : Array[Float]
  // Synaptic conductances (single-exp on soma + 2 dendrites).
  ge_s : Array[Float]
  gi_s : Array[Float]
  ge_d1 : Array[Float]
  gi_d1 : Array[Float]
  ge_d2 : Array[Float]
  gi_d2 : Array[Float]
  glu_s : Array[Float]
  gaba_s : Array[Float]
  glu_d1 : Array[Float]
  gaba_d1 : Array[Float]
  glu_d2 : Array[Float]
  gaba_d2 : Array[Float]
  // Synapse parameters (scalar).
  e_e : Float
  e_i : Float
  tau_e : Float
  tau_i : Float
  gsyn_e : Float
  gsyn_i : Float
  // State.
  n : Int
  v_s : Array[Float]
  w_s : Array[Float]
  v_d1 : Array[Float]
  v_d2 : Array[Float]
  fire : Array[Bool]
  threshold : Array[Float]
  tabs : Array[Int]
  // Heun temp arrays (4 * n).
  dv : Array[Float]
  dv_temp : Array[Float]
  syn_curr_s : Array[Float]
  syn_curr_d1 : Array[Float]
  syn_curr_d2 : Array[Float]
}

///|
/// Construct a TripodHet population. `soma_param` must already be
/// filled with per-neuron arrays of length N. Dendrite parameters
/// are derived from `d1`/`d2` (Julia's `human_dend` defaults).
pub fn TripodHet::new(
  n : Int,
  soma_param : AdExParameterHet,
  rng : Xoshiro,
) -> TripodHet {
  // Per-neuron c[i] = tm[i] / r[i] (matches Julia: C = tm / R, with
  // tm in ms and R in MΩ giving C in pF when multiplied by 1e-3
  // inside the dv formula).
  let c : Array[Float] = Array::make(n, 0.0F)
  let gl : Array[Float] = Array::make(n, 0.0F)
  for k in 0..<n {
    // tm in ms; r in MΩ (= 1/nS when 1/r is g_L). Julia AdEx uses
    // `gl = 1/r` so c = tm/r = tm*gl. Mirror Tripod convention.
    c[k] = soma_param.tm[k] * (1.0F / soma_param.r[k])
    gl[k] = 1.0F / soma_param.r[k]
  }
  // Soma initial v_s in [vr, vt] per neuron.
  let v_s : Array[Float] = Array::make(n, 0.0F)
  let v_d1 : Array[Float] = Array::make(n, 0.0F)
  let v_d2 : Array[Float] = Array::make(n, 0.0F)
  for k in 0..<n {
    let spread = soma_param.vt[k] - soma_param.vr[k]
    v_s[k] = soma_param.vr[k] + next_f32(rng) * spread
    v_d1[k] = soma_param.vr[k] + next_f32(rng) * spread
    v_d2[k] = soma_param.vr[k] + next_f32(rng) * spread
  }
  let w_s : Array[Float] = Array::make(n, 0.0F)
  let fire : Array[Bool] = Array::make(n, false)
  let threshold : Array[Float] = Array::make(n, 0.0F)
  for k in 0..<n {
    threshold[k] = soma_param.vt[k]
  }
  let tabs : Array[Int] = Array::make(n, 1)
  let i_s : Array[Float] = Array::make(n, 0.0F)
  let i_d1 : Array[Float] = Array::make(n, 0.0F)
  let i_d2 : Array[Float] = Array::make(n, 0.0F)
  let ge_s : Array[Float] = Array::make(n, 0.0F)
  let gi_s : Array[Float] = Array::make(n, 0.0F)
  let ge_d1 : Array[Float] = Array::make(n, 0.0F)
  let gi_d1 : Array[Float] = Array::make(n, 0.0F)
  let ge_d2 : Array[Float] = Array::make(n, 0.0F)
  let gi_d2 : Array[Float] = Array::make(n, 0.0F)
  let glu_s : Array[Float] = Array::make(n, 0.0F)
  let gaba_s : Array[Float] = Array::make(n, 0.0F)
  let glu_d1 : Array[Float] = Array::make(n, 0.0F)
  let gaba_d1 : Array[Float] = Array::make(n, 0.0F)
  let glu_d2 : Array[Float] = Array::make(n, 0.0F)
  let gaba_d2 : Array[Float] = Array::make(n, 0.0F)
  let total4 = n * 4
  let dv : Array[Float] = Array::make(total4, 0.0F)
  let dv_temp : Array[Float] = Array::make(total4, 0.0F)
  let syn_curr_s : Array[Float] = Array::make(n, 0.0F)
  let syn_curr_d1 : Array[Float] = Array::make(n, 0.0F)
  let syn_curr_d2 : Array[Float] = Array::make(n, 0.0F)
  let d1 = Dendrite::new(n)
  let d2 = Dendrite::new(n)
  {
    soma_param,
    soma_spike: AdExPostSpike::new(),
    c, gl,
    d1, d2,
    i_s, i_d1, i_d2,
    ge_s, gi_s, ge_d1, gi_d1, ge_d2, gi_d2,
    glu_s, gaba_s, glu_d1, gaba_d1, glu_d2, gaba_d2,
    e_e: 0.0F, e_i: -75.0F,
    tau_e: 6.0F, tau_i: 2.0F,
    gsyn_e: 1.0F, gsyn_i: 1.0F,
    n, v_s, w_s, v_d1, v_d2, fire, threshold, tabs,
    dv, dv_temp,
    syn_curr_s, syn_curr_d1, syn_curr_d2,
  }
}

///|
/// Single-exp synapse step for the soma (same as Tripod).
pub fn tripod_het_soma_step_synapses(p : TripodHet, dt : Float) -> Unit {
  let n = p.n
  for i in 0..<n {
    p.ge_s[i] = p.ge_s[i] + p.glu_s[i]
    p.gi_s[i] = p.gi_s[i] + p.gaba_s[i]
    p.ge_s[i] = p.ge_s[i] + dt * (-p.ge_s[i] / p.tau_e)
    p.gi_s[i] = p.gi_s[i] + dt * (-p.gi_s[i] / p.tau_i)
  }
  for i in 0..<n {
    p.glu_s[i] = 0.0F
    p.gaba_s[i] = 0.0F
  }
}

///|
/// Single-exp synapse step for both dendrites (same as Tripod).
pub fn tripod_het_dend_step_synapses(p : TripodHet, dt : Float) -> Unit {
  let n = p.n
  for i in 0..<n {
    p.ge_d1[i] = p.ge_d1[i] + p.glu_d1[i]
    p.gi_d1[i] = p.gi_d1[i] + p.gaba_d1[i]
    p.ge_d1[i] = p.ge_d1[i] + dt * (-p.ge_d1[i] / p.tau_e)
    p.gi_d1[i] = p.gi_d1[i] + dt * (-p.gi_d1[i] / p.tau_i)
    p.ge_d2[i] = p.ge_d2[i] + p.glu_d2[i]
    p.gi_d2[i] = p.gi_d2[i] + p.gaba_d2[i]
    p.ge_d2[i] = p.ge_d2[i] + dt * (-p.ge_d2[i] / p.tau_e)
    p.gi_d2[i] = p.gi_d2[i] + dt * (-p.gi_d2[i] / p.tau_i)
  }
  for i in 0..<n {
    p.glu_d1[i] = 0.0F
    p.gaba_d1[i] = 0.0F
    p.glu_d2[i] = 0.0F
    p.gaba_d2[i] = 0.0F
  }
}

///|
/// Synaptic current into soma (single-exp synapse with reversal potentials).
pub fn tripod_het_syn_curr_soma(p : TripodHet) -> Unit {
  let n = p.n
  for i in 0..<n {
    p.syn_curr_s[i] = p.ge_s[i] * (p.v_s[i] - p.e_e) * p.gsyn_e +
                      p.gi_s[i] * (p.v_s[i] - p.e_i) * p.gsyn_i
  }
}

///|
/// Synaptic current into both dendrites (passive dendrite with synapse).
pub fn tripod_het_syn_curr_dends(p : TripodHet) -> Unit {
  let n = p.n
  for i in 0..<n {
    p.syn_curr_d1[i] = p.ge_d1[i] * (p.v_d1[i] - p.e_e) * p.gsyn_e +
                       p.gi_d1[i] * (p.v_d1[i] - p.e_i) * p.gsyn_i
    p.syn_curr_d2[i] = p.ge_d2[i] * (p.v_d2[i] - p.e_e) * p.gsyn_e +
                       p.gi_d2[i] * (p.v_d2[i] - p.e_i) * p.gsyn_i
  }
}

///|
/// Heun predictor-corrector for TripodHet (per-neuron AdEx params).
/// `store_temp = false` → predictor pass (writes p.dv).
/// `store_temp = true`  → corrector pass (writes p.dv_temp using p.dv_temp for the predictor extrapolation).
fn tripod_het_heun_step(p : TripodHet, dt : Float, store_temp : Bool) -> Unit {
  let n = p.n
  let mut k = 0
  while k < n {
    let vt = p.soma_param.vt[k]
    let el = p.soma_param.el[k]
    let tm = p.soma_param.tm[k]
    let r_val = p.soma_param.r[k]
    let dt_slope = p.soma_param.dt_slope[k]
    let tw = p.soma_param.tw[k]
    let a = p.soma_param.a[k]
    let c_val = p.c[k]
    let gl_val = p.gl[k]

    let ds : Float = if store_temp { p.dv_temp[k * 4 + 0] * dt } else { 0.0F }
    let dd1 : Float = if store_temp { p.dv_temp[k * 4 + 1] * dt } else { 0.0F }
    let dd2 : Float = if store_temp { p.dv_temp[k * 4 + 2] * dt } else { 0.0F }
    let dw : Float = if store_temp { p.dv_temp[k * 4 + 3] } else { 0.0F }

    let ic1 = -(p.v_d1[k] + dd1 - p.v_s[k] - ds) * p.d1.gax[k]
    let ic2 = -(p.v_d2[k] + dd2 - p.v_s[k] - ds) * p.d2.gax[k]

    let exp_term = if dt_slope < 0.0F {
      0.0F
    } else {
      dt_slope * expf((p.v_s[k] + ds - p.threshold[k]) / dt_slope)
    }
    let dv_s_val = (gl_val * (el - p.v_s[k] - ds) + exp_term -
                    p.w_s[k] - dw - p.syn_curr_s[k] - (ic1 + ic2) +
                    p.i_s[k]) / c_val

    let dv_d1_val = ((el - p.v_d1[k] - dd1) * p.d1.gm[k] -
                     p.syn_curr_d1[k] + ic1 + p.i_d1[k]) / p.d1.c[k]
    let dv_d2_val = ((el - p.v_d2[k] - dd2) * p.d2.gm[k] -
                     p.syn_curr_d2[k] + ic2 + p.i_d2[k]) / p.d2.c[k]

    let dw_val = (a * (p.v_s[k] + ds - el) - (p.w_s[k] + dw)) / tw

    if store_temp {
      p.dv_temp[k * 4 + 0] = dv_s_val
      p.dv_temp[k * 4 + 1] = dv_d1_val
      p.dv_temp[k * 4 + 2] = dv_d2_val
      p.dv_temp[k * 4 + 3] = dw_val
    } else {
      p.dv[k * 4 + 0] = dv_s_val
      p.dv[k * 4 + 1] = dv_d1_val
      p.dv[k * 4 + 2] = dv_d2_val
      p.dv[k * 4 + 3] = dw_val
    }
    let _ = vt
    let _ = tm
    let _ = r_val
    k = k + 1
  }
}

///|
/// Update TripodHet for one timestep. Same Julia `integrate!` order
/// as `step_tripod`, but reads per-neuron AdEx params.
pub fn step_tripod_het(p : TripodHet, dt : Float) -> Unit {
  let n = p.n
  let at = p.soma_spike.at
  let tau_a = p.soma_spike.tau_a
  let tabs_const = p.soma_spike.tabs_const
  let up = p.soma_spike.up
  let ap_membrane = p.soma_spike.ap_membrane
  let tabs_steps : Int = ((up + tabs_const) / dt).to_int()

  // 1. Synapses.
  tripod_het_soma_step_synapses(p, dt)
  tripod_het_dend_step_synapses(p, dt)

  // 2. Synaptic currents.
  tripod_het_syn_curr_soma(p)
  tripod_het_syn_curr_dends(p)

  // 3. Heun integration.
  tripod_het_heun_step(p, dt, false)
  for i in 0..<(n * 4) {
    p.dv_temp[i] = p.dv[i]
  }
  tripod_het_heun_step(p, dt, true)

  // 4 + 5. Apply + spike detection (Julia's exact per-neuron order).
  for k in 0..<n {
    let vr_k = p.soma_param.vr[k]
    let b_k = p.soma_param.b[k]
    p.tabs[k] = p.tabs[k] - 1
    p.threshold[k] = p.threshold[k] +
      dt * (p.soma_param.vt[k] - p.threshold[k]) / tau_a

    if p.tabs[k] > 0 {
      // Refractory: v_s = Vr, v_d += axial coupling.
      p.v_s[k] = vr_k
      p.v_d1[k] = p.v_d1[k] + dt * (p.v_s[k] - p.v_d1[k]) * p.d1.gax[k] / p.d1.c[k]
      p.v_d2[k] = p.v_d2[k] + dt * (p.v_s[k] - p.v_d2[k]) * p.d2.gax[k] / p.d2.c[k]
      continue
    }

    // Active phase. Detect fire BEFORE Heun apply.
    let v_s_pred = p.v_s[k] + p.dv[k * 4 + 0] * dt
    p.fire[k] = v_s_pred >= -10.0F

    if p.fire[k] {
      p.dv[k * 4 + 0] = ap_membrane - p.v_s[k]
      p.v_s[k] = ap_membrane
      p.w_s[k] = p.w_s[k] + b_k
      p.threshold[k] = p.threshold[k] + at
      p.tabs[k] = tabs_steps
      continue
    }

    // No spike: apply Heun correction.
    p.v_s[k] = p.v_s[k] + 0.5F * dt * (p.dv[k * 4 + 0] + p.dv_temp[k * 4 + 0])
    p.v_d1[k] = p.v_d1[k] + 0.5F * dt * (p.dv[k * 4 + 1] + p.dv_temp[k * 4 + 1])
    p.v_d2[k] = p.v_d2[k] + 0.5F * dt * (p.dv[k * 4 + 2] + p.dv_temp[k * 4 + 2])
    p.w_s[k] = p.w_s[k] + 0.5F * dt * (p.dv[k * 4 + 3] + p.dv_temp[k * 4 + 3])
  }
}