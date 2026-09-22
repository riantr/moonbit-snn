// Tripod — Tripod multi-compartment neuron: AdEx soma + 2 passive
// dendritic compartments (d1, d2). Bit-exact port of
// SNNModels.jl/src/populations/multicompartment/tripod.jl.
//
// Structure:
//   - Soma: AdEx neuron (v_s, w_s, fire, threshold, tabs, etc.)
//   - 2 dendrites: passive RC compartments (each with own Dendrite)
//   - Axial currents: gax between soma and each dendrite
//   - Heun integration (predictor-corrector) for numerical stability
//
// Bit-exact ordering: same as BallAndStick but with one extra
// dendrite Δv component. Per neuron we have 4 Δv values:
//   dv[k*4 + 0] = dv_s    (soma)
//   dv[k*4 + 1] = dv_d1   (dendrite 1)
//   dv[k*4 + 2] = dv_d2   (dendrite 2)
//   dv[k*4 + 3] = dw_s    (soma adaptation current)
// dv length = n * 4.

///|
/// Tripod neuron state — soma (AdEx) + 2 dendrites (d1, d2).
pub struct Tripod {
  // Soma (AdEx) parameters.
  soma_param : AdExParameter
  soma_spike : AdExPostSpike
  // Dendrite passive parameters (d1 + d2).
  d1 : Dendrite
  d2 : Dendrite
  // External input currents.
  i_s : Array[Float]  // soma
  i_d1 : Array[Float] // dendrite 1
  i_d2 : Array[Float] // dendrite 2
  // Synaptic conductances (simple single-exp on soma + 2 dendrites).
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
  // Synapse parameters.
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
  // Temporary variables for Heun integration (n*4 per neuron).
  dv : Array[Float]
  dv_temp : Array[Float]
  syn_curr_s : Array[Float]
  syn_curr_d1 : Array[Float]
  syn_curr_d2 : Array[Float]
}

///|
/// Default Tripod: AdEx soma + 2 Dendrites with human_dend defaults.
pub fn Tripod::new(
  n : Int,
  soma_param : AdExParameter,
  rng : Xoshiro,
) -> Tripod {
  // Soma initial v_s in [vr, vt].
  let v_s = Array::make(n, 0.0F)
  let spread = soma_param.vt - soma_param.vr
  for k in 0..<n {
    v_s[k] = soma_param.vr + next_f32(rng) * spread
  }
  let w_s : Array[Float] = Array::make(n, 0.0F)
  // Dendrites initial v_d1/v_d2 also in [vr, vt].
  let v_d1 = Array::make(n, 0.0F)
  let v_d2 = Array::make(n, 0.0F)
  for k in 0..<n {
    v_d1[k] = soma_param.vr + next_f32(rng) * spread
    v_d2[k] = soma_param.vr + next_f32(rng) * spread
  }
  let fire : Array[Bool] = Array::make(n, false)
  let threshold : Array[Float] = Array::make(n, soma_param.vt)
  let tabs : Array[Int] = Array::make(n, 1)
  // External inputs.
  let i_s : Array[Float] = Array::make(n, 0.0F)
  let i_d1 : Array[Float] = Array::make(n, 0.0F)
  let i_d2 : Array[Float] = Array::make(n, 0.0F)
  // Synaptic conductances.
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
  // Heun temp arrays (4 * n).
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
/// Single-exp synapse step for the soma.
pub fn tripod_soma_step_synapses(p : Tripod, dt : Float) -> Unit {
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
/// Single-exp synapse step for both dendrites.
pub fn tripod_dend_step_synapses(p : Tripod, dt : Float) -> Unit {
  let n = p.n
  // dendrite 1.
  for i in 0..<n {
    p.ge_d1[i] = p.ge_d1[i] + p.glu_d1[i]
    p.gi_d1[i] = p.gi_d1[i] + p.gaba_d1[i]
    p.ge_d1[i] = p.ge_d1[i] + dt * (-p.ge_d1[i] / p.tau_e)
    p.gi_d1[i] = p.gi_d1[i] + dt * (-p.gi_d1[i] / p.tau_i)
  }
  for i in 0..<n {
    p.glu_d1[i] = 0.0F
    p.gaba_d1[i] = 0.0F
  }
  // dendrite 2.
  for i in 0..<n {
    p.ge_d2[i] = p.ge_d2[i] + p.glu_d2[i]
    p.gi_d2[i] = p.gi_d2[i] + p.gaba_d2[i]
    p.ge_d2[i] = p.ge_d2[i] + dt * (-p.ge_d2[i] / p.tau_e)
    p.gi_d2[i] = p.gi_d2[i] + dt * (-p.gi_d2[i] / p.tau_i)
  }
  for i in 0..<n {
    p.glu_d2[i] = 0.0F
    p.gaba_d2[i] = 0.0F
  }
}

///|
/// Synaptic current: soma = ge*(v-E_e)*gsyn + gi*(v-E_i)*gsyn.
pub fn tripod_syn_curr_soma(p : Tripod) -> Unit {
  let n = p.n
  for i in 0..<n {
    p.syn_curr_s[i] = p.ge_s[i] * (p.v_s[i] - p.e_e) * p.gsyn_e +
                      p.gi_s[i] * (p.v_s[i] - p.e_i) * p.gsyn_i
  }
}

///|
/// Synaptic current: dendrite1 and dendrite2.
pub fn tripod_syn_curr_dends(p : Tripod) -> Unit {
  let n = p.n
  for i in 0..<n {
    p.syn_curr_d1[i] = p.ge_d1[i] * (p.v_d1[i] - p.e_e) * p.gsyn_e +
                       p.gi_d1[i] * (p.v_d1[i] - p.e_i) * p.gsyn_i
    p.syn_curr_d2[i] = p.ge_d2[i] * (p.v_d2[i] - p.e_e) * p.gsyn_e +
                       p.gi_d2[i] * (p.v_d2[i] - p.e_i) * p.gsyn_i
  }
}

///|
/// Heun predictor step: compute Δv (v_s, v_d1, v_d2, w_s) per neuron.
/// Bit-exact port of Julia's `update_neuron!` for Tripod.
///
/// Note on units (matches Julia): `dv` is per-ms (dV/dt). For the
/// corrector pass we extrapolate `v + dv * dt` for soma and dendrite
/// voltages. The adaptation `dw` is NOT multiplied by dt in Julia's
/// source (pre-existing quirk); we match it bit-exactly.
fn tripod_heun_step(p : Tripod, dt : Float, store_temp : Bool) -> Unit {
  let n = p.n
  let p_ = p.soma_param
  let c = p_.c
  let gl = p_.gl
  let el = p_.el
  let dt_slope = p_.dt_slope
  let tw = p_.tw
  let a = p_.a
  let mut k = 0
  while k < n {
    // Read predictor's Δv (or 0 for predictor pass).
    // ds/dd1/dd2 multiplied by dt (soma + dendrite voltages).
    // dw NOT multiplied by dt (Julia quirk).
    let ds : Float = if store_temp { p.dv_temp[k * 4 + 0] * dt } else { 0.0F }
    let dd1 : Float = if store_temp { p.dv_temp[k * 4 + 1] * dt } else { 0.0F }
    let dd2 : Float = if store_temp { p.dv_temp[k * 4 + 2] * dt } else { 0.0F }
    let dw : Float = if store_temp { p.dv_temp[k * 4 + 3] } else { 0.0F }

    // Axial currents: ic_d1 = -(v_d1 + dd1 - v_s - ds) * gax_d1
    //                 ic_d2 = -(v_d2 + dd2 - v_s - ds) * gax_d2
    let ic1 = -(p.v_d1[k] + dd1 - p.v_s[k] - ds) * p.d1.gax[k]
    let ic2 = -(p.v_d2[k] + dd2 - p.v_s[k] - ds) * p.d2.gax[k]

    // Soma Δv (sum of both axial currents; Julia uses `sum(ic)`).
    let exp_term = if dt_slope < 0.0F {
      0.0F
    } else {
      dt_slope * expf((p.v_s[k] + ds - p.threshold[k]) / dt_slope)
    }
    let dv_s_val = (gl * (el - p.v_s[k] - ds) + exp_term -
                    p.w_s[k] - dw - p.syn_curr_s[k] - (ic1 + ic2) +
                    p.i_s[k]) / c

    // Dendrite 1 Δv: ((El - v_d1 - dd1)*gm_d1 - syn_curr_d1 + ic1 + i_d1) / C_d1
    let dv_d1_val = ((el - p.v_d1[k] - dd1) * p.d1.gm[k] -
                     p.syn_curr_d1[k] + ic1 + p.i_d1[k]) / p.d1.c[k]
    // Dendrite 2 Δv: same with ic2.
    let dv_d2_val = ((el - p.v_d2[k] - dd2) * p.d2.gm[k] -
                     p.syn_curr_d2[k] + ic2 + p.i_d2[k]) / p.d2.c[k]

    // Adaptation Δw.
    let dw_val = (a * (p.v_s[k] + ds - el) - (p.w_s[k] + dw)) / tw

    // Write.
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
    k = k + 1
  }
}

///|
/// Update Tripod for one timestep.
/// Mirrors Julia's Tripod `integrate!` order bit-exactly:
///   1. update_synapses! (soma + both dendrites)
///   2. synaptic_current! (soma + both dendrites)
///   3. Heun: predictor → save → corrector → save
///   4. for each neuron:
///      - decrement tabs, update threshold
///      - if tabs > 0 (refractory): v_s=Vr, v_d1,v_d2 += dt*axial
///      - else (active):
///          * detect fire (predictive): v_s + corrector_dv*dt >= -10mV
///          * if fire: v_s=AP, w+=b, θ+=At, tabs=tabs_steps, continue
///          * else: apply Heun correction to v_s, v_d1, v_d2, w_s
///
/// See `step_ballandstick` doc for why we must skip v_d apply on
/// fire (corrector exp_term can explode when v_s is near/above
/// threshold; Julia's `fire && continue` prevents v_d runaway).
///
/// `tabs_steps = round(Int, (up + τabs) / dt)` — Julia's full
/// backprop + refractory duration. Using only `τabs` gives half.
pub fn step_tripod(p : Tripod, dt : Float) -> Unit {
  let n = p.n
  let p_ = p.soma_param
  let vt = p_.vt
  let vr = p_.vr
  let b = p_.b
  let at = p.soma_spike.at
  let tau_a = p.soma_spike.tau_a
  let tabs_const = p.soma_spike.tabs_const
  let up = p.soma_spike.up
  let ap_membrane = p.soma_spike.ap_membrane
  let tabs_steps : Int = ((up + tabs_const) / dt).to_int()

  // 1. Synapses.
  tripod_soma_step_synapses(p, dt)
  tripod_dend_step_synapses(p, dt)

  // 2. Synaptic currents.
  tripod_syn_curr_soma(p)
  tripod_syn_curr_dends(p)

  // 3. Heun integration.
  tripod_heun_step(p, dt, false)
  for i in 0..<(n * 4) {
    p.dv_temp[i] = p.dv[i]
  }
  tripod_heun_step(p, dt, true)

  // 4 + 5. Apply + spike detection (Julia's exact per-neuron order).
  for k in 0..<n {
    p.tabs[k] = p.tabs[k] - 1
    p.threshold[k] = p.threshold[k] + dt * (vt - p.threshold[k]) / tau_a

    if p.tabs[k] > 0 {
      // Refractory: v_s = Vr, v_d += axial coupling.
      p.v_s[k] = vr
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
      p.w_s[k] = p.w_s[k] + b
      p.threshold[k] = p.threshold[k] + at
      p.tabs[k] = tabs_steps
      // Skip Heun apply (avoid corrector dv_d runaway).
      continue
    }

    // No spike: apply Heun correction.
    p.v_s[k] = p.v_s[k] + 0.5F * dt * (p.dv[k * 4 + 0] + p.dv_temp[k * 4 + 0])
    p.v_d1[k] = p.v_d1[k] + 0.5F * dt * (p.dv[k * 4 + 1] + p.dv_temp[k * 4 + 1])
    p.v_d2[k] = p.v_d2[k] + 0.5F * dt * (p.dv[k * 4 + 2] + p.dv_temp[k * 4 + 2])
    p.w_s[k] = p.w_s[k] + 0.5F * dt * (p.dv[k * 4 + 3] + p.dv_temp[k * 4 + 3])
  }
}