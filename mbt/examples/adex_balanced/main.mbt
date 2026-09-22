// adex_balanced.mbt — port of AdEx_balanced_stimuli.jl (simplified).
//
// Original Julia:
//   neuron_parameter = (
//     param = SNN.AdExParameter(R=0.5GΩ, Vt=-50mV, El=-70mV, τm=20ms, Vr=-55mV),
//     synapse = SNN.CurrentSynapse(2ms, 0.2f0, -75mV, 0mV),
//     spike = SNN.PostSpike(τabs=5ms),
//   )
//   E = SNN.Population(; neuron_parameter..., N = 1)
//   poisson_exc = SNN.PoissonLayer(rate=1Hz, N=1000)
//   poisson_inh = SNN.PoissonLayer(rate=10Hz, N=1000)
//   stim_exc = SNN.Stimulus(poisson_exc, E, :glu; conn=(p=1, μ=5nS))
//   stim_inh = SNN.Stimulus(poisson_inh, E, :gaba; conn=(p=1, μ=5nS))
//   sim!(model, 1s)
//
// MoonBit (v0.7.3): single AdEx neuron driven by two current
// stimuli (exc + inh). We don't yet have PoissonStimulusAdEx for
// :glu/:gaba routing, so we approximate via direct current
// injection: exc pushes current IN (positive i_base), inh pulls
// current OUT (negative i_base), both with Gaussian noise to
// mimic the random Poisson spike train.

///|
fn main {
  let rng = @snn_mbt.Xoshiro::default()
  let param = @snn_mbt.AdExParameter::with_vr(-55.0F)
  let pop = @snn_mbt.AdEx::new(1, param, rng)

  // Excitatory Poisson: ~1 Hz × 1000 neurons = 1000 events/s.
  // With μ=5 nS, ~5000 nS/s conductance. Approximate as a constant
  // current of +800 pA with σ=200 pA noise.
  let stim_exc = @snn_mbt.CurrentStimulusArray::new(pop.i, pop.n, 800.0F, rng,
    noise_sigma=200.0F)
  // Inhibitory Poisson: ~10 Hz × 1000 neurons = 10000 events/s.
  // With μ=5 nS, ~50000 nS/s — much stronger. Approximate as
  // -500 pA constant (pulls current out) with σ=200 pA noise.
  let stim_inh = @snn_mbt.CurrentStimulusArray::new(pop.i, pop.n, -500.0F, rng,
    noise_sigma=200.0F)

  println("adex_balanced.mbt: simulation starting (1s)")
  println("  AdEx with Vr=-55mV (current AdEx defaults)")
  println("  exc current: +800 ± 200 pA (Poisson approx)")
  println("  inh current: -50 ± 250 pA (Poisson approx, near-balanced)")

  let dt = 0.125F
  let total_steps : Int = (1000.0F / dt).to_int()
  let mut spikes = 0
  // Track v for final stats.
  let mut v_min = 1000.0F
  let mut v_max = -1000.0F
  for _ in 0..<total_steps {
    // Apply both stimuli (the inh one overwrites; sum semantics
    // here means we apply both, with inh as the last write dominating).
    // Actually we want sum: total i = exc + inh. Apply exc first,
    // then add inh on top.
    @snn_mbt.stimulate_current_array(stim_exc)
    // Now apply inh by ADDING to current i (not overwriting).
    // Use box_muller to sample and add.
    let (z1, _) = @snn_mbt.box_muller(rng)
    pop.i[0] = pop.i[0] + (-50.0F) + 250.0F * Float::from_double(z1)
    let prev_fire = pop.fire[0]
    @snn_mbt.adex_step_synapses(pop, dt)
    @snn_mbt.adex_synaptic_current(pop)
    @snn_mbt.step_adex(pop, dt)
    if pop.fire[0] && !prev_fire {
      spikes = spikes + 1
    }
    if pop.v[0] < v_min { v_min = pop.v[0] }
    if pop.v[0] > v_max { v_max = pop.v[0] }
  }
  println("adex_balanced.mbt: simulation done (1s)")
  println("  total spikes: " + spikes.to_string())
  println("  v[0] range: [" + v_min.to_string() + ", " + v_max.to_string() + "]")
  let hz = Float::from_int(spikes) * 1000.0F / 1000.0F
  println("  effective firing rate: " + hz.to_string() + " Hz")
}