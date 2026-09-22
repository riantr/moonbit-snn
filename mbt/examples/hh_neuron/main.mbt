// hh_neuron.mbt — port of SpikingNeuralNetworks.jl/examples/tutorials/neurons/HH_neuron.jl
//
// Simplified: runs a single HH neuron with the Travis Brians 2017
// parameter set for 1 s, with a 10 pA constant injection, and prints
// the final state plus spike count.

///|
fn main {
  let n = 1
  let rng = @snn_mbt.Xoshiro::default()
  let param = @snn_mbt.HHParameter::new()
  let pop = @snn_mbt.HH::new(n, param, rng)

  // 10 pA injection
  pop.i[0] = 10.0F

  let total_steps : Int = (1.0F * 1000.0F / 0.125F).to_int()
  let dt = 0.125F
  let spikes = run_hh(pop, total_steps, dt)
  println("hh_neuron.mbt: simulation done (1s)")
  println("  v[0] = " + pop.v[0].to_string())
  println("  m[0] = " + pop.m[0].to_string())
  println("  n[0] = " + pop.n_gate[0].to_string())
  println("  h[0] = " + pop.h[0].to_string())
  println("  ge[0] = " + pop.ge[0].to_string())
  println("  gi[0] = " + pop.gi[0].to_string())
  println("  fire[0] = " + pop.fire[0].to_string())
  println("  total spikes in 1s: " + spikes.to_string())
}

///|
fn run_hh(pop : @snn_mbt.HH, total_steps : Int, dt : Float) -> Int {
  let mut spikes : Int = 0
  for _ in 0..<total_steps {
    let prev_fire = pop.fire[0]
    @snn_mbt.step_hh(pop, dt)
    if pop.fire[0] && !prev_fire {
      spikes = spikes + 1
    }
  }
  spikes
}
