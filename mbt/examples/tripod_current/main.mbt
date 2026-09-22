// tripod_current.mbt — scaled-down port of SpikingNeuralNetworks.jl/test/pop/tripod_current.jl.
//
// Julia reference (full size: 100 Tripod + 1000-source PoissonLayers):
//   E = Tripod(..., NMDA=EyalNMDA, param=AdExSoma)
//   poisson_exc = PoissonLayer(20.2Hz, N=1000)
//   poisson_inh = PoissonLayer(3Hz,    N=1000)
//   stim_exc = Stimulus(poisson_exc, E, :glu, :d1, conn=(μ=0.1, ρ=1))
//   stim_inh = Stimulus(poisson_inh, E, :gaba, :d1, conn=(μ=0.1, ρ=1))
//   sim!(model; duration=2s)
//
// MoonBit (v0.10.27):
//   - 4 TripodHet neurons with default AdEx soma params + dendrites.
//   - 20 Poisson exc sources @ 20 Hz → glu_d1 (μ=0.1, p=1.0).
//   - 20 Poisson inh sources @ 3 Hz  → gaba_d1 (μ=0.1, p=1.0).
//   - Manual sim loop: stimulate (exc + inh) → step_receptors (per
//     synapse) → step_tripod_het (per neuron).
//   - 500 ms sim; reports spike count per neuron + final v_s/v_d1/v_d2.
//
// Bit-exact note: the Julia network uses NMDA voltage-dependent gating
// in ReceptorSynapse. Our scaled-down example uses the simpler
// single-exp synapses on :d1 (glu_d1 → ge_d1, gaba_d1 → gi_d1) via
// TripodHet's existing single-exp step_synapses. NMDA modulation is
// available via the ReceptorSynapseTripod infrastructure but not used
// here for simplicity.

///|
fn main {
  let rng = @snn_mbt.Xoshiro::default()
  let dt = 0.125F

  // Build 4 TripodHet neurons (scaled down from Julia's 100).
  let base = @snn_mbt.AdExParameter::new()
  let n_post = 4
  let param = @snn_mbt.AdExParameterHet::homogeneous(n_post, base)
  let post : @snn_mbt.TripodHet = @snn_mbt.TripodHet::new(n_post, param, rng)

  // 20 exc Poisson sources @ 20 Hz → :glu, :d1.
  let poisson_exc = @snn_mbt.PoissonLayer::with_n(20.0F * @snn_mbt.hz, 20)
  let stim_exc = @snn_mbt.PoissonLayerStimulusTripod::new(
    poisson_exc, post, "d1", "glu", 0.1F, 0.0F, 1.0F, rng,
  )

  // 20 inh Poisson sources @ 3 Hz → :gaba, :d1.
  let poisson_inh = @snn_mbt.PoissonLayer::with_n(3.0F * @snn_mbt.hz, 20)
  let stim_inh = @snn_mbt.PoissonLayerStimulusTripod::new(
    poisson_inh, post, "d1", "gaba", 0.1F, 0.0F, 1.0F, rng,
  )

  println("tripod_current.mbt: 4 TripodHet + Poisson exc + Poisson inh")
  println("  exc: 20 sources @ 20 Hz → :d1, μ=0.1, p=1.0")
  println("  inh: 20 sources @ 3 Hz  → :d1, μ=0.1, p=1.0")
  println("  total connections (exc): " + stim_exc.weights.length().to_string())
  println("  total connections (inh): " + stim_inh.weights.length().to_string())

  // Drive 500 ms (Julia: 2s; scaled to 500ms for example speed).
  let duration_ms = 500.0F
  let steps : Int = (duration_ms / dt).to_int()
  let spike_counts : Array[Int] = Array::make(n_post, 0)
  for _step in 0..<steps {
    let _ = _step
    // 1. stimulate (exc + inh on :d1)
    @snn_mbt.stimulate_layer_tripod(stim_exc, 0.0F, dt)
    @snn_mbt.stimulate_layer_tripod(stim_inh, 0.0F, dt)
    // 2. step synapses on soma + dendrites (single-exp; consumes glu/gaba)
    @snn_mbt.tripod_het_soma_step_synapses(post, dt)
    @snn_mbt.tripod_het_dend_step_synapses(post, dt)
    // 3. synaptic currents
    @snn_mbt.tripod_het_syn_curr_soma(post)
    @snn_mbt.tripod_het_syn_curr_dends(post)
    // 4. step TripodHet
    @snn_mbt.step_tripod_het(post, dt)
    // 5. count spikes
    for k in 0..<n_post {
      if post.fire[k] {
        spike_counts[k] = spike_counts[k] + 1
      }
    }
  }
  println("")
  println("After " + duration_ms.to_string() + " ms:")
  for k in 0..<n_post {
    println("  neuron " + k.to_string() + ": " + spike_counts[k].to_string() + " spikes")
  }
  println("")
  println("Final state:")
  for k in 0..<n_post {
    println(
      "  neuron " + k.to_string() + ": v_s=" + post.v_s[k].to_string() +
      " v_d1=" + post.v_d1[k].to_string() + " v_d2=" + post.v_d2[k].to_string(),
    )
  }
}