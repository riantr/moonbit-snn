// DoubleExpCurrentSynapse — current-based DoubleExp synaptic dynamics.
//
// Julia reference:
//   src/populations/synapse/synapses/DoubleExpCurrentSynapse.jl
//
// Difference vs DoubleExpSynapse (in synapse_params.mbt):
//   - DoubleExpSynapse: conductance-based, syn_curr = ge*(v-E_e) +
//     gi*(v-E_i) (voltage-dependent current).
//   - DoubleExpCurrentSynapse: current-based, syn_curr = -(ge - gi)
//     (no voltage dependence; the "ge" / "gi" are interpreted as
//     currents themselves, not conductances).
//
// Both use the same 2-state ODE form: he drives ge with rise τre,
// then ge decays with τde. Julia uses Euler forward
//   ge += dt * (-ge/τde + he)
//   he += dt * (-he/τre)
// which is the same as our existing DoubleExpSynapse step but without
// the multiplicative (he - ge) coupling factor.

///|
/// DoubleExpCurrentParameter — current-based DoubleExp synapse
/// parameters (separate rise/decay for exc and inh).
pub struct DoubleExpCurrentParameter {
  tau_re : Float
  tau_de : Float
  tau_ri : Float
  tau_di : Float
}

///|
/// Default DoubleExpCurrentParameter (Julia defaults: τre=1ms, τde=6ms,
/// τri=0.5ms, τdi=2ms).
pub fn DoubleExpCurrentParameter::new() -> DoubleExpCurrentParameter {
  { tau_re: 1.0F, tau_de: 6.0F, tau_ri: 0.5F, tau_di: 2.0F }
}

///|
/// DoubleExpCurrentSynapseVars — per-neuron state buffers for the
/// current-based DoubleExp synapse.
///
///   ge, gi : the "excitatory" / "inhibitory" currents (interpreted
///            directly as currents, not conductances)
///   he, hi : rise-state auxiliary variables
pub struct DoubleExpCurrentSynapseVars {
  n : Int
  ge : Array[Float]
  gi : Array[Float]
  he : Array[Float]
  hi : Array[Float]
}

///|
/// Allocate the per-neuron state buffers for an N-neuron population.
pub fn DoubleExpCurrentSynapseVars::new(n : Int) -> DoubleExpCurrentSynapseVars {
  let ge : Array[Float] = Array::make(n, 0.0F)
  let gi : Array[Float] = Array::make(n, 0.0F)
  let he : Array[Float] = Array::make(n, 0.0F)
  let hi : Array[Float] = Array::make(n, 0.0F)
  { n, ge, gi, he, hi }
}

///|
/// update_synapses! — Euler forward 2-state ODE per neuron.
/// Per Julia:
///   he[i] += glu[i]
///   hi[i] += gaba[i]
///   ge[i] += dt * (-ge[i]/τde + he[i])
///   he[i] += dt * (-he[i]/τre)
///   gi[i] += dt * (-gi[i]/τdi + hi[i])
///   hi[i] += dt * (-hi[i]/τri)
/// then `glu`, `gaba` are reset to zero (consumed as spike inputs).
pub fn update_synapses_double_exp_current(
  vars : DoubleExpCurrentSynapseVars,
  param : DoubleExpCurrentParameter,
  glu : Array[Float],
  gaba : Array[Float],
  dt : Float,
) -> Unit {
  let n = vars.n
  let ge = vars.ge
  let gi = vars.gi
  let he = vars.he
  let hi = vars.hi
  let tau_de = param.tau_de
  let tau_re = param.tau_re
  let tau_di = param.tau_di
  let tau_ri = param.tau_ri
  let inv_tau_de = 1.0F / tau_de
  let inv_tau_re = 1.0F / tau_re
  let inv_tau_di = 1.0F / tau_di
  let inv_tau_ri = 1.0F / tau_ri
  let mut k : Int = 0
  while k < n {
    let g_k = glu[k]
    let b_k = gaba[k]
    ignore(he.set(k, he[k] + g_k))
    ignore(hi.set(k, hi[k] + b_k))
    let ge_k = ge[k]
    let he_k = he[k]
    let gi_k = gi[k]
    let hi_k = hi[k]
    ignore(ge.set(k, ge_k + dt * (-ge_k * inv_tau_de + he_k)))
    ignore(he.set(k, he_k + dt * (-he_k * inv_tau_re)))
    ignore(gi.set(k, gi_k + dt * (-gi_k * inv_tau_di + hi_k)))
    ignore(hi.set(k, hi_k + dt * (-hi_k * inv_tau_ri)))
    ignore(glu.set(k, 0.0F))
    ignore(gaba.set(k, 0.0F))
    k = k + 1
  }
}

///|
/// synaptic_current! — computes syn_curr[i] = -(ge[i] - gi[i]).
///
/// Matches Julia's `syncurr[i] = -(ge[i] - gi[i])` exactly. Note the
/// sign convention: positive ge is excitatory (pushes v up), so
/// `-(ge - gi) > 0` for excitatory drive; opposite for inhibition.
pub fn synaptic_current_double_exp(
  vars : DoubleExpCurrentSynapseVars,
  syn_curr : Array[Float],
) -> Unit {
  let n = vars.n
  let ge = vars.ge
  let gi = vars.gi
  let mut k : Int = 0
  while k < n {
    ignore(syn_curr.set(k, -(ge[k] - gi[k])))
    k = k + 1
  }
}