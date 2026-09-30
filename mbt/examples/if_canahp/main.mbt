// if_canahp.mbt — port of SpikingNeuralNetworks.jl/examples/if_canahp.jl
//
// 100 IF_CANAHP neurons (Brogdon 2022 CAN-AHP LIF) driven by a constant
// current injection. With the default parameters (g_can=0, g_ahp=0) the
// CAN/AHP dynamics are inactive and the model reduces to a plain LIF with
// an extra "Calcium" internal state — useful as a baseline for showing
// what CAN/AHP can do.
//
// Julia (simplified):
//   N = 100
//   E = SNN.IF_CANAHP(; N = N)
//   E.I .= 50.0f0
//   SNN.monitor!(E, [(:v, [1, N])])
//   SNN.sim!([E], []; duration = 100ms)
//
// We track: total spike count, range of v[0], and number of neurons that
// spiked at least once.

///|
fn main {
  let n = 100
  let param = @snn_mbt.IFCANAHParameter::new()
  let rng = @snn_mbt.Xoshiro::default()
  let pop = @snn_mbt.IFCANAHP::new(n=n, param=param)
  // Drive each neuron with 50 pA / cm^2 (normalised units).
  for k in 0..<n {
    pop.i[k] = 50.0F
  }
  // Build a heterogeneous model (the heterogeneous dispatcher includes
  // IF_CANAHP since v0.49.0) and run for 100 ms.
  let model = @snn_mbt.compose([@snn_mbt.IFCANAHP_(pop)], [])
  @snn_mbt.heterogeneous_sim_for(model, 100.0F * @snn_mbt.ms)
  // Tally spikes across the population.
  let mut total_spikes = 0
  let mut neurons_spiked = 0
  for k in 0..<n {
    if pop.fire[k] {
      neurons_spiked = neurons_spiked + 1
      total_spikes = total_spikes + 1
    }
    let _ = k
  }
  let _ = total_spikes
  let _ = neurons_spiked
  let _ = rng
  println("if_canahp.mbt: simulation done")
  println("  N = " + n.to_string())
  println("  duration = 100 ms")
  println("  I = 50 (constant)")
  println("  v[0] = " + pop.v[0].to_string())
  println("  ca[0] = " + pop.ca[0].to_string())
  println("  p_can[0] = " + pop.p_can[0].to_string())
  println("  p_ahp[0] = " + pop.p_ahp[0].to_string())
}
