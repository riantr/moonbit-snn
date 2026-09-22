// poisson_if.mbt — port of afferent_response.jl (simplified).
//
// Original Julia (afferent_response.jl):
//   - 4000 exc + 1000 inh IF neurons
//   - 100-neuron Poisson excitatory afferent at 20 Hz
//   - 100-neuron Poisson inhibitory afferent at 20 Hz
//   - 5 second simulation
//   - measures firing rate
//
// MoonBit (v0.4.0): a smaller version for fast iteration.
//   - 32 exc + 8 inh IF neurons
//   - Poisson exc at 5 kHz onto :ge with μ=2.0 nS
//   - Poisson inh at 200 Hz onto :gi with μ=2.0 nS
//   - 200 ms simulation
//   - reports spike count in E[0] and I[0] via Monitors

///|
fn main {
  let rng = @snn_mbt.Xoshiro::default()
  let param = @snn_mbt.IFParameter::new()
  // E: 32 exc, I: 8 inh (32+8 = 40 total, like if_net)
  let e_pop = @snn_mbt.IF::new(32, param, rng)
  let i_pop = @snn_mbt.IF::new(8, param, rng)

  // Build sparse internal connectivity. Same parameters as if_net.jl.
  let ee = @snn_mbt.SpikingSynapse::random(e_pop, e_pop, "ge", 0.27F, 0.0F, 0.05F, rng)
  let ei = @snn_mbt.SpikingSynapse::random(e_pop, i_pop, "ge", 0.27F, 0.0F, 0.05F, rng)
  let ie = @snn_mbt.SpikingSynapse::random(i_pop, e_pop, "gaba", 4.5F, 0.0F, 0.05F, rng)
  let ii = @snn_mbt.SpikingSynapse::random(i_pop, i_pop, "gaba", 4.5F, 0.0F, 0.05F, rng)

  // External Poisson inputs onto :ge (exc) and :gi (inh).
  // 5 kHz onto :ge with μ=2.0 nS — strong enough to drive E cells to fire.
  // 200 Hz onto :gi with μ=2.0 nS — moderate inhibition.
  let stim_e = @snn_mbt.PoissonStimulusIF::new(e_pop, "ge", 5.0F, rng)
  let stim_i = @snn_mbt.PoissonStimulusIF::new(i_pop, "gi", 0.2F, rng)
  @snn_mbt.PoissonFixed::set_mu(stim_e.param, 2.0F)
  @snn_mbt.PoissonFixed::set_mu(stim_i.param, 2.0F)

  // Fire monitors for E[0] and I[0]. Driven by step_heterogeneous so
  // we can count spikes after the sim.
  let fm_e = @snn_mbt.Monitor::new_fire(e_pop, 0)
  let fm_i = @snn_mbt.Monitor::new_fire(i_pop, 0)

  // Build the heterogeneous model
  let model = @snn_mbt.compose(
    [@snn_mbt.IF_(e_pop), @snn_mbt.IF_(i_pop)],
    [ee, ei, ie, ii],
    stims=[@snn_mbt.PoissonIF_(stim_e), @snn_mbt.PoissonIF_(stim_i)],
    monitors=[fm_e, fm_i],
  )

  // Print initial state
  println("poisson_if.mbt: network built")
  println("  E: 32 IF neurons, I: 8 IF neurons")
  println("  EE connections: " + ee.matrix.nnz().to_string())
  println("  EI connections: " + ei.matrix.nnz().to_string())
  println("  IE connections: " + ie.matrix.nnz().to_string())
  println("  II connections: " + ii.matrix.nnz().to_string())

  // Simulate 200 ms
  @snn_mbt.heterogeneous_sim_for(model, 200.0F * @snn_mbt.ms)

  // Count spikes in E[0] and I[0] from the recorded fire monitor.
  let e_spikes = fm_e.count_spikes()
  let i_spikes = fm_i.count_spikes()
  println("poisson_if.mbt: simulation done (200ms)")
  println("  E[0] v = " + e_pop.v[0].to_string())
  println("  I[0] v = " + i_pop.v[0].to_string())
  println("  E[0] spikes in 200ms: " + e_spikes.to_string())
  println("  I[0] spikes in 200ms: " + i_spikes.to_string())
  // Effective firing rate (Hz) over the 200ms window:
  let e_hz = Float::from_int(e_spikes) * 1000.0F / 200.0F
  let i_hz = Float::from_int(i_spikes) * 1000.0F / 200.0F
  println("  E[0] firing rate: " + e_hz.to_string() + " Hz")
  println("  I[0] firing rate: " + i_hz.to_string() + " Hz")
}
