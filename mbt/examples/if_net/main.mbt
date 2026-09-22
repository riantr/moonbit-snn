// if_net.mbt — port of SpikingNeuralNetworks.jl/examples/tutorials/networks/IF_net.jl
//
// A small IF network with random connectivity. We use N_exc=32, N_inh=8
// (instead of 3200/800) so the example runs in seconds; the topology
// and connection rules are identical to the original.
//
// Julia:
//   E = SNN.IF(N = 3200, ...)
//   I = SNN.IF(N = 800, ...)
//   EE = SNN.SpikingSynapse(E, E, :ge; conn = (μ = 0.2, p = 0.2))
//   EI = SNN.SpikingSynapse(E, I, :ge; conn = (μ = 0.5, p = 0.2))
//   IE = SNN.SpikingSynapse(I, E, :gi; conn = (μ = 2,   p = 0.2))
//   II = SNN.SpikingSynapse(I, I, :gi; conn = (μ = 2,   p = 0.2))
//   model = SNN.compose(; E, I, EE, EI, IE, II)
//   SNN.sim!(model, 1s)

///|
fn main {
  let n_exc = 32
  let n_inh = 8
  let dt = 0.125F
  let rng = @snn_mbt.Xoshiro::default()

  // Two IF populations with different El. Use the with_el constructor.
  let exc_param = @snn_mbt.IFParameter::with_el(-49.0F)
  let exc_pop = @snn_mbt.IF::new(n_exc, exc_param, rng)
  let inh_param = @snn_mbt.IFParameter::with_el(-60.0F)
  let inh_pop = @snn_mbt.IF::new(n_inh, inh_param, rng)

  // Four random SpikingSynapses
  let ee = @snn_mbt.SpikingSynapse::random(exc_pop, exc_pop, "ge", 0.2F, 0.0F, 0.2F, rng)
  let ei = @snn_mbt.SpikingSynapse::random(exc_pop, inh_pop, "ge", 0.5F, 0.0F, 0.2F, rng)
  let ie = @snn_mbt.SpikingSynapse::random(inh_pop, exc_pop, "gi", 2.0F, 0.0F, 0.2F, rng)
  let ii = @snn_mbt.SpikingSynapse::random(inh_pop, inh_pop, "gi", 2.0F, 0.0F, 0.2F, rng)
  println("if_net.mbt: network built")
  println("  EE connections: " + ee.matrix.nnz().to_string())
  println("  EI connections: " + ei.matrix.nnz().to_string())
  println("  IE connections: " + ie.matrix.nnz().to_string())
  println("  II connections: " + ii.matrix.nnz().to_string())

  // Build a Model with fire monitors. We use fire monitors so we
  // can count spikes per population.
  let model : @snn_mbt.Model = {
    pops: [exc_pop, inh_pop],
    conns: [ee, ei, ie, ii],
    monitors: [
      @snn_mbt.Monitor::new_fire(exc_pop, 0),
      @snn_mbt.Monitor::new_fire(inh_pop, 0),
    ],
  }
  @snn_mbt.sim_for(model, 100.0F * @snn_mbt.ms)

  // Count spikes
  let e_spikes = model.monitors[0].count_spikes()
  let i_spikes = model.monitors[1].count_spikes()
  println("if_net.mbt: simulation done (100ms)")
  println("  v[0] (exc) = " + exc_pop.v[0].to_string())
  println("  v[0] (inh) = " + inh_pop.v[0].to_string())
  println("  exc[0] spikes in 100ms: " + e_spikes.to_string())
  println("  inh[0] spikes in 100ms: " + i_spikes.to_string())
}
