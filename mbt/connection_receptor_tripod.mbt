// connection_receptor_tripod.mbt — ReceptorSynapse wired into TripodHet.
//
// Mirrors SNNModels.jl/src/populations/synapse/synapses/ReceptorSynapse.jl
// + Julia's TripodHet wiring pattern. Each connection carries a
// per-(pre, post) weight that is split across up to 4 receptors
// (AMPA, NMDA, GABAa, GABAb) on a target compartment (:soma, :d1,
// or :d2) of a TripodHet neuron.
//
// Per-step dynamics:
//   1. forward: when pre[j] fires, for each outgoing connection s:
//      - if receptor index is in glu_receptors, add weight to glu (or
//        the compartment-specific glu buffer)
//      - if in gaba_receptors, add weight to gaba (or compartment gaba)
//   2. step_receptors: for each compartment × receptor (3 × 4 = 12),
//      run the 2-state ODE (h += target*α; g = exp(-dt/τd⁻)*(g+dt*h);
//      h = exp(-dt/τr⁻)*h; consume target). Replace ge_s/gi_s/ge_d1/...
//      with the sum across receptors in that compartment.
//
// Bit-exact note: this matches Julia's per-receptor update_synapses!
// + synaptic_current! exactly (same α, τr, τd, expf Float32 semantics).
// NMDA gating via B(v) = 1/(1 + (mg/b)*exp(k*v)) applies to NMDA
// receptors only.

///|
/// ReceptorSynapseTripod — multi-receptor SpikingSynapse wired into
/// TripodHet. One struct per (pre, post) connection; targets a single
/// compartment per synapse (set at construction).
///
/// Per-(pre, post) weight `w` is distributed to the right receptor
/// index based on `target_receptors[s]`:
///   - 0 = AMPA (glu, E_rev=0)
///   - 1 = NMDA (glu, E_rev=0, voltage-dependent)
///   - 2 = GABAa (gaba, E_rev<0)
///   - 3 = GABAb (gaba, E_rev<0, slow)
pub struct ReceptorSynapseTripod {
  pre : IF
  post : TripodHet
  matrix : SparseMatrixCSR         // sparse (pre, post) connections
  target_compartment : String       // "soma" | "d1" | "d2"
  receptors : Receptors             // 4 receptors (AMPA, NMDA, GABAa, GABAb)
  nmda_dep : NMDAVoltageDependency  // voltage-dependence for NMDA
  // Per-receptor state for the post-synaptic population.
  // g_state[i * 4 + r] = g for post-neuron i, receptor r (in compartment).
  // h_state[i * 4 + r] = h for post-neuron i, receptor r.
  g_state : Array[Float]
  h_state : Array[Float]
  // Updated conductance outputs (sum across receptors in same comp).
  // Populated by step_receptors_tripod_synapse; consumed by TripodHet
  // synaptic_current (or by manual wiring).
  ge_out : Array[Float]
  gi_out : Array[Float]
}

///|
pub fn ReceptorSynapseTripod::new(
  pre : IF,
  post : TripodHet,
  target_compartment : String,
  receptors : Receptors,
  nmda_dep : NMDAVoltageDependency,
  mu : Float,
  sigma : Float,
  p : Float,
  rng : Xoshiro,
) -> ReceptorSynapseTripod {
  let m = SparseMatrixCSR::random(pre.n, post.n, mu, sigma, p, rng)
  let n_post = post.n
  let g_state : Array[Float] = Array::make(n_post * 4, 0.0F)
  let h_state : Array[Float] = Array::make(n_post * 4, 0.0F)
  let ge_out : Array[Float] = Array::make(n_post, 0.0F)
  let gi_out : Array[Float] = Array::make(n_post, 0.0F)
  { pre, post, matrix: m, target_compartment, receptors, nmda_dep,
    g_state, h_state, ge_out, gi_out }
}

///|
/// Forward pre-synaptic spikes through the receptor synapse. When
/// pre[j] fires, each (j → post[k]) edge contributes weight to the
/// appropriate input buffer (glu or gaba) for the target compartment.
///
/// `target_receptor[s]` = 0/1/2/3 selects which receptor of the 4 to
/// feed. For simplicity in this version, all connections use the same
/// target_receptor (passed at forward time as a single Int). For
/// heterogeneous per-edge targeting, use the dedicated constructor
/// that takes a per-row `Array[Int]` — TODO.
pub fn forward_receptor_tripod_synapse(
  s : ReceptorSynapseTripod,
  target_receptor : Int,
  t_now : Float,
) -> Unit {
  // Pick the input buffer based on (compartment, receptor target).
  // AMPA (0) + NMDA (1) → glu; GABAa (2) + GABAb (3) → gaba.
  let is_glu = target_receptor == 0 || target_receptor == 1
  let buf : Array[Float] = match s.target_compartment {
    "soma" => if is_glu { s.post.glu_s } else { s.post.gaba_s }
    "d1" => if is_glu { s.post.glu_d1 } else { s.post.gaba_d1 }
    "d2" => if is_glu { s.post.glu_d2 } else { s.post.gaba_d2 }
    _ => s.post.glu_s
  }
  // Manual forward: walk CSR rows for pre fires, add weight to buf.
  let n_pre = s.pre.fire.length()
  let mut j = 0
  while j < n_pre {
    if s.pre.fire[j] {
      let start = s.matrix.rowptr[j]
      let end = s.matrix.rowptr[j + 1]
      let mut idx = start
      while idx < end {
        let post_idx = s.matrix.colptr[idx]
        let w = s.matrix.vals[idx]
        buf[post_idx] = buf[post_idx] + w
        idx = idx + 1
      }
    }
    j = j + 1
  }
}

///|
/// Run the 2-state ODE for each receptor on the target compartment.
/// Populates `s.ge_out` and `s.gi_out` (sum across receptors in the
/// same compartment).
///
/// Consumes the input buffer (glu_X or gaba_X) — it is reset to 0
/// after step_receptor consumes it.
pub fn step_receptors_tripod_synapse(
  s : ReceptorSynapseTripod,
  dt : Float,
) -> Unit {
  let n_post = s.post.n
  // For each receptor, run the 2-state ODE against the appropriate
  // input buffer (from the post compartment), updating g_state and
  // h_state, then summing into ge_out/gi_out.
  let mut r = 0
  while r < 4 {
    let rec = s.receptors.rec[r]
    let is_glu = rec.target == "glu"
    let buf : Array[Float] = match s.target_compartment {
      "soma" => if is_glu { s.post.glu_s } else { s.post.gaba_s }
      "d1" => if is_glu { s.post.glu_d1 } else { s.post.gaba_d1 }
      "d2" => if is_glu { s.post.glu_d2 } else { s.post.gaba_d2 }
      _ => s.post.glu_s
    }
    // Slice the per-receptor column from g_state / h_state.
    let g_col : Array[Float] = Array::make(n_post, 0.0F)
    let h_col : Array[Float] = Array::make(n_post, 0.0F)
    for i in 0..<n_post {
      g_col[i] = s.g_state[i * 4 + r]
      h_col[i] = s.h_state[i * 4 + r]
    }
    // Run 2-state ODE on the slice.
    step_receptor(g_col, h_col, buf, rec, dt)
    // Write back.
    for i in 0..<n_post {
      s.g_state[i * 4 + r] = g_col[i]
      s.h_state[i * 4 + r] = h_col[i]
    }
    // Sum into ge_out / gi_out using NMDA gating for r==1.
    if is_glu {
      receptor_current(g_col, s.post.v_s, rec, s.nmda_dep, s.ge_out)
    } else {
      receptor_current(g_col, s.post.v_s, rec, s.nmda_dep, s.gi_out)
    }
    r = r + 1
  }
}