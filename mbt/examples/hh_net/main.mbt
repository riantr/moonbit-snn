// hh_net.mbt — port of HH_net.jl (simplified).
//
// Original Julia:
//   E = SNN.HH(N = 3200)
//   I = SNN.HH(N = 800)
//   EE = SpikingSynapse(E, E, :ge; μ=6, p=0.02)
//   EI = SpikingSynapse(E, I, :ge; μ=6, p=0.02)
//   IE = SpikingSynapse(I, E, :gi; μ=67, p=0.02)
//   II = SpikingSynapse(I, I, :gi; μ=67, p=0.02)
//   sim!(P, C; dt = 0.01, duration = 100ms)
//
// MoonBit (v0.7.1): tiny HH network (8 exc + 4 inh).
// EE/EI → :ge (μ=6, p=0.5); IE/II → :gi (μ=67, p=0.5).
// 200ms sim, manual loop (SpikingSynapseHH + step_hh).
// Original Julia uses 3200+800 neurons; we use 8+4 for speed.

///|
fn main {
  let rng = @snn_mbt.Xoshiro::default()
  let ne = 8
  let ni = 4
  let e_pop = @snn_mbt.HH::new(ne, @snn_mbt.HHParameter::new(), rng)
  let i_pop = @snn_mbt.HH::new(ni, @snn_mbt.HHParameter::new(), rng)

  // Random connectivity.
  let ee = @snn_mbt.SpikingSynapseHH::random(e_pop, e_pop, "ge", 6.0F, 0.0F, 0.5F, rng)
  let ei = @snn_mbt.SpikingSynapseHH::random(e_pop, i_pop, "ge", 6.0F, 0.0F, 0.5F, rng)
  let ie = @snn_mbt.SpikingSynapseHH::random(i_pop, e_pop, "gi", 67.0F, 0.0F, 0.5F, rng)
  let ii = @snn_mbt.SpikingSynapseHH::random(i_pop, i_pop, "gi", 67.0F, 0.0F, 0.5F, rng)

  println("hh_net.mbt: network built (simplified HH balanced network)")
  println("  E: " + ne.to_string() + " HH neurons")
  println("  I: " + ni.to_string() + " HH neurons")
  println("  EE connections: " + ee.matrix.nnz().to_string())
  println("  EI connections: " + ei.matrix.nnz().to_string())
  println("  IE connections: " + ie.matrix.nnz().to_string())
  println("  II connections: " + ii.matrix.nnz().to_string())

  // Manual sim loop: 200 ms = 1600 steps at dt=0.125F.
  // (Original Julia uses dt=0.01; we keep dt=0.125 to stay consistent
  // with the rest of the codebase. The dynamics are similar but
  // slightly less accurate.)
  let dt = 0.125F
  let total_steps : Int = (200.0F / dt).to_int()
  let mut total_e_spikes = 0
  let mut total_i_spikes = 0
  for _ in 0..<total_steps {
    // Synapses first (propagate pre spikes).
    @snn_mbt.forward_hh_synapse(ee)
    @snn_mbt.forward_hh_synapse(ei)
    @snn_mbt.forward_hh_synapse(ie)
    @snn_mbt.forward_hh_synapse(ii)
    // Step HH (decay ge/gi, update gates, integrate v).
    @snn_mbt.step_hh(e_pop, dt)
    @snn_mbt.step_hh(i_pop, dt)
    // Track rising-edge spikes.
    for f in e_pop.fire {
      if f { total_e_spikes = total_e_spikes + 1 }
    }
    for f in i_pop.fire {
      if f { total_i_spikes = total_i_spikes + 1 }
    }
  }
  println("hh_net.mbt: simulation done (200ms)")
  println("  E[0] v = " + e_pop.v[0].to_string())
  println("  I[0] v = " + i_pop.v[0].to_string())
  println("  E total spikes (rising-edge count): " + total_e_spikes.to_string())
  println("  I total spikes (rising-edge count): " + total_i_spikes.to_string())
}