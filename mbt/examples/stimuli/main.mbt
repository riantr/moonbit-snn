// stimuli.mbt — simplified port of SpikingNeuralNetworks.jl/test/network/stimuli.jl.
//
// Julia reference:
//   E_BallStick = BallAndStick(300um, N=1, NMDA=EyalNMDA,
//                              param=AdExSoma(Vr=-55mV, Vt=-50mV))
//   E_Tripod    = Tripod(300um, 300um, N=1, NMDA=EyalNMDA,
//                         param=AdExSoma(Vr=-55mV, Vt=-50mV))
//   SE_BallStick = PoissonStimulus(E_BallStick, :he, :d, E_rate, μ=30)
//   SI_BallStick = PoissonStimulus(E_BallStick, :hi, :d, I_rate, μ=15)
//   SE_Tripod    = PoissonStimulus(E_Tripod,    :he, :d1, E_rate, μ=30)
//   SI_Tripod    = PoissonStimulus(E_Tripod,    :hi, :d1, I_rate, μ=15)
//   sim!(model, 10s)
//
// MoonBit (v0.10.29):
//   - 1 BallAndStick + 1 TripodHet neuron.
//   - 4 PoissonLayerStimulus variants: exc + inh on each (Ball :d,
//     Tripod :d1). For simplicity we use the same PoissonLayerStimulus
//     pattern (glu/gaba on :d/:d1) — the full he/hi rising/decay would
//     require the receptor infrastructure (already done in v0.10.25-26).
//   - 500 ms sim with manual loop.
//   - Reports final voltages on both neurons.

///|
fn main {
  let rng = @snn_mbt.Xoshiro::default()
  let dt = 0.125F

  // Build 1 BallAndStick + 1 TripodHet.
  let soma_param = @snn_mbt.AdExParameter::new()
  let ball = @snn_mbt.BallAndStick::new(1, soma_param, rng)
  let base = @snn_mbt.AdExParameter::new()
  let trip_param = @snn_mbt.AdExParameterHet::homogeneous(1, base)
  let tripod : @snn_mbt.TripodHet = @snn_mbt.TripodHet::new(1, trip_param, rng)

  // Exc Poisson (200 Hz) on BallAndStick :d.
  let pl_ball_e = @snn_mbt.PoissonLayer::with_n(200.0F * @snn_mbt.hz, 1)
  let stim_ball_e = @snn_mbt.PoissonLayerStimulusBallAndStick::new(
    pl_ball_e, ball, "d", "glu", 30.0F, 0.0F, 1.0F, rng,
  )

  // Inh Poisson (400 Hz) on BallAndStick :d.
  let pl_ball_i = @snn_mbt.PoissonLayer::with_n(400.0F * @snn_mbt.hz, 1)
  let stim_ball_i = @snn_mbt.PoissonLayerStimulusBallAndStick::new(
    pl_ball_i, ball, "d", "gaba", 15.0F, 0.0F, 1.0F, rng,
  )

  // Exc Poisson (200 Hz) on Tripod :d1.
  let pl_trip_e = @snn_mbt.PoissonLayer::with_n(200.0F * @snn_mbt.hz, 1)
  let stim_trip_e = @snn_mbt.PoissonLayerStimulusTripod::new(
    pl_trip_e, tripod, "d1", "glu", 30.0F, 0.0F, 1.0F, rng,
  )

  // Inh Poisson (400 Hz) on Tripod :d1.
  let pl_trip_i = @snn_mbt.PoissonLayer::with_n(400.0F * @snn_mbt.hz, 1)
  let stim_trip_i = @snn_mbt.PoissonLayerStimulusTripod::new(
    pl_trip_i, tripod, "d1", "gaba", 15.0F, 0.0F, 1.0F, rng,
  )

  println("stimuli.mbt: BallAndStick + Tripod + dendritic Poisson (exc + inh)")
  println("  BallAndStick :d   exc: 1 source @ 200 Hz μ=30; inh: 1 source @ 400 Hz μ=15")
  println("  TripodHet    :d1  exc: 1 source @ 200 Hz μ=30; inh: 1 source @ 400 Hz μ=15")

  // Drive 500 ms.
  let duration_ms = 500.0F
  let steps : Int = (duration_ms / dt).to_int()
  let mut ball_spikes = 0
  let mut trip_spikes = 0
  for _step in 0..<steps {
    let _ = _step
    // Stimulate BallAndStick
    @snn_mbt.stimulate_layer_ball(stim_ball_e, 0.0F, dt)
    @snn_mbt.stimulate_layer_ball(stim_ball_i, 0.0F, dt)
    // Stimulate Tripod
    @snn_mbt.stimulate_layer_tripod(stim_trip_e, 0.0F, dt)
    @snn_mbt.stimulate_layer_tripod(stim_trip_i, 0.0F, dt)
    // Step BallAndStick (step_ballandstick internally handles
    // soma+dend synapse steps + synaptic current).
    @snn_mbt.ballandstick_soma_step_synapses(ball, dt)
    @snn_mbt.ballandstick_dend_step_synapses(ball, dt)
    @snn_mbt.step_ballandstick(ball, dt)
    if ball.fire[0] {
      ball_spikes = ball_spikes + 1
    }
    // Step TripodHet (similarly internal).
    @snn_mbt.tripod_het_soma_step_synapses(tripod, dt)
    @snn_mbt.tripod_het_dend_step_synapses(tripod, dt)
    @snn_mbt.step_tripod_het(tripod, dt)
    if tripod.fire[0] {
      trip_spikes = trip_spikes + 1
    }
  }
  println("")
  println("After " + duration_ms.to_string() + " ms:")
  println("  BallAndStick spikes = " + ball_spikes.to_string())
  println("  TripodHet   spikes = " + trip_spikes.to_string())
  println("")
  println("Final BallAndStick state:")
  println("  v_s = " + ball.v_s[0].to_string() + " mV")
  println("  v_d = " + ball.v_d[0].to_string() + " mV")
  println("")
  println("Final TripodHet state:")
  println("  v_s  = " + tripod.v_s[0].to_string() + " mV")
  println("  v_d1 = " + tripod.v_d1[0].to_string() + " mV")
  println("  v_d2 = " + tripod.v_d2[0].to_string() + " mV")
}