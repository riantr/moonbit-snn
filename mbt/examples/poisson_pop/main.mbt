// poisson_pop.mbt — port of SpikingNeuralNetworks.jl/examples/tutorials/neurons/PoissonPop.jl
//
// Simplified: a population of 1000 Poisson neurons firing at 5 Hz
// each. Simulate 100 s and count total spikes. The expected total
// is 1000 * 5 * 100 = 500,000 spikes (with variance from sampling).

///|
fn main {
  let n_neurons = 1000
  let rate_hz = 5.0F
  let duration_s = 100.0F
  let dt = 0.125F
  let rng = @snn_mbt.Xoshiro::default()
  let pop = @snn_mbt.Poisson::new(n_neurons, rate_hz * @snn_mbt.hz, rng)
  let total_steps : Int = (duration_s * 1000.0F / dt).to_int()
  let mut total_fires : Int = 0
  for _ in 0..<total_steps {
    @snn_mbt.step_poisson(pop, dt)
    for k in 0..<n_neurons {
      if pop.fire[k] {
        total_fires = total_fires + 1
      }
    }
  }
  let expected = n_neurons * rate_hz.to_int() * duration_s.to_int()
  println("poisson_pop.mbt: simulation done (" + duration_s.to_string() + "s)")
  println("  N = " + n_neurons.to_string())
  println("  rate = " + rate_hz.to_string() + " Hz")
  println("  total fires = " + total_fires.to_string())
  println("  expected = " + expected.to_string())
}
