// potjans.mbt — port of Potjans-Diesmann 2014 cortical microcircuit
// (simplified: 2 layers instead of 8).
//
// Original Julia (multilayer_potjans-diesmann2015.jl):
//   - 8 populations (E23, I23, E4, I4, E5, I5, E6, I6)
//   - Full cortical-layer cell counts (E23=20683, E4=21915, ...)
//   - 64 SpikingSynapses with layer-specific μ and p
//   - 15s simulation
//
// MoonBit (v0.6.0): a tiny 2-layer version.
//   - E (10 exc IF) + I (3 inh IF), each with tonic Poisson drive
//   - EE, EI, IE, II SpikingSynapses
//   - 200ms simulation, count spikes
//
// Layer parameters are loosely based on Potjans-Diesmann Table 1,
// scaled for a tiny network.

///|
fn main {
  let rng = @snn_mbt.Xoshiro::default()
  let ne = 10
  let ni = 3
  let exc_param = @snn_mbt.IFParameter::with_el(-60.0F)
  let inh_param = @snn_mbt.IFParameter::with_el(-60.0F)
  let e_pop = @snn_mbt.IF::new(ne, exc_param, rng)
  let i_pop = @snn_mbt.IF::new(ni, inh_param, rng)

  // Layer-specific connection probabilities and weights (scaled).
  // Format: (μ, p) — μ is mean synaptic weight, p is connection probability.
  let ee = @snn_mbt.SpikingSynapse::random(e_pop, e_pop, "ge", 0.5F, 0.0F, 0.2F, rng)
  let ei = @snn_mbt.SpikingSynapse::random(e_pop, i_pop, "ge", 0.5F, 0.0F, 0.2F, rng)
  let ie = @snn_mbt.SpikingSynapse::random(i_pop, e_pop, "gaba", 2.0F, 0.0F, 0.2F, rng)
  let ii = @snn_mbt.SpikingSynapse::random(i_pop, i_pop, "gaba", 2.0F, 0.0F, 0.2F, rng)

  // External Poisson drive onto :ge (background input).
  let stim_e = @snn_mbt.PoissonStimulusIF::new(e_pop, "ge", 2.0F, rng)
  @snn_mbt.PoissonFixed::set_mu(stim_e.param, 2.0F)
  let stim_i = @snn_mbt.PoissonStimulusIF::new(i_pop, "ge", 2.0F, rng)
  @snn_mbt.PoissonFixed::set_mu(stim_i.param, 2.0F)

  // Monitors for spike counting.
  let fm_e = @snn_mbt.Monitor::new_fire(e_pop, 0)
  let fm_i = @snn_mbt.Monitor::new_fire(i_pop, 0)

  println("potjans.mbt: network built (simplified 2-layer Potjans-Diesmann)")
  println("  E: " + ne.to_string() + " IF neurons")
  println("  I: " + ni.to_string() + " IF neurons")
  println("  EE connections: " + ee.matrix.nnz().to_string())
  println("  EI connections: " + ei.matrix.nnz().to_string())
  println("  IE connections: " + ie.matrix.nnz().to_string())
  println("  II connections: " + ii.matrix.nnz().to_string())

  let model = @snn_mbt.compose(
    [@snn_mbt.IF_(e_pop), @snn_mbt.IF_(i_pop)],
    [ee, ei, ie, ii],
    stims=[@snn_mbt.PoissonIF_(stim_e), @snn_mbt.PoissonIF_(stim_i)],
    monitors=[fm_e, fm_i],
  )

  // Simulate 200ms.
  @snn_mbt.heterogeneous_sim_for(model, 200.0F * @snn_mbt.ms)

  let e_spikes = fm_e.count_spikes()
  let i_spikes = fm_i.count_spikes()
  let e_hz = Float::from_int(e_spikes) * 1000.0F / 200.0F
  let i_hz = Float::from_int(i_spikes) * 1000.0F / 200.0F
  println("potjans.mbt: simulation done (200ms)")
  println("  E[0] spikes: " + e_spikes.to_string() + " (" + e_hz.to_string() + " Hz)")
  println("  I[0] spikes: " + i_spikes.to_string() + " (" + i_hz.to_string() + " Hz)")
  println("  E[0] final v: " + e_pop.v[0].to_string())
  println("  I[0] final v: " + i_pop.v[0].to_string())
}