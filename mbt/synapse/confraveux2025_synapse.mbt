// Confavreux2025Synapse — Confavreux 2025 multi-timescale synapse
// with separate AMPA / NMDA / GABA conductances.
//
// Julia reference:
//   src/populations/synapse/synapses/Confraveux2025.jl
//
// Difference vs the existing DoubleExpSynapse (in synapse_params.mbt):
//   - DoubleExpSynapse: 2-state ODE (h rises, g decays) with single
//     g_e/g_i per neuron, syn_curr = gsyn_e*ge*(v-E_e) + gsyn_i*gi*(v-E_i).
//   - Confavreux2025Synapse: 1-state ODE for AMPA + GABA (single tau),
//     and a *coupled* NMDA channel that tracks AMPA with τNMDA. The
//     output current is a weighted blend:
//       syn_curr = (α * gAMPA + (1-α) * gNMDA) * (v - E_e) + gGABA * (v - E_i)
//     i.e. AMPA contribution is α-weighted; NMDA contribution is
//     (1-α)-weighted (the NMDA voltage dependence B(v) is in `α`).
//
// Float32 contract: every arithmetic uses `Float` (Float32). Update
// order matches Julia's update_synapses! exactly:
//   gAMPA += dt * (-gAMPA/τAMPA + glu)
//   gGABA += dt * (-gGABA/τGABA + gaba)
//   gNMDA += dt * (gAMPA - gNMDA) / τNMDA   (coupled to AMPA)
// then `fill!(glu, 0); fill!(gaba, 0)` (consume spike inputs).
//
// synaptic_current!:
//   syn_curr[i] = (α * gAMPA[i] + (1-α) * gNMDA[i]) * (v[i] - E_e)
//                + gGABA[i] * (v[i] - E_i)

///|
/// Confavreux2025Parameter — synapse parameters for the
/// Confavreux 2025 multi-timescale model.
///
/// Julia defaults: τAMPA=5ms, τNMDA=100ms, τGABA=10ms, E_i=-80mV,
/// E_e=0mV, α=0.23.
pub struct Confavreux2025Parameter {
  tau_ampa : Float
  tau_nmda : Float
  tau_gaba : Float
  e_i : Float
  e_e : Float
  alpha : Float
}

///|
/// Default Confavreux2025Parameter (Julia defaults).
pub fn Confavreux2025Parameter::new() -> Confavreux2025Parameter {
  { tau_ampa: 5.0F, tau_nmda: 100.0F, tau_gaba: 10.0F,
    e_i: -80.0F, e_e: 0.0F, alpha: 0.23F }
}

///|
/// Confavreux2025SynapseVars — per-neuron state buffers.
///
///   gAMPA : AMPA conductance (drives both E_e drive + NMDA coupling)
///   gNMDA : NMDA conductance (coupled to AMPA via τNMDA)
///   gGABA : GABA conductance (inhibitory drive)
pub struct Confavreux2025SynapseVars {
  n : Int
  g_ampa : Array[Float]
  g_nmda : Array[Float]
  g_gaba : Array[Float]
}

///|
/// Allocate per-neuron buffers for an N-neuron population.
pub fn Confavreux2025SynapseVars::new(n : Int) -> Confavreux2025SynapseVars {
  let g_ampa : Array[Float] = Array::make(n, 0.0F)
  let g_nmda : Array[Float] = Array::make(n, 0.0F)
  let g_gaba : Array[Float] = Array::make(n, 0.0F)
  { n, g_ampa, g_nmda, g_gaba }
}

///|
/// update_synapses! — 1-state ODE for AMPA/GABA, coupled ODE for NMDA.
///
/// Julia's update order preserved bit-exactly:
///   gAMPA[i] += dt * (-gAMPA[i] / τAMPA + glu[i])
///   gGABA[i] += dt * (-gGABA[i] / τGABA + gaba[i])
///   gNMDA[i] += dt * (gAMPA[i] - gNMDA[i]) / τNMDA
/// then `glu`, `gaba` are reset to zero (consumed as spike inputs).
pub fn update_synapses_confraveux2025(
  vars : Confavreux2025SynapseVars,
  param : Confavreux2025Parameter,
  glu : Array[Float],
  gaba : Array[Float],
  dt : Float,
) -> Unit {
  let n = vars.n
  let g_ampa = vars.g_ampa
  let g_nmda = vars.g_nmda
  let g_gaba = vars.g_gaba
  let tau_ampa = param.tau_ampa
  let tau_nmda = param.tau_nmda
  let tau_gaba = param.tau_gaba
  let inv_tau_ampa = 1.0F / tau_ampa
  let inv_tau_nmda = 1.0F / tau_nmda
  let inv_tau_gaba = 1.0F / tau_gaba
  let mut k : Int = 0
  while k < n {
    let g_k = glu[k]
    let b_k = gaba[k]
    let ga_k = g_ampa[k]
    let gn_k = g_nmda[k]
    let gg_k = g_gaba[k]
    // Julia uses `gAMPA[i] + dt*(-gAMPA[i]/τAMPA + glu[i])` form;
    // for the NMDA step Julia uses `gAMPA[i]` (post-update value),
    // not the pre-update. We replicate by reading ga after the AMPA update.
    ignore(g_ampa.set(k, ga_k + dt * (-ga_k * inv_tau_ampa + g_k)))
    ignore(g_gaba.set(k, gg_k + dt * (-gg_k * inv_tau_gaba + b_k)))
    let ga_new = g_ampa[k] // post-update AMPA, matches Julia's @gAMPA
    ignore(g_nmda.set(k, gn_k + dt * (ga_new - gn_k) * inv_tau_nmda))
    ignore(glu.set(k, 0.0F))
    ignore(gaba.set(k, 0.0F))
    k = k + 1
  }
}

///|
/// synaptic_current! — outputs the weighted AMPA + NMDA + GABA current.
///
///   syn_curr[i] = (α * gAMPA[i] + (1-α) * gNMDA[i]) * (v[i] - E_e)
///               + gGABA[i] * (v[i] - E_i)
pub fn synaptic_current_confraveux2025(
  vars : Confavreux2025SynapseVars,
  param : Confavreux2025Parameter,
  v : Array[Float],
  syn_curr : Array[Float],
) -> Unit {
  let n = vars.n
  let g_ampa = vars.g_ampa
  let g_nmda = vars.g_nmda
  let g_gaba = vars.g_gaba
  let alpha = param.alpha
  let e_e = param.e_e
  let e_i = param.e_i
  let mut k : Int = 0
  while k < n {
    let weighted = alpha * g_ampa[k] + (1.0F - alpha) * g_nmda[k]
    let exc = weighted * (v[k] - e_e)
    let inh = g_gaba[k] * (v[k] - e_i)
    ignore(syn_curr.set(k, exc + inh))
    k = k + 1
  }
}