// if_neuron.mbt — port of SpikingNeuralNetworks.jl/examples/tutorials/neurons/IF_neuron.jl
//
// A single IF neuron driven by two Poisson stimulus trains
// (one excitatory, one inhibitory) for 10 s. The original Julia
// records :v, :fire, :ge, :gi at 1 kHz and plots. We run a shorter
// version and print the final state.
//
// Julia (simplified):
//   E = SNN.IF(; N = 1)
//   rate = 50Hz * 300
//   stim1 = SNN.PoissonStimulus(E, :ge, param = rate, μ = 1.0f0)
//   stim2 = SNN.PoissonStimulus(E, :gi, param = rate, μ = 5.1f0)
//   SNN.monitor!(E, [:v, :fire, :ge, :gi], sr = 1000Hz)
//   SNN.sim!(; model, duration = 10s)

///|
fn main {
  let n = 1
  let param = @neuron.IFParameter::new()
  let rng = @core.Xoshiro::default()
  let e_pop = @neuron.IF::new(n, param, rng)

  // 50Hz * 300 = 15000 Hz
  let rate = 50.0F * 300.0F
  // E[ge per step] = rate * dt = 15000 * 0.000125 = 1.875
  // E[gi per step] = rate * dt * 5.1 = 9.5625

  let sE = @stimulus.PoissonStimulusIF::new(e_pop, "ge", rate, rng)
  let sI = @stimulus.PoissonStimulusIF::new(e_pop, "gi", rate * 5.1F, rng)
  // Set the inhibitory μ to 5.1 (matches Julia)
  @stimulus.PoissonFixed::set_mu(sI.param, 5.1F)

  // Build the model
  let model : @snn_mbt.Model = {
    pops: [e_pop],
    conns: [],
    monitors: [
      @snn_mbt.Monitor::new_v(e_pop, 0),
    ],
  }

  // 1 s simulation (10s in the Julia example is too slow for MoonBit's
  // Knuth Poisson sampler at these rates; expected rate is the same).
  let total_steps : Int = (1.0F * 1000.0F / 0.125F).to_int()
  let dt = 0.125F
  let time = @core.Time::new()
  @core.set_dt(time, dt)
  @snn_mbt.record_zero(model)
  let mut spikes : Int = 0
  for _ in 0..<total_steps {
    @stimulus.stimulate_if(sE, time.t[0], dt)
    @stimulus.stimulate_if(sI, time.t[0], dt)
    // Integrate
    @neuron.step_synapses(e_pop, dt)
    @neuron.synaptic_current(e_pop)
    let prev_fire = e_pop.fire[0]
    @neuron.step_neuron(e_pop, dt)
    if e_pop.fire[0] && !prev_fire {
      spikes = spikes + 1
    }
    @snn_mbt.record_one(model.monitors[0], time.t[0])
    @core.update_time(time, dt)
  }

  println("if_neuron.mbt: simulation done (1s of 10s)")
  println("  v[0] = " + e_pop.v[0].to_string())
  println("  fire[0] = " + e_pop.fire[0].to_string())
  println("  total spikes in 1s: " + spikes.to_string())
  println("  recorded steps: " + model.monitors[0].data.length().to_string())
  println("--- v[0] trace (ASCII plot) ---")
  model.monitors[0].ascii_plot(width=70, height=15)
}
