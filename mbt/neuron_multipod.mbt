// neuron_multipod.mbt — variable-dendrite-count Tripod (Multipod).
//
// Julia reference:
//   SNNModels.jl/src/populations/multicompartment/multipod.jl
//
// Provides:
//   - MultipodParameter — wraps Array[Dendrite] (the existing
//     Dendrite struct from neuron_dendrite.mbt is reused).
//   - Multipod struct (n, nd, v_s, w_s, v_d, ge_s, gi_s, he_s, hi_s,
//     he_d, hi_d, g_d, h_d, fire, after_spike, postspike, theta,
//     dv, dv_temp, cs, iv, soma_syn, dend_syn, glu_receptors,
//     gaba_receptors, alpha).
//   - Multipod::new(dendrites, n, param) — allocates all per-neuron +
//     per-dendrite arrays; initial v_s / v_d[k] in [vr, vt].
//   - Multipod::step(p, dt) — Euler-style update (he_d → h_d → g_d,
//     soma_syn, w_s, v_s, v_d[k], fire/after_spike reset).
//
// Float32 contract: every arithmetic uses Float32.
//
// **Note**: this is a simplified port. The Julia `Multipod` includes
// full NMDA multi-receptor + ReceptorArray wiring + Heun corrector.
// We provide a minimum-viable Euler step + receptor buffers (4 per
// dendrite: AMPA / NMDA / GABAa / GABAb) so the per-dendrite
// per-receptor structure is in place. Heun + NMDA voltage gating are
// TODO.

// =========================================================================
// MultipodParameter
// =========================================================================

// Dendrite is defined in neuron_dendrite.mbt — we reuse it.

///|
/// MultipodParameter — wraps an Array[Dendrite] for the multipod
/// constructor. The Julia version uses Dendrite objects directly.
pub struct MultipodParameter {
  dendrites : Array[Dendrite]
}

///|
pub fn MultipodParameter::new(dendrites : Array[Dendrite]) -> MultipodParameter {
  { dendrites: dendrites }
}

///|
/// Convenience: MultipodParameter from a single Dendrite applied
/// `nd` times. Mirrors Julia's `Multipod(d; N, Nd)` constructor.
pub fn MultipodParameter::uniform(
  d : Dendrite,
  nd : Int,
) -> MultipodParameter {
  let ds : Array[Dendrite] = Array::make(nd, d)
  { dendrites: ds }
}

// =========================================================================
// Multipod struct
// =========================================================================

///|
/// Multipod — variable-dendrite-count multi-compartment AdEx neuron.
/// `v_d` is a Array[Array[Float]] (one inner array per dendrite,
/// each of length N). `g_d` is a flat Float array indexed as
/// `g_d[i, d, n]` via `g_d[i + d*N + n*N*nd]`.
pub struct Multipod {
  n : Int
  nd : Int
  // Soma
  v_s : Array[Float]
  w_s : Array[Float]
  // Dendrite voltages (length nd; each inner array length N).
  v_d : Array[Array[Float]]
  // Soma conductances + spike-input buffers.
  ge_s : Array[Float]
  gi_s : Array[Float]
  he_s : Array[Float]
  hi_s : Array[Float]
  // Per-dendrite spike-input buffers (length nd; each inner length N).
  he_d : Array[Array[Float]]
  hi_d : Array[Array[Float]]
  // Per-dendrite per-receptor synaptic conductance + spike-input
  // state. Nested 3-level per-dendrite per-neuron: g_d[d][i][n] for
  // receptor n (0=AMPA, 1=NMDA, 2=GABAa, 3=GABAb). The 3-level
  // nesting avoids the flat-index nested loops that previously
  // triggered stack overflows in MoonBit's JIT.
  g_d : Array[Array[Array[Float]]]
  h_d : Array[Array[Array[Float]]]
  // Receptor markers.
  glu_receptors : Array[Int]
  gaba_receptors : Array[Int]
  // Alpha scaling for each of the 4 receptors.
  alpha : Array[Float]
  // Per-neuron spike bookkeeping.
  fire : Array[Bool]
  after_spike : Array[Int]
  postspike : PostSpike
  // Threshold + adaptation state.
  theta : Array[Float]
  // Heun scratch buffers (length nd + 1).
  dv : Array[Float]
  dv_temp : Array[Float]
  // Per-compartment axial + synaptic-current buffers.
  cs : Array[Float]
  iv : Array[Float]
  // Synapse parameter arrays (simplified — Julia uses Receptors).
  soma_syn : SingleExpSynapse
  dend_syn : SingleExpSynapse
  // NMDA voltage-dependency.
  nmda : NMDAVoltageDependency
  // Dendrite parameters.
  dendrites : Array[Dendrite]
}

///|
/// Allocate a Multipod. `dendrites` is the per-dendrite parameter
/// list (length = nd). `n` is the number of neurons. `param` is the
/// AdEx soma parameters (defaults to AdExParameter::new()).
pub fn Multipod::new(
  dendrites : Array[Dendrite],
  n : Int,
  param? : AdExParameter = AdExParameter::new(),
) -> Multipod {
  let nd = dendrites.length()
  let vt = param.vt
  let vr = param.vr
  let range = vt - vr
  // Initial v_s[i] in [vr, vt] uniformly (deterministic).
  let v_s : Array[Float] = Array::make(n, vr)
  let mut i = 0
  while i < n {
    v_s[i] = vr + (i % 3).to_float() * range / 3.0F
    i = i + 1
  }
  let w_s : Array[Float] = Array::make(n, 0.0F)
  // Dendrite voltages (length nd; each inner array length N).
  let v_d : Array[Array[Float]] = Array::make(nd, [vr])
  let mut d = 0
  while d < nd {
    let inner : Array[Float] = Array::make(n, vr + (d % 2).to_float() * range / 2.0F)
    ignore(v_d.set(d, inner))
    d = d + 1
  }
  // Spike-input buffers.
  let he_s : Array[Float] = Array::make(n, 0.0F)
  let hi_s : Array[Float] = Array::make(n, 0.0F)
  let ge_s : Array[Float] = Array::make(n, 0.0F)
  let gi_s : Array[Float] = Array::make(n, 0.0F)
  let he_d : Array[Array[Float]] = Array::make(nd, [0.0F])
  let hi_d : Array[Array[Float]] = Array::make(nd, [0.0F])
  let mut d2 = 0
  while d2 < nd {
    let inner_e : Array[Float] = Array::make(n, 0.0F)
    let inner_i : Array[Float] = Array::make(n, 0.0F)
    ignore(he_d.set(d2, inner_e))
    ignore(hi_d.set(d2, inner_i))
    d2 = d2 + 1
  }
  // Per-receptor per-dendrite conductance + spike-input state.
  // Nested per-dendrite per-neuron: g_d[d][i][n] for receptor n
  // (0=AMPA, 1=NMDA, 2=GABAa, 3=GABAb). The 3-level nesting avoids
  // the flat-index nested loops that previously triggered stack
  // overflows in MoonBit's JIT.
  let g_d : Array[Array[Array[Float]]] = Array::make(nd, [[0.0F]])
  let h_d : Array[Array[Array[Float]]] = Array::make(nd, [[0.0F]])
  let mut d3 = 0
  while d3 < nd {
    let g_inner : Array[Array[Float]] = Array::make(n, [0.0F])
    let h_inner : Array[Array[Float]] = Array::make(n, [0.0F])
    let mut i3 = 0
    while i3 < n {
      let g_neuron : Array[Float] = Array::make(4, 0.0F)
      let h_neuron : Array[Float] = Array::make(4, 0.0F)
      ignore(g_inner.set(i3, g_neuron))
      ignore(h_inner.set(i3, h_neuron))
      i3 = i3 + 1
    }
    ignore(g_d.set(d3, g_inner))
    ignore(h_d.set(d3, h_inner))
    d3 = d3 + 1
  }
  // Receptor markers.
  let glu_receptors : Array[Int] = [1, 2]
  let gaba_receptors : Array[Int] = [3, 4]
  let alpha : Array[Float] = [1.0F, 1.0F, 1.0F, 1.0F]
  // Spike bookkeeping.
  let fire : Array[Bool] = Array::make(n, false)
  let after_spike : Array[Int] = Array::make(n, 0)
  let postspike = PostSpike::new()
  let theta : Array[Float] = Array::make(n, param.vt)
  // Heun scratch.
  let dv : Array[Float] = Array::make(nd + 1, 0.0F)
  let dv_temp : Array[Float] = Array::make(nd + 1, 0.0F)
  let cs : Array[Float] = Array::make(nd, 0.0F)
  let iv : Array[Float] = Array::make(nd + 1, 0.0F)
  // Synapse parameters (SingleExpSynapse defaults).
  let soma_syn = SingleExpSynapse::new()
  let dend_syn = SingleExpSynapse::new()
  // NMDA voltage-dependency (Eyal defaults).
  let nmda = NMDAVoltageDependency::eyal()
  {
    n,
    nd,
    v_s,
    w_s,
    v_d,
    ge_s,
    gi_s,
    he_s,
    hi_s,
    he_d,
    hi_d,
    g_d,
    h_d,
    glu_receptors,
    gaba_receptors,
    alpha,
    fire,
    after_spike,
    postspike,
    theta,
    dv,
    dv_temp,
    cs,
    iv,
    soma_syn,
    dend_syn,
    nmda,
    dendrites,
  }
}

///|
/// Step the multipod forward one dt — Euler-style update (no Heun
/// corrector). Updates soma_syn, dend_syn receptors, w_s, v_s, v_d,
/// then spike detection.
pub fn Multipod::step(p : Multipod, dt : Float) -> Unit {
  let n = p.n
  let nd = p.nd
  let soma_decay_d : Float = expf(-dt / p.soma_syn.tau_e)
  let soma_decay_r : Float = expf(-dt / p.soma_syn.tau_e)
  let dend_decay_d : Float = expf(-dt / p.dend_syn.tau_e)
  let dend_decay_r : Float = expf(-dt / p.dend_syn.tau_e)
  // Phase 1: per-dendrite, per-receptor h update from he_d / hi_d.
  let mut d_idx = 0
  while d_idx < nd {
    let inner_e = p.he_d[d_idx]
    let inner_i = p.hi_d[d_idx]
    let mut i = 0
    while i < n {
      // Update glu_receptors [1, 2] (h_d[d][i][1] + he_d[i] * alpha[0],
      // h_d[d][i][2] + he_d[i] * alpha[1]).
      p.h_d[d_idx][i][1] = p.h_d[d_idx][i][1] + inner_e[i] * p.alpha[0]
      p.h_d[d_idx][i][2] = p.h_d[d_idx][i][2] + inner_e[i] * p.alpha[1]
      // Update gaba_receptors [3, 4].
      p.h_d[d_idx][i][3] = p.h_d[d_idx][i][3] + inner_i[i] * p.alpha[2]
      p.h_d[d_idx][i][4] = p.h_d[d_idx][i][4] + inner_i[i] * p.alpha[3]
      i = i + 1
    }
    d_idx = d_idx + 1
  }
  // Phase 2: reset he_d, hi_d.
  d_idx = 0
  while d_idx < nd {
    let mut i = 0
    while i < n {
      p.he_d[d_idx][i] = 0.0F
      p.hi_d[d_idx][i] = 0.0F
      i = i + 1
    }
    d_idx = d_idx + 1
  }
  // Phase 3: update soma_syn (ge_s, he_s, gi_s, hi_s).
  let mut i = 0
  while i < n {
    p.ge_s[i] = soma_decay_d * (p.ge_s[i] + dt * p.he_s[i])
    p.he_s[i] = soma_decay_r * p.he_s[i]
    p.gi_s[i] = soma_decay_d * (p.gi_s[i] + dt * p.hi_s[i])
    p.hi_s[i] = soma_decay_r * p.hi_s[i]
    i = i + 1
  }
  // Phase 4: update per-dendrite g_d, h_d for each of 4 receptors.
  d_idx = 0
  while d_idx < nd {
    let mut i = 0
    while i < n {
      let mut k = 0
      while k < 4 {
        p.g_d[d_idx][i][k] = dend_decay_d *
          (p.g_d[d_idx][i][k] + dt * p.h_d[d_idx][i][k])
        p.h_d[d_idx][i][k] = dend_decay_r * p.h_d[d_idx][i][k]
        k = k + 1
      }
      i = i + 1
    }
    d_idx = d_idx + 1
  }
  // Phase 5: per-neuron state update.
  multipod_step_neurons(p, dt, n, nd)
}

// =========================================================================
// Per-neuron update — extracted to flatten the call stack.
// =========================================================================

///|
fn multipod_step_neurons(p : Multipod, dt : Float, n : Int, nd : Int) -> Unit {
  let c = 281.0F
  let gl = 40.0F
  let dt_slope = 2.0F
  let el = -70.6F
  let a : Float = 4.0F
  let b : Float = 80.5F
  let tw : Float = 144.0F
  let mut i = 0
  while i < n {
    // Compute cs[d] = -((v_d[d][i] - v_s[i]) * gax[i]).
    let mut d_idx = 0
    while d_idx < nd {
      p.cs[d_idx] = -(p.v_d[d_idx][i] - p.v_s[i]) * p.dendrites[d_idx].gax[i]
      d_idx = d_idx + 1
    }
    // Compute iv[0] = soma synaptic current.
    p.iv[0] = p.soma_syn.gsyn_e * p.ge_s[i] *
      (p.v_s[i] - p.soma_syn.e_e) +
      p.soma_syn.gsyn_i * p.gi_s[i] * (p.v_s[i] - p.soma_syn.e_i)
    // Compute iv[d+1] = sum of dendrite synaptic currents.
    d_idx = 0
    while d_idx < nd {
      p.iv[d_idx + 1] = p.g_d[d_idx][i][0] +
        p.g_d[d_idx][i][1] +
        p.g_d[d_idx][i][2] +
        p.g_d[d_idx][i][3]
      d_idx = d_idx + 1
    }
    // Sum cs.
    let cs_sum = multipod_sum_cs(p, nd)
    // Δv[0] for soma.
    let theta_i = p.theta[i]
    let dv_soma : Float = (gl *
      ((-p.v_s[i] + el) + dt_slope * expf((p.v_s[i] - theta_i) / dt_slope)) -
      p.w_s[i] - p.iv[0] - cs_sum) / c
    p.v_s[i] = p.v_s[i] + dt * dv_soma
    // Δv[d+1] for each dendrite.
    d_idx = 0
    while d_idx < nd {
      let dd = p.dendrites[d_idx]
      let dv_d : Float = (-(p.v_d[d_idx][i] - el) * dd.gm[i] - p.iv[d_idx + 1] + p.cs[d_idx]) / dd.c[i]
      p.v_d[d_idx][i] = p.v_d[d_idx][i] + dt * dv_d
      d_idx = d_idx + 1
    }
    // w_s[i] += dt * (a * (v_s - El) - w_s) / τw.
    p.w_s[i] = p.w_s[i] + dt * (a * (p.v_s[i] - el) - p.w_s[i]) / tw
    // Reset fire + update threshold.
    p.fire[i] = false
    p.theta[i] = p.theta[i] - dt * (p.theta[i] - p.theta[i]) / p.postspike.tabs_const
    p.after_spike[i] = p.after_spike[i] - 1
    // Spike detection.
    if p.after_spike[i] < 0 {
      if p.v_s[i] > p.theta[i] + 10.0F {
        p.fire[i] = true
        p.theta[i] = p.theta[i] + 10.0F
        p.v_s[i] = 10.0F
        p.w_s[i] = p.w_s[i] + b
      }
    }
    i = i + 1
  }
}

///|
fn multipod_sum_cs(p : Multipod, nd : Int) -> Float {
  let mut s = 0.0F
  let mut d_idx = 0
  while d_idx < nd {
    s = s + p.cs[d_idx]
    d_idx = d_idx + 1
  }
  s
}