// adex_neuron.mbt — port of SpikingNeuralNetworks.jl/examples/tutorials/neurons/AdEx_neuron.jl
//
// Simplified: runs one AdEx neuron with the Brette-Gerstner 2005
// tonic-spiking defaults, injects 65 pA, and prints the final state.
//
// Julia (simplified):
//   E = SNN.AdEx(; N = 1, param = SNN.AdExParameter(...))
//   SNN.monitor!(E, [:v, :fire, :w], sr = 8kHz)
//   SNN.sim!(; model, duration = 300ms)

///|
fn main {
  let n = 1
  let param = @snn_mbt.AdExParameter::new()
  let rng = @snn_mbt.Xoshiro::default()
  let e_pop = @snn_mbt.AdEx::new(n, param, rng)

  // 65 pA injection
  e_pop.i[0] = 65.0F

  // Build the AdEx model
  let model : @snn_mbt.AdExModel = {
    pops: [e_pop],
    conns: [],
    monitors: [
      @snn_mbt.MonitorAdEx::new_v(e_pop, 0),
    ],
  }

  // Simulate 300 ms
  @snn_mbt.adex_sim_for(model, 300.0F * @snn_mbt.ms)

  // Print final state
  println("adex_neuron.mbt: simulation done")
  println("  v[0] = " + e_pop.v[0].to_string())
  println("  w[0] = " + e_pop.w[0].to_string())
  println("  threshold[0] = " + e_pop.threshold[0].to_string())
  println("  fire[0] = " + e_pop.fire[0].to_string())
  println("  recorded steps: " + model.monitors[0].data.length().to_string())
}
