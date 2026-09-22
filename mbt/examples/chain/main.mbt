// chain.mbt — port of SpikingNeuralNetworks.jl/examples/chain.jl
//
// Original Julia:
//   using SNNPlots
//   using SpikingNeuralNetworks
//   SNN.@load_units
//
//   N = 3
//   E = SNN.IF(; N = N)
//   EE = SNN.SpikingSynapse(E, E, :ge; μ = 0.5, p = 0.8)
//   for n = 1:(N-1)
//       SNN.connect!(EE, n, n + 1, 50)
//   end
//   E.I[1] = 30
//
//   SNN.monitor!(E, [(:v, [1, N])])
//   SNN.sim!([E], [EE]; duration = 100ms)
//   SNN.vecplot(E, :v)
//
// MoonBit differences (vs. the Julia source):
//   * Units are applied explicitly: `100.0 * ms` instead of `100ms`.
//   * `SNN.IF(N=N)` → `IF::new(N, IFParameter::new(), rng)`.
//   * The full SpikingSynapse constructor with random connectivity
//     is not yet implemented in v0.0.1; we construct an empty
//     synapse and add the chain connections by hand.

///|
fn main {
  let n = 3
  let param = @snn_mbt.IFParameter::new()
  let rng = @snn_mbt.Xoshiro::default()
  let e_pop = @snn_mbt.IF::new(n, param, rng)

  // Inject 30 pA into the first neuron
  e_pop.i[0] = 30.0F

  // Build the chain synapse (empty, then connect 1→2 and 2→3 with w=50)
  let ee = @snn_mbt.SpikingSynapse::new(e_pop, e_pop, "ge")
  @snn_mbt.spiking_connect(ee, 1, 2, 50.0F)
  @snn_mbt.spiking_connect(ee, 2, 3, 50.0F)

  // Build the model
  let model : @snn_mbt.Model = {
    pops: [e_pop],
    conns: [ee],
    monitors: [
      @snn_mbt.Monitor::new_v(e_pop, 0),
      @snn_mbt.Monitor::new_v(e_pop, 2),
    ],
  }

  // Simulate for 100 ms
  @snn_mbt.sim_for(model, 100.0F * @snn_mbt.ms)

  // Print final values
  println("chain.mbt: simulation done")
  println("  v[0] = " + e_pop.v[0].to_string())
  println("  v[1] = " + e_pop.v[1].to_string())
  println("  v[2] = " + e_pop.v[2].to_string())
  println("  recorded steps: " + model.monitors[0].data.length().to_string())
  // v[0] monitor summary
  model.monitors[0].dump_summary()
}
