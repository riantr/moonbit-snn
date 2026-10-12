// SingleExpSynapse — single exponential synaptic dynamics.
// Bit-exact port of SNNModels.jl.
//
// Julia reference:
//   src/populations/synapse/synapses/SingleExpSynapse.jl
//
// Differs from DoubleExpSynapse (in synapse_params.mbt):
//   - DoubleExpSynapse: 2-state ODE (he → ge) with separate rise τ
//     and decay τ.
//   - SingleExpSynapse: 1-state ODE (ge += spike_input + dt*(-ge/τ)).
//     Spike input is added directly to ge; no rise state.
//
// Float32 contract: every arithmetic uses `Float` (Float32).

///|
/// SingleExpParameter — single exponential synapse parameters.
pub struct SingleExpParameter {
  tau_e : Float
  tau_i : Float
  e_i : Float
  e_e : Float
  gsyn_e : Float
  gsyn_i : Float
}

///|
/// Default SingleExpParameter (Julia defaults: τe=6ms, τi=0.5ms,
/// E_i=-75mV, E_e=0mV, gsyn_e=1.0, gsyn_i=1.0).
pub fn SingleExpParameter::new() -> SingleExpParameter {
  { tau_e: 6.0F, tau_i: 0.5F, e_i: -75.0F, e_e: 0.0F,
    gsyn_e: 1.0F, gsyn_i: 1.0F }
}

///|
/// SingleExpSynapseVars is already defined in synapse_params.mbt
/// (struct alias for the same shape). Reuse it here.

///|
/// update_synapses! — 1-state exponential decay.
///
/// Julia's form:
///   ge[i] += glu[i]               (spike input, added directly)
///   gi[i] += gaba[i]
///   ge[i] += dt * (-ge[i] / τe)
///   gi[i] += dt * (-gi[i] / τi)
/// then `glu`, `gaba` are reset to 0 (consume spike inputs).
pub fn update_synapses_single_exp(
  vars : SingleExpSynapseVars,
  param : SingleExpParameter,
  glu : Array[Float],
  gaba : Array[Float],
  dt : Float,
) -> Unit {
  let n = vars.n
  let ge = vars.ge
  let gi = vars.gi
  let tau_e = param.tau_e
  let tau_i = param.tau_i
  let inv_tau_e = 1.0F / tau_e
  let inv_tau_i = 1.0F / tau_i
  let mut k : Int = 0
  while k < n {
    let g_k = glu[k]
    let b_k = gaba[k]
    let ge_k = ge[k]
    let gi_k = gi[k]
    ignore(ge.set(k, ge_k + g_k + dt * (-ge_k * inv_tau_e)))
    ignore(gi.set(k, gi_k + b_k + dt * (-gi_k * inv_tau_i)))
    ignore(glu.set(k, 0.0F))
    ignore(gaba.set(k, 0.0F))
    k = k + 1
  }
}

///|
/// synaptic_current! — conductance-based current.
///
///   syn_curr[i] = gsyn_e * ge[i] * (v[i] - E_e) + gsyn_i * gi[i] * (v[i] - E_i)
pub fn synaptic_current_single_exp(
  vars : SingleExpSynapseVars,
  param : SingleExpParameter,
  v : Array[Float],
  syn_curr : Array[Float],
) -> Unit {
  let n = vars.n
  let ge = vars.ge
  let gi = vars.gi
  let gsyn_e = param.gsyn_e
  let gsyn_i = param.gsyn_i
  let e_e = param.e_e
  let e_i = param.e_i
  let mut k : Int = 0
  while k < n {
    let exc = gsyn_e * ge[k] * (v[k] - e_e)
    let inh = gsyn_i * gi[k] * (v[k] - e_i)
    ignore(syn_curr.set(k, exc + inh))
    k = k + 1
  }
}