// receptor.mbt — Multi-receptor synaptic dynamics (AMPA, NMDA, GABAa, GABAb)
// for multi-compartment neurons.
//
// Port of SNNModels.jl/src/populations/synapse/receptors.jl and
// synapses/ReceptorSynapse.jl. Implements the same 2-state ODE
// for synaptic rise/decay:
//
//   h[i] += target[i] * α
//   g[i] = exp(-dt * τd⁻) * (g[i] + dt * h[i])
//   h[i] = exp(-dt * τr⁻) * h[i]
//
// where τr⁻ = 1/τr, τd⁻ = 1/τd, α = (τd - τr) / (τd * τr).
//
// Plus NMDA voltage-dependent gating:
//
//   B(v) = 1 / (1 + (mg/b) * exp(k * v))
//
// with Eyal 2018 defaults (mg=1.0 mM, b=3.36, k=-0.077).
//
// Bit-exact note: this implements the same dynamics as Julia's
// `ReceptorSynapse` + `nmda_gating` for a single population. The
// full multi-compartment wiring (TripodSynapse / ReceptorSynapse
// with soma/dendrite routing) will compose this in a follow-up.

///|
/// NMDA voltage-dependence parameters (Eyal 2018):
///   B(v) = 1 / (1 + (mg/b) * exp(k * v))
pub struct NMDAVoltageDependency {
  b : Float  // voltage dependence factor (default: 3.36)
  k : Float  // voltage dependence factor (default: -0.077)
  mg : Float // Mg concentration in mM (default: 1.0)
}

///|
/// Eyal 2018 defaults: mg = 1.0 mM, b = 3.36, k = -0.077.
pub fn NMDAVoltageDependency::eyal() -> NMDAVoltageDependency {
  { b: 3.36F, k: -0.077F, mg: 1.0F }
}

///|
/// Custom NMDAVoltageDependency with explicit mg / b / k values.
/// Mirrors Julia's `NMDAVoltageDependency(mg = 1.0, b = 3.36, k = -0.077)`.
pub fn NMDAVoltageDependency::custom(
  mg : Float,
  b : Float,
  k : Float,
) -> NMDAVoltageDependency {
  { b, k, mg }
}

///|
/// Soma NMDA defaults (SomaNMDA in Julia): mg = 1.0, b = 3.57, k = -0.062.
pub fn NMDAVoltageDependency::soma() -> NMDAVoltageDependency {
  { b: 3.57F, k: -0.062F, mg: 1.0F }
}

///|
/// Compute the NMDA voltage-dependent gating B(v) = 1/(1 + (mg/b)*exp(k*v)).
/// Matches Julia's `nmda_gating(v, NMDA)`.
pub fn nmda_gating(v : Float, nmda : NMDAVoltageDependency) -> Float {
  // exp(k * v) can underflow for very negative v; clamp at expf
  // boundary. expf(-87) ≈ 1.5e-38 (smallest positive Float32).
  // expf(88) ≈ 1.65e38 (largest positive Float32).
  let arg = nmda.k * v
  let exp_arg = if arg < -87.0F {
    0.0F
  } else if arg > 88.0F {
    1.0e38F  // saturating exp without overflow
  } else {
    expf(arg)
  }
  let denom = 1.0F + (nmda.mg / nmda.b) * exp_arg
  1.0F / denom
}

///|
/// A synaptic receptor (AMPA, NMDA, GABAa, GABAb, ...).
/// `target` ∈ {"glu", "gaba"} determines which input buffer
/// (`target[i] = glu[i]` or `gaba[i]`) drives this receptor.
/// `is_nmda = true` triggers voltage-dependent gating in current computation.
pub struct Receptor {
  e_rev : Float
  tau_r : Float
  tau_d : Float
  g0 : Float          // maximum conductance (set by caller; used as gsyn)
  gsyn : Float        // normalised peak conductance (g0 * norm_synapse)
  alpha : Float       // α = (τd - τr) / (τd * τr)
  tau_r_inv : Float   // 1/τr (0 if τr <= 0)
  tau_d_inv : Float   // 1/τd (0 if τd <= 0)
  is_nmda : Bool      // true for NMDA receptor (voltage-dependent)
  target : String     // "glu" or "gaba"
}

///|
/// Construct a non-NMDA receptor (AMPA, GABAa, GABAb).
pub fn Receptor::simple(
  e_rev : Float,
  tau_r : Float,
  tau_d : Float,
  g0 : Float,
  target : String,
) -> Receptor {
  let alpha = (tau_d - tau_r) / (tau_d * tau_r)
  let tau_r_inv = if tau_r > 0.0F { 1.0F / tau_r } else { 0.0F }
  let tau_d_inv = if tau_d > 0.0F { 1.0F / tau_d } else { 0.0F }
  // norm_synapse(τr, τd) = 1 / (-exp(-t_p/τr) + exp(-t_p/τd))
  // where t_p = τr * τd / (τd - τr) * log(τd / τr).
  let norm = norm_synapse(tau_r, tau_d)
  let gsyn = if g0 > 0.0F { g0 * norm } else { 0.0F }
  { e_rev, tau_r, tau_d, g0, gsyn, alpha, tau_r_inv, tau_d_inv,
    is_nmda: false, target }
}

///|
/// Construct an NMDA receptor (voltage-dependent gating).
pub fn Receptor::nmda(
  e_rev : Float,
  tau_r : Float,
  tau_d : Float,
  g0 : Float,
) -> Receptor {
  let r = Receptor::simple(e_rev, tau_r, tau_d, g0, "glu")
  { ..r, is_nmda: true }
}

///|
/// norm_synapse(τr, τd) = 1 / (-exp(-t_p/τr) + exp(-t_p/τd))
/// where t_p = τr * τd / (τd - τr) * log(τd / τr).
pub fn norm_synapse(tau_r : Float, tau_d : Float) -> Float {
  let t_p = tau_r * tau_d / (tau_d - tau_r) * logf(tau_d / tau_r)
  1.0F / (-expf(-t_p / tau_r) + expf(-t_p / tau_d))
}

///|
/// alpha_synapse(τr, τd) — the α factor used in the 2-state ODE
/// `h += α * target` where target is the spike-input pulse.
/// Julia formula: `α = (τd - τr) / (τd * τr)`.
pub fn alpha_synapse(tau_r : Float, tau_d : Float) -> Float {
  (tau_d - tau_r) / (tau_d * tau_r)
}

///|
/// Glutamatergic — bundle of AMPA + NMDA receptors (exc synapse pair).
pub struct Glutamatergic {
  ampa : Receptor
  nmda : Receptor
}

///|
/// Default Glutamatergic (empty AMPA + empty NMDA ReceptorVoltage).
pub fn Glutamatergic::new(
  ampa~ : Receptor = Receptor::simple(0.0F, 6.0F, 0.0F, 1.0F, "glu"),
  nmda~ : Receptor = Receptor::nmda(0.0F, 100.0F, 0.0F, 1.0F),
) -> Glutamatergic {
  { ampa, nmda }
}

///|
/// Custom Glutamatergic (caller supplies both AMPA + NMDA).
pub fn Glutamatergic::custom(ampa~ : Receptor, nmda~ : Receptor) -> Glutamatergic {
  { ampa, nmda }
}

///|
/// GABAergic — bundle of GABAa + GABAb receptors (inh synapse pair).
pub struct GABAergic {
  gabaa : Receptor
  gabab : Receptor
}

///|
/// Default GABAergic (empty GABAa + empty GABAb).
pub fn GABAergic::new(
  gabaa~ : Receptor = Receptor::simple(-75.0F, 0.5F, 2.0F, 1.0F, "gaba"),
  gabab~ : Receptor = Receptor::simple(-75.0F, 0.5F, 20.0F, 1.0F, "gaba"),
) -> GABAergic {
  { gabaa, gabab }
}

///|
/// Custom GABAergic (caller supplies both GABAa + GABAb).
pub fn GABAergic::custom(gabaa~ : Receptor, gabab~ : Receptor) -> GABAergic {
  { gabaa, gabab }
}

///|
/// Receptors — a 4-element collection (AMPA, NMDA, GABAa, GABAb)
/// for one synapse. Index 0 = AMPA, 1 = NMDA, 2 = GABAa, 3 = GABAb
/// (matches Julia's Receptors default ordering).
pub struct Receptors {
  rec : Array[Receptor]  // size 4
}

///|
/// Build a default Receptors (empty AMPA, NMDA, GABAa, GABAb).
pub fn Receptors::new(
  ampa : Receptor,
  nmda_rec : Receptor,
  gabaa : Receptor,
  gabab : Receptor,
) -> Receptors {
  { rec: [ampa, nmda_rec, gabaa, gabab] }
}

///|
/// Build a Receptors from a Glutamatergic + GABAergic pair. Mirrors
/// Julia's `Receptors(glu::Glutamatergic, gaba::GABAergic)` constructor.
pub fn Receptors::from_pair(glu : Glutamatergic, gaba : GABAergic) -> Receptors {
  { rec: [glu.ampa, glu.nmda, gaba.gabaa, gaba.gabab] }
}

///|
/// Build a Receptors from an explicit `Array[Receptor]` of length 4.
/// Mirrors Julia's `Receptors(rec::Vector{Receptor})` constructor.
/// Indices: 0=AMPA, 1=NMDA, 2=GABAa, 3=GABAb. Panics if length != 4.
pub fn Receptors::from_array(rec : Array[Receptor]) -> Receptors {
  if rec.length() != 4 {
    abort("Receptors::from_array requires length-4 array")
  }
  { rec }
}

///|
/// Update one receptor's 2-state ODE (g, h) for one time step.
/// `target[i]` is the input pulse added to `h[i]` (multiplied by α).
/// Post-condition: target is consumed (caller resets to 0).
pub fn step_receptor(
  g : Array[Float],  // running conductance state (modified in place)
  h : Array[Float],  // auxiliary variable (modified in place)
  target : Array[Float],  // input pulse (consumed; reset to 0 after)
  r : Receptor,
  dt : Float,
) -> Unit {
  let n = g.length()
  let alpha = r.alpha
  let tr_inv = r.tau_r_inv
  let td_inv = r.tau_d_inv
  let decay_d = expf(-dt * td_inv)
  let decay_r = expf(-dt * tr_inv)
  for i in 0..<n {
    h[i] = h[i] + target[i] * alpha
    g[i] = decay_d * (g[i] + dt * h[i])
    h[i] = decay_r * h[i]
    target[i] = 0.0F
  }
}

///|
/// Compute the synaptic current contribution of one receptor across N
/// neurons: I[i] = gsyn * g[i] * (v[i] - e_rev) * B(v[i]).
/// For non-NMDA, B(v) = 1.
pub fn receptor_current(
  g : Array[Float],
  v : Array[Float],
  r : Receptor,
  nmda_dep : NMDAVoltageDependency,
  out : Array[Float],
) -> Unit {
  let n = g.length()
  let gsyn = r.gsyn
  let e_rev = r.e_rev
  if r.is_nmda {
    for i in 0..<n {
      let b = nmda_gating(v[i], nmda_dep)
      out[i] = out[i] + gsyn * g[i] * (v[i] - e_rev) * b
    }
  } else {
    for i in 0..<n {
      out[i] = out[i] + gsyn * g[i] * (v[i] - e_rev)
    }
  }
}

///|
/// Convenience: compute current for all 4 receptors of a Receptors
/// collection. `out[i]` is the sum across receptors.
pub fn receptors_current(
  g_matrix : Array[Float],  // size N × 4 (rows = neurons, cols = receptors)
  v : Array[Float],
  rs : Receptors,
  nmda_dep : NMDAVoltageDependency,
  out : Array[Float],
) -> Unit {
  let n = v.length()
  let n_rec = rs.rec.length()
  for k in 0..<n_rec {
    // Slice the k-th column of g_matrix.
    let g_col : Array[Float] = Array::make(n, 0.0F)
    for i in 0..<n {
      g_col[i] = g_matrix[i * n_rec + k]
    }
    receptor_current(g_col, v, rs.rec[k], nmda_dep, out)
  }
}