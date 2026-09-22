// synapse_params.mbt — synaptic parameter types for IF populations.
//
// Ports the four canonical SNN synapse parameter structs from
// SNNModels.jl/src/populations/synapse/synapses/:
//
//   - DeltaSynapse      : instantaneous (no time constants)
//   - SingleExpSynapse  : single exponential decay
//   - DoubleExpSynapse  : rise + decay exponentials
//   - CurrentSynapse    : exponential-decay current-based synapses
//
// In Julia, each struct has a companion `*Vars` struct that holds
// the per-neuron state (ge, gi, optional he, hi for double-exp).
// Each synapse type also defines `update_synapses!` and
// `synaptic_current!` functions, dispatched on the struct type.
//
// In MoonBit, the existing `IF` struct already implements the
// DoubleExpSynapse-style update path internally
// (`step_synapses` + `synaptic_current`). The four structs below
// are the parameter types only — they don't replace the IF
// integration loop. Instead, they let users build the synapse
// parameter object and read its fields for tests / composability.
//
// We also provide vars structs (DeltaSynapseVars / SingleExpSynapseVars
// / DoubleExpSynapseVars / CurrentSynapseVars) plus step functions
// that mirror the Julia update rules. These are useful for
// hand-rolled sim loops that want explicit per-synapse state
// (analogous to Julia's `Neurons` + `synapse = T(...)` pattern).

///|
/// DeltaSynapse — instantaneous synaptic dynamics. No parameters.
/// The `is a` struct (no fields) matches Julia's
/// `struct DeltaSynapse <: AbstractDeltaParameter end`.
pub(all) struct DeltaSynapse {
  // Empty struct — zero fields.
}

///|
pub fn DeltaSynapse::new() -> DeltaSynapse {
  DeltaSynapse::{  }
}

///|
/// DeltaSynapseVars — per-neuron state for DeltaSynapse.
pub struct DeltaSynapseVars {
  n : Int
  ge : Array[Float]
  gi : Array[Float]
}

///|
pub fn DeltaSynapseVars::new(n : Int) -> DeltaSynapseVars {
  { n, ge: Array::make(n, 0.0F), gi: Array::make(n, 0.0F) }
}

///|
/// SingleExpSynapse — single-exponential synaptic dynamics.
/// Fields:
///   - tau_e : decay time constant for excitatory synapses (ms)
///   - tau_i : decay time constant for inhibitory synapses (ms)
///   - e_i   : reversal potential for inhibitory synapses (mV)
///   - e_e   : reversal potential for excitatory synapses (mV)
///   - gsyn_e : scalar synaptic conductance for excitatory synapses
///   - gsyn_i : scalar synaptic conductance for inhibitory synapses
pub(all) struct SingleExpSynapse {
  tau_e : Float
  tau_i : Float
  e_i : Float
  e_e : Float
  gsyn_e : Float
  gsyn_i : Float
}

///|
/// Defaults match Julia's SingleExpSynapse (tau_e=6ms, tau_i=0.5ms,
/// e_i=-75mV, e_e=0mV, gsyn_e=gsyn_i=1.0).
pub fn SingleExpSynapse::new() -> SingleExpSynapse {
  {
    tau_e: 6.0F,
    tau_i: 0.5F,
    e_i: -75.0F,
    e_e: 0.0F,
    gsyn_e: 1.0F,
    gsyn_i: 1.0F,
  }
}

///|
/// SingleExpSynapseVars — per-neuron state for SingleExpSynapse.
pub struct SingleExpSynapseVars {
  n : Int
  ge : Array[Float]
  gi : Array[Float]
}

///|
pub fn SingleExpSynapseVars::new(n : Int) -> SingleExpSynapseVars {
  { n, ge: Array::make(n, 0.0F), gi: Array::make(n, 0.0F) }
}

///|
/// DoubleExpSynapse — rise + decay exponential synaptic dynamics.
/// Fields:
///   - tau_re, tau_de : rise + decay for excitatory synapses (ms)
///   - tau_ri, tau_di : rise + decay for inhibitory synapses (ms)
///   - e_i, e_e       : reversal potentials (mV)
///   - gsyn_e, gsyn_i : scalar synaptic conductances
pub(all) struct DoubleExpSynapse {
  tau_re : Float
  tau_de : Float
  tau_ri : Float
  tau_di : Float
  e_i : Float
  e_e : Float
  gsyn_e : Float
  gsyn_i : Float
}

///|
/// Defaults match Julia's DoubleExpSynapse (tau_re=1ms, tau_de=6ms,
/// tau_ri=0.5ms, tau_di=2ms, e_i=-75mV, e_e=0mV, gsyn_e=gsyn_i=1.0).
pub fn DoubleExpSynapse::new() -> DoubleExpSynapse {
  {
    tau_re: 1.0F,
    tau_de: 6.0F,
    tau_ri: 0.5F,
    tau_di: 2.0F,
    e_i: -75.0F,
    e_e: 0.0F,
    gsyn_e: 1.0F,
    gsyn_i: 1.0F,
  }
}

///|
/// DoubleExpSynapseVars — per-neuron state for DoubleExpSynapse.
pub struct DoubleExpSynapseVars {
  n : Int
  ge : Array[Float]
  gi : Array[Float]
  he : Array[Float]
  hi : Array[Float]
}

///|
pub fn DoubleExpSynapseVars::new(n : Int) -> DoubleExpSynapseVars {
  {
    n,
    ge: Array::make(n, 0.0F),
    gi: Array::make(n, 0.0F),
    he: Array::make(n, 0.0F),
    hi: Array::make(n, 0.0F),
  }
}

///|
/// CurrentSynapse — exponential-decay current-based synapses.
/// Fields:
///   - tau_e : decay time constant for excitatory synapses (ms)
///   - tau_i : decay time constant for inhibitory synapses (ms)
pub(all) struct CurrentSynapse {
  tau_e : Float
  tau_i : Float
}

///|
/// Defaults match Julia's CurrentSynapse (tau_e=6ms, tau_i=2ms).
pub fn CurrentSynapse::new() -> CurrentSynapse {
  { tau_e: 6.0F, tau_i: 2.0F }
}

///|
/// CurrentSynapseVars — per-neuron state for CurrentSynapse.
pub struct CurrentSynapseVars {
  n : Int
  ge : Array[Float]
  gi : Array[Float]
}

///|
pub fn CurrentSynapseVars::new(n : Int) -> CurrentSynapseVars {
  { n, ge: Array::make(n, 0.0F), gi: Array::make(n, 0.0F) }
}

///|
/// Update step for DeltaSynapseVars: instantaneous spike-driven
/// conductance — ge[i] += glu[i]; gi[i] += gaba[i]. No decay.
/// Caller is responsible for clearing glu / gaba buffers afterwards.
pub fn delta_synapse_step(
  vars : DeltaSynapseVars,
  glu : Array[Float],
  gaba : Array[Float],
) -> Unit {
  let n = vars.n
  let mut i = 0
  while i < n {
    vars.ge[i] = vars.ge[i] + glu[i]
    vars.gi[i] = vars.gi[i] + gaba[i]
    i = i + 1
  }
}

///|
/// Update step for SingleExpSynapseVars: spike-driven conductance
/// update + exponential decay. Caller is responsible for clearing
/// glu / gaba buffers afterwards.
pub fn single_exp_synapse_step(
  vars : SingleExpSynapseVars,
  param : SingleExpSynapse,
  glu : Array[Float],
  gaba : Array[Float],
  dt : Float,
) -> Unit {
  let n = vars.n
  let inv_tau_e : Float = 1.0F / param.tau_e
  let inv_tau_i : Float = 1.0F / param.tau_i
  let mut i = 0
  while i < n {
    vars.ge[i] = vars.ge[i] + glu[i]
    vars.gi[i] = vars.gi[i] + gaba[i]
    vars.ge[i] = vars.ge[i] + dt * (-vars.ge[i]) * inv_tau_e
    vars.gi[i] = vars.gi[i] + dt * (-vars.gi[i]) * inv_tau_i
    i = i + 1
  }
}

///|
/// Update step for DoubleExpSynapseVars: spike-driven conductance
/// update + rise/decay exponentials. Mirrors Julia's 2-state ODE:
///   h += dt * (input - h) / tau_rise
///   g += dt * (h - g) / tau_decay
/// Caller is responsible for clearing glu / gaba buffers afterwards.
pub fn double_exp_synapse_step(
  vars : DoubleExpSynapseVars,
  param : DoubleExpSynapse,
  glu : Array[Float],
  gaba : Array[Float],
  dt : Float,
) -> Unit {
  let n = vars.n
  let inv_tau_re : Float = 1.0F / param.tau_re
  let inv_tau_de : Float = 1.0F / param.tau_de
  let inv_tau_ri : Float = 1.0F / param.tau_ri
  let inv_tau_di : Float = 1.0F / param.tau_di
  let mut i = 0
  while i < n {
    // Update rise vars (he, hi).
    vars.he[i] = vars.he[i] + dt * (glu[i] - vars.he[i]) * inv_tau_re
    vars.hi[i] = vars.hi[i] + dt * (gaba[i] - vars.hi[i]) * inv_tau_ri
    // Update conductance (ge, gi).
    vars.ge[i] = vars.ge[i] + dt * (vars.he[i] - vars.ge[i]) * inv_tau_de
    vars.gi[i] = vars.gi[i] + dt * (vars.hi[i] - vars.gi[i]) * inv_tau_di
    i = i + 1
  }
}

///|
/// Update step for CurrentSynapseVars: spike-driven conductance
/// update + exponential decay. Caller is responsible for clearing
/// glu / gaba buffers afterwards.
pub fn current_synapse_step(
  vars : CurrentSynapseVars,
  param : CurrentSynapse,
  glu : Array[Float],
  gaba : Array[Float],
  dt : Float,
) -> Unit {
  let n = vars.n
  let inv_tau_e : Float = 1.0F / param.tau_e
  let inv_tau_i : Float = 1.0F / param.tau_i
  let mut i = 0
  while i < n {
    vars.ge[i] = vars.ge[i] + glu[i]
    vars.gi[i] = vars.gi[i] + gaba[i]
    vars.ge[i] = vars.ge[i] + dt * (-vars.ge[i]) * inv_tau_e
    vars.gi[i] = vars.gi[i] + dt * (-vars.gi[i]) * inv_tau_i
    i = i + 1
  }
}

///|
/// synaptic_current for DeltaSynapse — `syn_curr = -(ge - gi)`,
/// then zero ge / gi (instantaneous). Mirrors Julia's
/// `synaptic_current!(p, synapse::DeltaSynapse, synvars::DeltaSynapseVars)`.
pub fn delta_synapse_current(
  vars : DeltaSynapseVars,
  syn_curr : Array[Float],
) -> Unit {
  let n = vars.n
  let mut i = 0
  while i < n {
    syn_curr[i] = -(vars.ge[i] - vars.gi[i])
    vars.ge[i] = 0.0F
    vars.gi[i] = 0.0F
    i = i + 1
  }
}

///|
/// synaptic_current for CurrentSynapse — `syn_curr = -(ge - gi)`.
/// Ge / gi decay via the `current_synapse_step` so this just
/// computes the current.
pub fn current_synapse_current(
  vars : CurrentSynapseVars,
  syn_curr : Array[Float],
) -> Unit {
  let n = vars.n
  let mut i = 0
  while i < n {
    syn_curr[i] = -(vars.ge[i] - vars.gi[i])
    i = i + 1
  }
}

///|
/// synaptic_current for SingleExpSynapse / DoubleExpSynapse —
/// `syn_curr = ge * (v - E_e) * gsyn_e + gi * (v - E_i) * gsyn_i`.
pub fn conductance_synapse_current(
  vars : SingleExpSynapseVars,
  param : SingleExpSynapse,
  v : Array[Float],
  syn_curr : Array[Float],
) -> Unit {
  let n = vars.n
  let mut i = 0
  while i < n {
    syn_curr[i] = vars.ge[i] * (v[i] - param.e_e) * param.gsyn_e +
      vars.gi[i] * (v[i] - param.e_i) * param.gsyn_i
    i = i + 1
  }
}

///|
pub fn double_exp_conductance_current(
  vars : DoubleExpSynapseVars,
  param : DoubleExpSynapse,
  v : Array[Float],
  syn_curr : Array[Float],
) -> Unit {
  let n = vars.n
  let mut i = 0
  while i < n {
    syn_curr[i] = vars.ge[i] * (v[i] - param.e_e) * param.gsyn_e +
      vars.gi[i] * (v[i] - param.e_i) * param.gsyn_i
    i = i + 1
  }
}