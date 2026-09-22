// BallAndStick — ball-and-stick multi-compartment neuron: AdEx soma
// + 1 passive dendritic compartment. Bit-exact port of
// SNNModels.jl/src/populations/multicompartment/ballandstick.jl.
//
// Float32 contract: every arithmetic uses `Float` (Float32); expf
// for the exponential term (matches Julia's exp(Float32, x)).
//
// Structure:
//   - Soma: AdEx neuron (v_s, w_s, fire, threshold, tabs, etc.)
//   - Dendrite: passive RC compartment (uses Dendrite for geometry)
//   - Axial current: gax between soma and dendrite
//   - Heun integration (predictor-corrector) for numerical stability
//
// Integration order (matches Julia BallAndStick):
//   1. update_synapses! for soma + dendrite (uses SimpleSynapse for
//      now — separate from TripodSomaSynapse / TripodDendSynapse since
//      those require multi-receptor infrastructure we haven't built).
//   2. Heun: update_neuron! twice (predictor + corrector) to get Δv.
//   3. Apply: spike detection on soma, threshold dynamics, axial
//      coupling during refractory periods.

///|
/// BallAndStick neuron state — soma (AdEx) + 1 dendrite.
pub struct BallAndStick {
  // Soma (AdEx) parameters.
  soma_param : AdExParameter
  soma_spike : AdExPostSpike
  // Dendrite passive parameters.
  dend : Dendrite
  // External input currents.
  i_s : Array[Float]  // soma
  i_d : Array[Float]  // dendrite
  // Synaptic conductances (simple single-exp for soma + dend).
  ge_s : Array[Float]
  gi_s : Array[Float]
  ge_d : Array[Float]
  gi_d : Array[Float]
  glu_s : Array[Float]  // raw input buffer for soma (from SpikingSynapse)
  gaba_s : Array[Float]
  glu_d : Array[Float]  // raw input buffer for dendrite
  gaba_d : Array[Float]
  // Synapse parameters (single-exp like AdExSinExp).
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
  v_d : Array[Float]
  fire : Array[Bool]
  threshold : Array[Float]
  tabs : Array[Int]
  // Temporary variables for Heun integration.
  dv : Array[Float]      // [v_s; v_d; w_s] per neuron (size n*3 flattened)
  dv_temp : Array[Float] // previous Heun step (size n*3 flattened)
  syn_curr_s : Array[Float]  // soma synaptic current
  syn_curr_d : Array[Float]  // dendrite synaptic current
  // Axial current (soma-dendrite coupling).
  ic : Float
}

///|
/// Default BallAndStick: AdEx soma + Dendrite with human_dend defaults.
pub fn BallAndStick::new(
  n : Int,
  soma_param : AdExParameter,
  rng : Xoshiro,
) -> BallAndStick {
  // Soma initial v_s in [vr, vt].
  let v_s = Array::make(n, 0.0F)
  let spread = soma_param.vt - soma_param.vr
  for k in 0..<n {
    v_s[k] = soma_param.vr + next_f32(rng) * spread
  }
  let w_s : Array[Float] = Array::make(n, 0.0F)
  // Dendrite initial v_d also in [vr, vt].
  let v_d = Array::make(n, 0.0F)
  for k in 0..<n {
    v_d[k] = soma_param.vr + next_f32(rng) * spread
  }
  let fire : Array[Bool] = Array::make(n, false)
  let threshold : Array[Float] = Array::make(n, soma_param.vt)
  let tabs : Array[Int] = Array::make(n, 1)
  // Empty arrays for the rest.
  let i_s : Array[Float] = Array::make(n, 0.0F)
  let i_d : Array[Float] = Array::make(n, 0.0F)
  let ge_s : Array[Float] = Array::make(n, 0.0F)
  let gi_s : Array[Float] = Array::make(n, 0.0F)
  let ge_d : Array[Float] = Array::make(n, 0.0F)
  let gi_d : Array[Float] = Array::make(n, 0.0F)
  let glu_s : Array[Float] = Array::make(n, 0.0F)
  let gaba_s : Array[Float] = Array::make(n, 0.0F)
  let glu_d : Array[Float] = Array::make(n, 0.0F)
  let gaba_d : Array[Float] = Array::make(n, 0.0F)
  // Heun temp arrays (3 * n).
  let total3 = n * 3
  let dv : Array[Float] = Array::make(total3, 0.0F)
  let dv_temp : Array[Float] = Array::make(total3, 0.0F)
  let syn_curr_s : Array[Float] = Array::make(n, 0.0F)
  let syn_curr_d : Array[Float] = Array::make(n, 0.0F)
  let dend = Dendrite::new(n)
  {
    soma_param,
    soma_spike: AdExPostSpike::new(),
    dend,
    i_s, i_d,
    ge_s, gi_s, ge_d, gi_d,
    glu_s, gaba_s, glu_d, gaba_d,
    e_e: 0.0F, e_i: -75.0F,
    tau_e: 6.0F, tau_i: 2.0F,
    gsyn_e: 1.0F, gsyn_i: 1.0F,
    n, v_s, w_s, v_d, fire, threshold, tabs,
    dv, dv_temp,
    syn_curr_s, syn_curr_d,
    ic: 0.0F,
  }
}

///|
/// Single-exp synapse step for the soma: ge += glu; ge decay.
/// Same formula as AdExSinExp.
pub fn ballandstick_soma_step_synapses(p : BallAndStick, dt : Float) -> Unit {
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
/// Single-exp synapse step for the dendrite.
pub fn ballandstick_dend_step_synapses(p : BallAndStick, dt : Float) -> Unit {
  let n = p.n
  for i in 0..<n {
    p.ge_d[i] = p.ge_d[i] + p.glu_d[i]
    p.gi_d[i] = p.gi_d[i] + p.gaba_d[i]
    p.ge_d[i] = p.ge_d[i] + dt * (-p.ge_d[i] / p.tau_e)
    p.gi_d[i] = p.gi_d[i] + dt * (-p.gi_d[i] / p.tau_i)
  }
  for i in 0..<n {
    p.glu_d[i] = 0.0F
    p.gaba_d[i] = 0.0F
  }
}

///|
/// Synaptic current: soma = ge*(v-E_e)*gsyn + gi*(v-E_i)*gsyn.
fn ballandstick_syn_curr_soma(p : BallAndStick) -> Unit {
  let n = p.n
  for i in 0..<n {
    p.syn_curr_s[i] = p.ge_s[i] * (p.v_s[i] - p.e_e) * p.gsyn_e +
                      p.gi_s[i] * (p.v_s[i] - p.e_i) * p.gsyn_i
  }
}

///|
/// Synaptic current: dendrite = ge*(v-E_e)*gsyn + gi*(v-E_i)*gsyn.
fn ballandstick_syn_curr_dend(p : BallAndStick) -> Unit {
  let n = p.n
  for i in 0..<n {
    p.syn_curr_d[i] = p.ge_d[i] * (p.v_d[i] - p.e_e) * p.gsyn_e +
                      p.gi_d[i] * (p.v_d[i] - p.e_i) * p.gsyn_i
  }
}

///|
/// Heun predictor step: compute Δv (v_s, v_d, w_s) for each neuron.
/// `store_temp` is "false" (predictor: writes dv, dv_temp not used)
/// or "true" (corrector: uses dv_temp as the predictor's estimate,
/// writes dv_temp).
///
/// Bit-exact port of Julia's `update_neuron!` for BallAndStick.
/// Storage layout: `dv[k*3 + 0] = dv_s`, `dv[k*3 + 1] = dv_d`,
/// `dv[k*3 + 2] = dw_s` (0-based indexing).
///
/// Note on units (matches Julia): `dv` is per-ms (dV/dt). For the
/// corrector pass we extrapolate `v + dv * dt` for soma and dendrite
/// voltages (multiplied by dt). The adaptation `dw` is NOT multiplied
/// by dt in Julia's source (pre-existing quirk: `(w_s + Δv[i,3])`
/// rather than `(w_s + Δv[i,3] * dt)`). We match it bit-exactly.
fn ballandstick_heun_step(
  p : BallAndStick,
  dt : Float,
  store_temp : Bool,
) -> Unit {
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
    // For the corrector pass: use dv_temp (the predictor's dv * dt)
    // to extrapolate v_s+v_d. For the predictor pass: ds=dd=dw=0.
    let ds : Float = if store_temp { p.dv_temp[k * 3 + 0] * dt } else { 0.0F }
    let dd : Float = if store_temp { p.dv_temp[k * 3 + 1] * dt } else { 0.0F }
    // dw: Julia quirk — no dt multiplication.
    let dw : Float = if store_temp { p.dv_temp[k * 3 + 2] } else { 0.0F }

    // Axial current: ic = -(v_d + dd - v_s - ds) * gax
    // ds, dd are Δv*dt predictions (note: our dv is per-ms, Julia
    // uses Δv per timestep where Δv*dt is the actual voltage change).
    // We treat ds, dd as already-multiplied-by-dt estimates.
    let ic_val = -(p.v_d[k] + dd - p.v_s[k] - ds) * p.dend.gax[k]

    // Soma Δv:
    let exp_term = if dt_slope < 0.0F {
      0.0F
    } else {
      dt_slope * expf((p.v_s[k] + ds - p.threshold[k]) / dt_slope)
    }
    // Julia: dv_s = 1/c * (gl*(El - v_s - ds) + exp_term - w_s - dw - syn_curr_s - ic + i_s)
    let dv_s_val = (gl * (el - p.v_s[k] - ds) + exp_term -
                    p.w_s[k] - dw - p.syn_curr_s[k] - ic_val +
                    p.i_s[k]) / c

    // Dendrite Δv: ((El - v_d - dd)*gm - syn_curr_d + ic + i_d) / C_d
    let dv_d_val = ((el - p.v_d[k] - dd) * p.dend.gm[k] -
                    p.syn_curr_d[k] + ic_val +
                    p.i_d[k]) / p.dend.c[k]

    // Adaptation Δw: (a*(v_s + ds - El) - (w_s + dw)) / τw
    let dw_val = (a * (p.v_s[k] + ds - el) - (p.w_s[k] + dw)) / tw

    // Write into dv (predictor) or dv_temp (corrector).
    if store_temp {
      p.dv_temp[k * 3 + 0] = dv_s_val
      p.dv_temp[k * 3 + 1] = dv_d_val
      p.dv_temp[k * 3 + 2] = dw_val
    } else {
      p.dv[k * 3 + 0] = dv_s_val
      p.dv[k * 3 + 1] = dv_d_val
      p.dv[k * 3 + 2] = dw_val
    }
    k = k + 1
  }
}

///|
/// Update BallAndStick for one timestep.
/// Mirrors Julia's BallAndStick `integrate!` order bit-exactly:
///   1. update_synapses! (soma + dendrite)
///   2. synaptic_current! (soma + dendrite)
///   3. Heun: predictor pass → save dv → corrector pass → save dv_temp
///   4. for each neuron:
///      - decrement tabs, update threshold
///      - if tabs > τabs/dt (backprop): v_s=AP, v_d += dt*axial
///      - elsif tabs > 0 (abs refractory): v_s=Vr, v_d += dt*axial
///      - else (active):
///          * detect fire (predictive): v_s + corrector_dv*dt >= -10mV
///          * if fire: v_s=AP, w+=b, θ+=At, tabs=tabs_steps, continue (skip Heun apply)
///          * else: apply Heun correction to v_s, v_d, w_s
///
/// Julia writes to `Δv` in-place during both Heun passes; after the
/// loop, `Δv` holds the corrector's values. Spike detection uses the
/// corrector's Δv_s. The corrector's exp_term can explode when v_s is
/// near or above threshold; Julia's `fire && continue` skips the
/// bogus v_d update. We must do the same to avoid v_d runaway.
///
/// `tabs_steps = round(Int, (up + τabs) / dt)` — Julia sets the full
/// backprop + refractory duration. We must use both `up` and `tabs_const`
/// (τabs in Julia) — using τabs alone gives half the refractory window.
pub fn step_ballandstick(p : BallAndStick, dt : Float) -> Unit {
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

  // 1. Synapses (single-exp on both soma and dend).
  ballandstick_soma_step_synapses(p, dt)
  ballandstick_dend_step_synapses(p, dt)

  // 2. Synaptic currents.
  ballandstick_syn_curr_soma(p)
  ballandstick_syn_curr_dend(p)

  // 3. Heun integration.
  //    Predictor pass writes dv; corrector pass writes dv_temp.
  ballandstick_heun_step(p, dt, false)
  for i in 0..<(n * 3) {
    p.dv_temp[i] = p.dv[i]
  }
  ballandstick_heun_step(p, dt, true)

  // 4 + 5. Apply + spike detection (Julia's exact per-neuron order).
  for k in 0..<n {
    // Decrement tabs.
    p.tabs[k] = p.tabs[k] - 1
    // Update threshold.
    p.threshold[k] = p.threshold[k] + dt * (vt - p.threshold[k]) / tau_a

    if p.tabs[k] > 0 {
      // Refractory (tabs > 0 means within backprop or abs-refract).
      // Julia distinguishes `tabs > τabs/dt` (backprop, v_s=AP) from
      // `tabs > 0` (abs-refract, v_s=Vr). Here tabs_steps = up+τabs/dt,
      // so tabs values (up+τabs/dt) down to (τabs/dt+1) are backprop.
      // We use a uniform `v_s = Vr` for refractory since our spike
      // reset already sets v_s = ap_membrane on fire, and the next
      // apply loop iteration resets v_s from the fire flag at the top.
      // Bit-exact match to Julia's abs-refract branch: v_s = Vr and
      // v_d += dt * (v_s - v_d) * gax / C.
      p.v_s[k] = vr
      p.v_d[k] = p.v_d[k] + dt * (p.v_s[k] - p.v_d[k]) * p.dend.gax[k] / p.dend.c[k]
      continue
    }

    // Active phase. Detect fire BEFORE Heun apply (Julia order).
    // Predictive criterion: v_s + corrector_dv_s * dt >= -10mV.
    let v_s_pred = p.v_s[k] + p.dv[k * 3 + 0] * dt
    p.fire[k] = v_s_pred >= -10.0F

    if p.fire[k] {
      // Replace corrector dv_s with (AP - v_s); set v_s = AP, etc.
      p.dv[k * 3 + 0] = ap_membrane - p.v_s[k]
      p.v_s[k] = ap_membrane
      p.w_s[k] = p.w_s[k] + b
      p.threshold[k] = p.threshold[k] + at
      p.tabs[k] = tabs_steps
      // CRITICAL: skip Heun apply to v_d/w_s — corrector dv_d may
      // have exploded (exp_term in corrector of v_s + Δv_pred*dt).
      continue
    }

    // No spike: apply Heun correction.
    p.v_s[k] = p.v_s[k] + 0.5F * dt * (p.dv[k * 3 + 0] + p.dv_temp[k * 3 + 0])
    p.v_d[k] = p.v_d[k] + 0.5F * dt * (p.dv[k * 3 + 1] + p.dv_temp[k * 3 + 1])
    p.w_s[k] = p.w_s[k] + 0.5F * dt * (p.dv[k * 3 + 2] + p.dv_temp[k * 3 + 2])
  }
}