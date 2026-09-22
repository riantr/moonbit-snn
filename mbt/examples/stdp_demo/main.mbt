// stdp_demo.mbt — port of stdp_demo.jl (reduced scale).
//
// Original Julia:
//   N = 100
//   E1 = SNN.IF(N=N); E2 = SNN.IF(N=N)
//   EE = SNN.SpikingSynapse(E1, E2, :ge)
//   for n = 1:E1.N; SNN.connect!(EE, n, n); end
//   for t = 1:N
//     E1.v[t] = 100     # force E1[t] to fire
//     E2.v[N-t+1] = 100 # force E2[N-t+1] to fire
//     SNN.train!(model, duration = 5ms)
//   end
//   # Plot ΔW from EE.records[:W]
//
// MoonBit (v0.7.4): small version (4 IF neurons, identity EE
// connection) with manual STDP step applied after each pair of
// pre/post spikes. Prints initial and final weights and the
// total ΔW across the 4 pairings.

///|
fn main {
  let rng = @snn_mbt.Xoshiro::default()
  let n = 4
  let e1 = @snn_mbt.IF::new(n, @snn_mbt.IFParameter::new(), rng)
  let e2 = @snn_mbt.IF::new(n, @snn_mbt.IFParameter::new(), rng)
  // Identity synapse: E1[i] → E2[i].
  let ee = @snn_mbt.SpikingSynapse::new(e1, e2, "ge")
  for k in 0..<n {
    @snn_mbt.spiking_connect(ee, k + 1, k + 1, 1.0F)
  }

  // STDP state.
  let stdp_param = @snn_mbt.STDPGerstner::new()
  let stdp_vars = @snn_mbt.STDPVariables::new(n, n)

  println("stdp_demo.mbt: STDP pairing protocol (Gerstner 1996)")
  let initial_weights = ee.matrix.vals.copy()
  println("  initial weights: " + initial_weights.to_string())
  let n_syn = initial_weights.length()

  let dt = 0.125F
  let pair_steps : Int = (5.0F / dt).to_int()  // 40 steps × 0.125ms = 5ms
  for t_step in 0..<n {
    // Force E1[t_step] and E2[n-1-t_step] to fire.
    let pre_idx = t_step
    let post_idx = n - 1 - t_step
    e1.v[pre_idx] = 100.0F
    e2.v[post_idx] = 100.0F
    // Run pair_steps steps. STDP integrates over the full duration.
    let mut s = 0
    while s < pair_steps {
      // Step both populations.
      @snn_mbt.step_synapses(e1, dt)
      @snn_mbt.synaptic_current(e1)
      @snn_mbt.step_neuron(e1, dt)
      @snn_mbt.step_synapses(e2, dt)
      @snn_mbt.synaptic_current(e2)
      @snn_mbt.step_neuron(e2, dt)
      // Forward spikes through EE synapse.
      @snn_mbt.forward_synapse(ee, Float::from_int(t_step * pair_steps + s) * dt)
      // Apply STDP.
      let t_now = Float::from_int(t_step * pair_steps + s) * dt
      @snn_mbt.stdp_step(ee.matrix.vals, e1.fire, e2.fire,
        ee.matrix.colptr, ee.matrix.rowptr, stdp_vars, stdp_param, t_now, dt)
      s = s + 1
    }
  }
  println("stdp_demo.mbt: pairing done (4 pairs × 5ms = 20ms total)")
  println("  final weights: " + ee.matrix.vals.to_string())
  let mut total_change = 0.0F
  let mut k = 0
  while k < n_syn {
    let delta = ee.matrix.vals[k] - initial_weights[k]
    total_change = total_change + delta
    k = k + 1
  }
  println("  total ΔW across " + n_syn.to_string() + " synapses: " +
    total_change.to_string())
  // Visualise the STDP kernel that drove these weight changes.
  println("--- STDP kernel (Gerstner 1996) ---")
  @snn_mbt.stdp_kernel_plot(stdp_param)
}