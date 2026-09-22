// izhikevich.mbt — port of SpikingNeuralNetworks.jl/examples/tutorials/neurons/Izikievich_neuron.jl
//
// Simplified: runs a single regular-spiking (RS) Izhikevich neuron
// for 2 s with a 10 pA constant input. Prints the final state.

///|
fn main {
  let n = 1
  let rng = @snn_mbt.Xoshiro::default()
  let param = @snn_mbt.IZParameter::rs()
  let pop = @snn_mbt.IZ::new(n, param, rng)

  // 10 pA injection
  pop.i[0] = 10.0F

  // Run for 2 s with manual loop
  let total_steps : Int = (2.0F * 1000.0F / 0.125F).to_int()
  let dt = 0.125F
  let spikes = run_izhikevich(pop, total_steps, dt)
  println("izhikevich.mbt: simulation done (2s)")
  println("  v[0] = " + pop.v[0].to_string())
  println("  u[0] = " + pop.u[0].to_string())
  println("  ge[0] = " + pop.ge[0].to_string())
  println("  gi[0] = " + pop.gi[0].to_string())
  println("  fire[0] = " + pop.fire[0].to_string())
  println("  total spikes in 2s: " + spikes.to_string())
}

///|
fn run_izhikevich(pop : @snn_mbt.IZ, total_steps : Int, dt : Float) -> Int {
  let mut spikes : Int = 0
  for _ in 0..<total_steps {
    let prev_fire = pop.fire[0]
    @snn_mbt.step_iz(pop, dt)
    if pop.fire[0] && !prev_fire {
      spikes = spikes + 1
    }
  }
  spikes
}
