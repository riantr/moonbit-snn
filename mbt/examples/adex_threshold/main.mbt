// adex_threshold.mbt — port of AdEx_threshold.jl (simplified).
//
// Original Julia:
//   adex_neuron = (
//     param = SNN.AdExParameter(Vr = -50mV),
//     spike = SNN.PostSpike(At = 10mV, τA = 10ms),
//     synapse = SNN.SingleExpSynapse(),
//   )
//   E = SNN.Population(; N = 1, adex_neuron...)
//   SNN.monitor!(E, [:v, :fire, :w, :θ], sr = 1kHz)
//   stim_exc = SNN.Stimulus(poisson_exc, E, :ge; ...)
//   sim!(; model, duration = 20s)
//
// MoonBit (v0.7.3): single AdEx with At=10, τA=10. Receives
// Poisson-like noise via CurrentStimulusArray. We track v[0] across
// 200ms and report spike count. 8x faster than the original 20s
// to keep iteration snappy.

///|
fn main {
  let rng = @snn_mbt.Xoshiro::default()
  let param = @snn_mbt.AdExParameter::with_vr(-50.0F)
  let spike = @snn_mbt.AdExPostSpike::with_at(10.0F, 10.0F)
  let pop = @snn_mbt.AdEx::new_with_spike(1, param, spike, rng)

  // Background Poisson-like drive (current noise). The Julia example
  // uses 15 Hz Poisson × 100 presynaptic neurons → ~1500 events/s.
  // We approximate with N(μ, σ) current noise. Baseline 800 pA puts
  // v near threshold; ±400 pA noise pushes it over occasionally.
  let stim = @snn_mbt.CurrentStimulusArray::new(pop.i, pop.n, 800.0F, rng,
    noise_sigma=400.0F)

  println("adex_threshold.mbt: simulation starting (200ms)")
  println("  AdEx with Vr=-50mV, At=10mV, τA=10ms")
  println("  drive: Poisson-like current noise (baseline=800 pA, σ=400 pA)")

  let dt = 0.125F
  let total_steps : Int = (200.0F / dt).to_int()
  let mut spikes = 0
  for _ in 0..<total_steps {
    @snn_mbt.stimulate_current_array(stim)
    let prev_fire = pop.fire[0]
    @snn_mbt.adex_step_synapses(pop, dt)
    @snn_mbt.adex_synaptic_current(pop)
    @snn_mbt.step_adex(pop, dt)
    if pop.fire[0] && !prev_fire {
      spikes = spikes + 1
    }
  }
  println("adex_threshold.mbt: simulation done (200ms)")
  println("  total spikes: " + spikes.to_string())
  println("  v[0] = " + pop.v[0].to_string())
  println("  w[0] = " + pop.w[0].to_string())
  println("  threshold[0] = " + pop.threshold[0].to_string())
  let hz = Float::from_int(spikes) * 1000.0F / 200.0F
  println("  effective firing rate: " + hz.to_string() + " Hz")
}