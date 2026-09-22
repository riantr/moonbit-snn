// tsodyks.mbt — port of Tsodyks1997_ParadoxicalEffect.jl (scaled down).
//
// Original Julia:
//   E = AdEx(N=2000, AdExSinExpParameter(El=-65mV))
//   I = IF(N=500, IFSinExpParameter())
//   EE = SpikingSynapse(E, E, :ge; μ=1,  p=0.2)
//   EI = SpikingSynapse(E, I, :ge; μ=5,  p=0.2)
//   IE = SpikingSynapse(I, E, :gi; μ=10, p=0.2)
//   II = SpikingSynapse(I, I, :gi; μ=10, p=0.2)
//   S  = PoissonStimulus(E, :ge; rate=800Hz, μ=1.7)
//   sim!(P, C, S; duration=3s)
//
// MoonBit (v0.7.2): smaller version (8 AdEx + 4 IF), 1s sim.
// We use SpikingSynapseAdEx for AdEx→AdEx. The AdEx→IF coupling is
// done manually: copy AdEx fire flags into IF's glu array each step.
// Poisson input is approximated via CurrentStimulusArray with noise.

///|
fn main {
  let rng = @snn_mbt.Xoshiro::default()
  let ne = 8
  let ni = 4

  // AdEx excitatory population with default params.
  let e_param = @snn_mbt.AdExParameter::new()
  let e_pop = @snn_mbt.AdEx::new(ne, e_param, rng)
  // IF inhibitory population.
  let i_param = @snn_mbt.IFParameter::new()
  let i_pop = @snn_mbt.IF::new(ni, i_param, rng)

  // Set up AdEx→AdEx synapses via SpikingSynapseAdEx.
  let ee = @snn_mbt.SpikingSynapseAdEx::random(e_pop, e_pop, "ge", 1.0F, 0.0F, 0.2F, rng)

  // Background drive via CurrentStimulusArray: 1000 pA baseline +
  // 200 pA noise. Drives v above threshold so cells fire.
  let stim = @snn_mbt.CurrentStimulusArray::new(e_pop.i, e_pop.n, 1000.0F, rng,
    noise_sigma=200.0F)

  println("tsodyks.mbt: network built (scaled-down Tsodyks1997)")
  println("  E: " + ne.to_string() + " AdEx neurons")
  println("  I: " + ni.to_string() + " IF neurons")
  println("  EE: " + ee.matrix.nnz().to_string() + " connections (AdEx→AdEx)")

  // Manual sim loop.
  let dt = 0.125F
  let total_steps : Int = (1000.0F / dt).to_int()
  let mut total_e_spikes = 0
  let mut total_i_spikes = 0
  for _ in 0..<total_steps {
    // Drive E with current noise (Poisson input approximation).
    @snn_mbt.stimulate_current_array(stim)
    // Forward AdEx→AdEx synapses.
    @snn_mbt.forward_adex_synapse(ee)
    // Step AdEx.
    @snn_mbt.adex_step_synapses(e_pop, dt)
    @snn_mbt.adex_synaptic_current(e_pop)
    @snn_mbt.step_adex(e_pop, dt)
    // AdEx→IF coupling: copy AdEx fire flags into IF's glu array.
    for k in 0..<ne {
      if e_pop.fire[k] {
        i_pop.glu[k % ni] = i_pop.glu[k % ni] + 5.0F
      }
    }
    // Step IF (now with AdEx→IF input).
    @snn_mbt.step_synapses(i_pop, dt)
    @snn_mbt.synaptic_current(i_pop)
    @snn_mbt.step_neuron(i_pop, dt)
    // Reset IF glu/gaba for next step.
    for k in 0..<ni {
      i_pop.glu[k] = 0.0F
    }
    // Count spikes (rising edge of fire).
    for f in e_pop.fire {
      if f { total_e_spikes = total_e_spikes + 1 }
    }
    for f in i_pop.fire {
      if f { total_i_spikes = total_i_spikes + 1 }
    }
  }
  println("tsodyks.mbt: simulation done (1s)")
  println("  E total spikes: " + total_e_spikes.to_string())
  println("  I total spikes: " + total_i_spikes.to_string())
}