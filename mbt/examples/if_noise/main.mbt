// if_noise.mbt — port of IF_noise_input.jl.
//
// Single IF neuron driven by CurrentNoise: a constant base current
// plus Gaussian noise. Matches the Julia setup:
//   current_param = SNN.CurrentNoise(E.N; I_base = 30pA,
//                                      I_dist = Normal(0pA, 100pA))
//   current_stim = SNN.Stimulus(current_param, E, :I)
//   sim!(; model, duration = 2000ms)
//
// We use the new CurrentStimulusIF with noise_sigma=100, i_base=400.
// (Our default IF has R=0.06 so the membrane equilibrium with I=30
// pA is at v=-68.2 — far below the threshold. We use i_base=400 pA
// to drive the neuron just below threshold, then noise spikes push
// it over. The Julia example uses R=0.5 which scales currents by
// 8x; 400 pA × 0.06 ≈ 24 mV vs 30 pA × 0.5 = 15 mV is in the
// same effective range.)

///|
fn main {
  let rng = @snn_mbt.Xoshiro::default()
  let param = @snn_mbt.IFParameter::new()
  let pop = @snn_mbt.IF::new(1, param, rng)

  // 400 pA base + N(0, 100) pA noise — drives v near threshold.
  let stim = @snn_mbt.CurrentStimulusIF::new(pop, 400.0F, rng, noise_sigma=100.0F)
  // Fire monitor for spike counting.
  let fm = @snn_mbt.Monitor::new_fire(pop, 0)
  let model = @snn_mbt.compose(
    [@snn_mbt.IF_(pop)],
    [],
    stims=[@snn_mbt.CurrentIF_(stim)],
    monitors=[fm],
  )

  println("if_noise.mbt: simulation starting (2s)")
  println("  base current = 400 pA, noise sigma = 100 pA")

  // 2 s = 2000 ms = 16000 steps at dt=0.125F
  @snn_mbt.heterogeneous_sim_for(model, 2000.0F * @snn_mbt.ms)

  let spikes = fm.count_spikes()
  let hz = Float::from_int(spikes) * 1000.0F / 2000.0F
  println("if_noise.mbt: simulation done (2s)")
  println("  total spikes: " + spikes.to_string())
  println("  effective firing rate: " + hz.to_string() + " Hz")
  println("  final v[0] = " + pop.v[0].to_string())
  println("  final I[0] = " + pop.i[0].to_string())
}
