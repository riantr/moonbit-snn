// tripod_receptor.mbt — end-to-end TripodHet + ReceptorSynapse wiring.
//
// Demonstrates:
//   - 4 TripodHet neurons (soma + 2 dendrites each)
//   - 4-receptor collection (AMPA, NMDA, GABAa, GABAb)
//   - ReceptorSynapseTripod targeting :d1 with AMPA receptor
//   - Manual sim loop: forward → step_receptors → step_tripod_het
//   - One pre-neuron forced to fire each step; verify the 2-state
//     ODE accumulates g over multiple steps before decaying.
//
// Reference Julia (simplified tripod_current.jl): Tripod +
// ReceptorSynapse on :d1 + PoissonLayer stimulus. We use a forced
// pre-fire pattern instead of PoissonLayer here for determinism.

///|
fn main {
  let rng = @snn_mbt.Xoshiro::default()
  let dt = 0.125F

  // Pre: 1 IF neuron that we'll force to fire each step.
  let pre = @snn_mbt.IF::new(1, @snn_mbt.IFParameter::new(), rng)

  // Post: 4 TripodHet neurons with default AdEx params.
  let base = @snn_mbt.AdExParameter::new()
  let n_post = 4
  let param = @snn_mbt.AdExParameterHet::homogeneous(n_post, base)
  let post : @snn_mbt.TripodHet = @snn_mbt.TripodHet::new(n_post, param, rng)

  // 4-receptor collection (AMPA, NMDA, GABAa, GABAb).
  // Use EyalGluDend-style params from Julia.
  let ampa = @snn_mbt.Receptor::simple(0.0F, 0.26F, 2.0F, 0.73F, "glu")
  let nmda_rec = @snn_mbt.Receptor::nmda(0.0F, 8.0F, 35.0F, 1.31F)
  let gabaa = @snn_mbt.Receptor::simple(-70.0F, 4.8F, 29.0F, 0.27F, "gaba")
  let gabab = @snn_mbt.Receptor::simple(-90.0F, 30.0F, 400.0F, 0.006F, "gaba")
  let receptors = @snn_mbt.Receptors::new(ampa, nmda_rec, gabaa, gabab)
  let nmda_dep = @snn_mbt.NMDAVoltageDependency::eyal()

  // ReceptorSynapseTripod targeting :d1, μ=2.0, σ=0, p=1.0.
  let cs = @snn_mbt.ReceptorSynapseTripod::new(
    pre, post, "d1", receptors, nmda_dep, 2.0F, 0.0F, 1.0F, rng,
  )

  println("tripod_receptor.mbt: TripodHet + ReceptorSynapse on :d1")
  println("  4 TripodHet neurons (soma + d1 + d2 each)")
  println("  4 receptors per synapse (AMPA / NMDA / GABAa / GABAb)")
  println("  connections: " + cs.matrix.nnz().to_string() + " (p=1.0, 1 pre × 4 post)")

  // Drive for 100 ms with pre firing every step (continuous input).
  let duration_ms = 100.0F
  let steps : Int = (duration_ms / dt).to_int()
  let mut n_spikes = 0
  for _step in 0..<steps {
    let _ = _step
    pre.fire[0] = true
    // AMPA = receptor 0 → forward to glu_d1.
    @snn_mbt.forward_receptor_tripod_synapse(cs, 0, 0.0F)
    // Run 2-state ODE for all 4 receptors on :d1.
    @snn_mbt.step_receptors_tripod_synapse(cs, dt)
    // Integrate TripodHet.
    @snn_mbt.step_tripod_het(post, dt)
    for k in 0..<n_post {
      if post.fire[k] {
        n_spikes = n_spikes + 1
      }
    }
    pre.fire[0] = false
  }
  println("")
  println("After " + duration_ms.to_string() + " ms with continuous pre-fire (AMPA on :d1):")
  println("  total spikes (across all 4 neurons) = " + n_spikes.to_string())
  println("")
  // Print final state for sanity.
  println("Final g_state for neuron 0 (AMPA / NMDA / GABAa / GABAb):")
  for r in 0..<4 {
    let label = match r {
      0 => "AMPA"
      1 => "NMDA"
      2 => "GABAa"
      _ => "GABAb"
    }
    let _ = label
    let g = cs.g_state[0 * 4 + r]
    println("  receptor " + r.to_string() + ": g = " + g.to_string())
  }
  println("")
  println("Final ge_out (sum across AMPA + NMDA):")
  for k in 0..<n_post {
    println("  neuron " + k.to_string() + ": ge_out = " + cs.ge_out[k].to_string())
  }
}