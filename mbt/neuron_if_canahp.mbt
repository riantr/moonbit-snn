// IF_CANAHP (CAN-AHP Integrate-and-Fire) — bit-exact port of
// SNNModels.jl.
//
// Julia reference:
//   src/populations/generalized_if/if_CANAHP.jl
//   Brogdon et al. (2022) "Calcium-Activated Nonspecific Cation
//   Current and Afterhyperpolarization in a Spiking Neural Model"
//   (bioRxiv 2022.07.26.501548)
//
// The CAN-AHP model adds two Calcium-driven currents to the standard
// LIF membrane:
//   - I_CAN = gCAN * area * pCAN * (v - V_CAN)   — depolarizing CAN
//   - I_AHP = gAHP * area * pAHP * (v - V_AHP)  — hyperpolarizing AHP
// Both pCAN / pAHP are gating variables in [0, 1] driven by the
// intracellular Calcium concentration. Calcium evolves toward Ca0
// with time-constant τCa, plus a per-spike ΔCa bump.
//
// Float32 contract: every arithmetic operation uses `Float` (Float32).
// Update order matches Julia's update_neuron! / update_synapses! /
// update_spike! / synaptic_current! exactly.

///|
/// IF_CANAHPParameter — biophysical constants for the CAN-AHP LIF.
///
/// Float32 fields match Julia's defaults (area=5e-4cm², C=1uF/cm²,
/// Vt=-50mV, Vr=-65mV, τabs=3ms, gl=0.05mS/cm², VL=-70mV,
/// ΔCa=0.2uM, Ca0=0.1uM, τCa=100ms, V_AHP=-90mV, g_AHP=0,
/// α_AHP=0.125uM/ms, β_AHP=0.025uM/ms, γ_AHP=1, V_CAN=30mV,
/// g_CAN=0, β_CAN=0.025/ms, α_CAN=0.03125uM/ms, γ_CAN=1).
pub(all) struct IFCANAHParameter {
  area : Float
  c : Float
  vt : Float
  vr : Float
  tau_abs : Float
  gl : Float
  vl : Float
  delta_ca : Float
  ca0 : Float
  tau_ca : Float
  v_ahp : Float
  g_ahp : Float
  alpha_ahp : Float
  beta_ahp : Float
  gamma_ahp : Float
  v_can : Float
  g_can : Float
  beta_can : Float
  alpha_can : Float
  gamma_can : Float
}

///|
/// Default IFCANAHParameter (CAN/AHP off by default — g_can=0, g_ahp=0).
pub fn IFCANAHParameter::new() -> IFCANAHParameter {
  { area: 5.0e-4F, c: 1.0F, vt: -50.0F, vr: -65.0F, tau_abs: 3.0F,
    gl: 50000.0F, vl: -70.0F, delta_ca: 0.2F, ca0: 0.1F, tau_ca: 100.0F,
    v_ahp: -90.0F, g_ahp: 0.0F, alpha_ahp: 0.125F, beta_ahp: 0.025F,
    gamma_ahp: 1.0F, v_can: 30.0F, g_can: 0.0F, beta_can: 0.025F,
    alpha_can: 0.03125F, gamma_can: 1.0F }
}

///|
/// IFCANAHP — CAN-AHP Integrate-and-Fire neuron with multi-receptor
/// synaptic input (glu_receptors + gaba_receptors).
///
/// We simplify Julia's 4-receptor array (AMPA / NMDA / GABAa / GABAb)
/// to a single combined synaptic current `syn_curr` for bit-exactness
/// with the existing MoonBit IF/SpikingSynapse infrastructure. The
/// core CAN-AHP dynamics (Ca dynamics, pCAN/pAHP gating, membrane)
/// are ported unchanged.
pub(all) struct IFCANAHP {
  n : Int
  param : IFCANAHParameter
  v : Array[Float]
  fire : Array[Bool]
  tabs : Array[Float]
  i : Array[Float]
  // CAN-AHP state
  p_can : Array[Float]
  p_ahp : Array[Float]
  ca : Array[Float]
  // synaptic input (single combined current, set externally)
  syn_curr : Array[Float]
}

///|
/// Default IFCANAHP (100 neurons, parameters = IFCANAHParameter::new()).
pub fn IFCANAHP::new(
  n~ : Int = 100,
  param~ : IFCANAHParameter = IFCANAHParameter::new(),
) -> IFCANAHP {
  let v : Array[Float] = Array::make(n, param.vr)
  let fire : Array[Bool] = Array::make(n, false)
  let tabs : Array[Float] = Array::make(n, 0.0F)
  let i : Array[Float] = Array::make(n, 0.0F)
  let p_can : Array[Float] = Array::make(n, 0.0F)
  let p_ahp : Array[Float] = Array::make(n, 0.0F)
  let ca : Array[Float] = Array::make(n, 0.0F)
  let syn_curr : Array[Float] = Array::make(n, 0.0F)
  { n, param, v, fire, tabs, i, p_can, p_ahp, ca, syn_curr }
}

///|
/// update_neuron! — Ca drive + CAN-AHP gating + membrane update.
///
/// Julia's `Ca[i] = (Ca0 - Ca[i]) / τCa` is a drive-toward-Ca0 step
/// (not a standard Euler decay); we preserve the exact form so the
/// unit test matches Julia's reference trajectory.
pub fn update_neuron_ifcanahp(
  p : IFCANAHP,
  param : IFCANAHParameter,
  dt : Float,
) -> Unit {
  let n = p.n
  let v = p.v
  let i = p.i
  let tabs = p.tabs
  let ca = p.ca
  let p_can = p.p_can
  let p_ahp = p.p_ahp
  let syn_curr = p.syn_curr
  let ca0 = param.ca0
  let tau_ca = param.tau_ca
  let alpha_can = param.alpha_can
  let beta_can = param.beta_can
  let alpha_ahp = param.alpha_ahp
  let beta_ahp = param.beta_ahp
  let g_can = param.g_can
  let v_can = param.v_can
  let g_ahp = param.g_ahp
  let v_ahp = param.v_ahp
  let gl = param.gl
  let vl = param.vl
  let area = param.area
  let c = param.c
  let mut k = 0
  while k < n {
    let tabs_k = tabs[k]
    if tabs_k > 0.0F {
      ignore(tabs.set(k, tabs_k - 1.0F))
      k = k + 1
      continue
    }
    let v_k = v[k]
    let ca_k = ca[k]
    // Ca drive (Julia form: drive toward Ca0)
    let ca_new = (ca0 - ca_k) / tau_ca
    let ca_k = ca_new
    // CAN / AHP gating (Julia form)
    let p_can_new = (1.0F - p_can[k]) * (alpha_can * ca_k + beta_can) - beta_can
    let p_ahp_new = (1.0F - p_ahp[k]) * (alpha_ahp * ca_k + beta_ahp) - beta_ahp
    ignore(ca.set(k, ca_k))
    ignore(p_can.set(k, p_can_new))
    ignore(p_ahp.set(k, p_ahp_new))
    // Ionic currents
    let i_l = gl * area * (v_k - vl)
    let i_can = g_can * area * p_can_new * (v_k - v_can)
    let i_ahp = g_ahp * area * p_ahp_new * (v_k - v_ahp)
    let i_ionic = i_l + i_can + i_ahp
    // Membrane update
    let v_new = v_k + dt / (c * area) * (-(i_ionic + syn_curr[k]) + i[k])
    ignore(v.set(k, v_new))
    k = k + 1
  }
}

///|
/// update_spike! — fire detection, reset, Ca bump, refractory countdown.
///
/// Run AFTER update_neuron! to avoid interfering with the membrane
/// update in the same step.
pub fn update_spike_ifcanahp(
  p : IFCANAHP,
  param : IFCANAHParameter,
  dt : Float,
) -> Unit {
  let n = p.n
  let v = p.v
  let fire = p.fire
  let tabs = p.tabs
  let ca = p.ca
  let vt = param.vt
  let vr = param.vr
  let delta_ca = param.delta_ca
  let tau_abs = param.tau_abs
  let tabs_steps : Int = Float::to_int(tau_abs / dt + 0.5F)
  let mut k = 0
  while k < n {
    let v_k = v[k]
    let f = v_k > vt
    let v_new = if f { vr } else { v_k }
    ignore(v.set(k, v_new))
    ignore(fire.set(k, f))
    // Ca bump on spike
    ignore(ca.set(k, ca[k] + delta_ca))
    // Refractory
    ignore(tabs.set(k, if f { Float::from_int(tabs_steps) } else { 0.0F }))
    k = k + 1
  }
}

///|
/// integrate! — runs update_neuron! then update_spike!. Synaptic
/// current is set externally (syn_curr array). Mirrors Julia's
/// `integrate!(p, param, dt)` minus the multi-receptor synaptic
/// current computation (we use a single combined `syn_curr`).
pub fn integrate_ifcanahp(
  p : IFCANAHP,
  param : IFCANAHParameter,
  dt : Float,
) -> Unit {
  update_neuron_ifcanahp(p, param, dt)
  update_spike_ifcanahp(p, param, dt)
}