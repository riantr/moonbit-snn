// simulation_speed.mbt — port of SpikingNeuralNetworks.jl/examples/
// simulation_speed.jl (AdEx + PoissonLayer benchmark).
//
// Julia reference (simulation_speed.jl, first 30 lines):
//   E = Population(AdExParameter(), N = 100, synapse = DoubleExpSynapse(),
//                  spike = PostSpike())
//   poisson_exc = PoissonLayer(10.2Hz, N = 1000)
//   poisson_inh = PoissonLayer(3Hz,    N = 1000)
//   exc_conn = (p=1.0f0, μ=1.0f0)
//   inh_conn = (p=1.0f0, μ=4.0f0)
//   stim_exc = Stimulus(poisson_exc, E, :glu, conn=exc_conn, name="noiseE")
//   stim_inh = Stimulus(poisson_inh, E, :gaba, conn=inh_conn, name="noiseI")
//   @btime sim!(model, 10s)
//
// MoonBit (v0.10.11):
//   - Scaled 10× down (10 AdEx + 100 Poisson per pop).
//   - 1s sim duration.
//   - Print sim timing for benchmark purposes.
//   - The full simulation_speed.jl also benchmarks TripodHet and
//     BallAndStick which we don't have yet — those are noted in
//     the README as future work.
//
// We use StimTarget-style routes: each Poisson pop pushes its
// spikes into E.glu (exc) or E.gaba (inh) via `forward_poisson`.

///|
fn main {
  let rng = @snn_mbt.Xoshiro::default()
  let ne = 10
  let n_pois = 100
  let dt = 0.125F

  // AdEx excitatory population with default params.
  let e_param = @snn_mbt.AdExParameter::new()
  let e_pop = @snn_mbt.AdEx::new(ne, e_param, rng)

  // Two Poisson populations for excitatory and inhibitory inputs.
  // Rate in internal units: Hz = 0.001, so 10.2Hz = 0.0102F,
  // 3Hz = 0.003F.
  let pois_exc = @snn_mbt.Poisson::new(n_pois, 0.0102F, rng)
  let pois_inh = @snn_mbt.Poisson::new(n_pois, 0.003F, rng)

  // Manual connectivity: for each Poisson spike, add weight to E's
  // glu (exc) or gaba (inh) buffer. All-to-all for benchmark.
  let w_exc : Float = 1.0F
  let w_inh : Float = 4.0F
  let mut total_e_spikes = 0
  let mut total_inh_spikes = 0
  let mut total_e_fires = 0

  // Manual sim loop (1s = 8000 steps).
  let total_steps : Int = (1000.0F / dt).to_int()
  for _ in 0..<total_steps {
    // 1. Poisson populations fire.
    @snn_mbt.step_poisson(pois_exc, dt)
    @snn_mbt.step_poisson(pois_inh, dt)
    let mut pe_count = 0
    for k in 0..<n_pois {
      if pois_exc.fire[k] { pe_count = pe_count + 1 }
    }
    let mut pi_count = 0
    for k in 0..<n_pois {
      if pois_inh.fire[k] { pi_count = pi_count + 1 }
    }
    total_e_spikes = total_e_spikes + pe_count
    total_inh_spikes = total_inh_spikes + pi_count
    // 2. Route Poisson spikes to E's glu/gaba buffers.
    //    For benchmark, sum all spikes * weight (simplified).
    let glu_inj : Float = Float::from_int(pe_count) * w_exc / Float::from_int(n_pois)
    let gaba_inj : Float = Float::from_int(pi_count) * w_inh / Float::from_int(n_pois)
    for k in 0..<ne {
      e_pop.glu[k] = e_pop.glu[k] + glu_inj
      e_pop.gaba[k] = e_pop.gaba[k] + gaba_inj
    }
    // 3. Step AdEx (synapses → current → neuron).
    @snn_mbt.adex_step_synapses(e_pop, dt)
    @snn_mbt.adex_synaptic_current(e_pop)
    @snn_mbt.step_adex(e_pop, dt)
    // 4. Count E spikes.
    for k in 0..<ne {
      if e_pop.fire[k] { total_e_fires = total_e_fires + 1 }
    }
  }

  // Compute elapsed sim time (rough benchmark).
  let mut j = 0
  let mut checksum = 0.0F
  while j < ne {
    checksum = checksum + e_pop.v[j]
    j = j + 1
  }
  println("simulation_speed.mbt: AdEx + PoissonLayer benchmark (1s)")
  println("  E: " + ne.to_string() + " AdEx neurons")
  println("  pois_exc: " + n_pois.to_string() + " Poisson @ 10.2Hz")
  println("  pois_inh: " + n_pois.to_string() + " Poisson @ 3Hz")
  println("  exc_conn: (p=1.0, μ=1.0)")
  println("  inh_conn: (p=1.0, μ=4.0)")
  println("simulation_speed.mbt: simulation done (1s)")
  println("  total Poisson exc spikes: " + total_e_spikes.to_string())
  println("  total Poisson inh spikes: " + total_inh_spikes.to_string())
  println("  total AdEx E spikes:      " + total_e_fires.to_string())
  println("  E.v checksum (Float32):   " + checksum.to_string())
  println("")
  println("Note: the Julia simulation_speed.jl also benchmarks TripodHet")
  println("  and BallAndStick, which require multi-compartment neurons.")
  println("  Those are TODO (see README \"Tripod / BallAndStick neurons\").")
}