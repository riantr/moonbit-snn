// morris_lecar.mbt — port of SpikingNeuralNetworks.jl/examples/tutorials/neurons/MorrisLecar_neuron.jl
//
// Simplified: a single Morris-Lecar neuron with 100 pA constant
// injection for 1 second. Prints spike count.

///|
fn main {
  let n = 1
  let rng = @snn_mbt.Xoshiro::default()
  let param = @snn_mbt.MorrisLecarParameter::new()
  let pop = @snn_mbt.MorrisLecar::new(n, param, rng)

  // 100 pA injection
  pop.i[0] = 100.0F

  let total_steps : Int = (1.0F * 1000.0F / 0.125F).to_int()
  let dt = 0.125F
  let spikes = run_ml(pop, total_steps, dt)
  println("morris_lecar.mbt: simulation done (1s)")
  println("  v[0] = " + pop.v[0].to_string())
  println("  w[0] = " + pop.w[0].to_string())
  println("  fire[0] = " + pop.fire[0].to_string())
  println("  total spikes in 1s: " + spikes.to_string())
}

///|
fn run_ml(pop : @snn_mbt.MorrisLecar, total_steps : Int, dt : Float) -> Int {
  let mut spikes : Int = 0
  for _ in 0..<total_steps {
    let prev_fire = pop.fire[0]
    @snn_mbt.step_ml(pop, dt)
    if pop.fire[0] && !prev_fire {
      spikes = spikes + 1
    }
  }
  spikes
}
