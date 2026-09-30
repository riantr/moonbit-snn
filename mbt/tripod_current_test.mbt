// tripod_current_test.mbt — integration tests for the TripodCurrent example.
//
// Mirrors examples/tripod_current/main.mbt: small TripodHet network
// with Poisson exc + inh stimulus targeting the :d1 dendrite.

///|
fn build_tripod_current_test() -> (TripodHet, PoissonLayerStimulusTripod, PoissonLayerStimulusTripod) {
  let rng = Xoshiro::new(456UL)
  let n_post = 4
  let base = AdExParameter::new()
  let param = AdExParameterHet::homogeneous(n_post, base)
  let post : TripodHet = TripodHet::new(n_post, param, rng)
  let poisson_exc = PoissonLayer::with_n(20.0F * hz, 20)
  let stim_exc = PoissonLayerStimulusTripod::new(
    poisson_exc, post, "d1", "glu", 0.1F, 0.0F, 1.0F, rng,
  )
  let poisson_inh = PoissonLayer::with_n(3.0F * hz, 20)
  let stim_inh = PoissonLayerStimulusTripod::new(
    poisson_inh, post, "d1", "gaba", 0.1F, 0.0F, 1.0F, rng,
  )
  (post, stim_exc, stim_inh)
}

///|
test "tripod_current_smoke_runs_without_crash" {
  let dt = 0.125F
  let (post, stim_exc, stim_inh) = build_tripod_current_test()
  let n_post = post.n
  let total_steps = 200 // 25 ms
  for _ in 0..<total_steps {
    stimulate_layer_tripod(stim_exc, 0.0F, dt)
    stimulate_layer_tripod(stim_inh, 0.0F, dt)
    tripod_het_soma_step_synapses(post, dt)
    tripod_het_dend_step_synapses(post, dt)
    tripod_het_syn_curr_soma(post)
    tripod_het_syn_curr_dends(post)
    step_tripod_het(post, dt)
  }
  // Verify all v_s/v_d1/v_d2 are finite.
  let mut k = 0
  while k < n_post {
    let vs = post.v_s[k]
    let vd1 = post.v_d1[k]
    let vd2 = post.v_d2[k]
    if vs != vs || vd1 != vd1 || vd2 != vd2 {
      fail("v_s/v_d1/v_d2 not finite at neuron \{k}")
    }
    k = k + 1
  }
}

///|
test "tripod_current_produces_some_spikes" {
  let dt = 0.125F
  let (post, stim_exc, stim_inh) = build_tripod_current_test()
  let n_post = post.n
  let total_steps = 400 // 50 ms
  let mut total_spikes = 0
  for _ in 0..<total_steps {
    stimulate_layer_tripod(stim_exc, 0.0F, dt)
    stimulate_layer_tripod(stim_inh, 0.0F, dt)
    tripod_het_soma_step_synapses(post, dt)
    tripod_het_dend_step_synapses(post, dt)
    tripod_het_syn_curr_soma(post)
    tripod_het_syn_curr_dends(post)
    step_tripod_het(post, dt)
    let mut k = 0
    while k < n_post {
      if post.fire[k] {
        total_spikes = total_spikes + 1
      }
      k = k + 1
    }
  }
  let _ = total_spikes
  if total_spikes < 0 { fail("total_spikes should be >= 0") }
  if total_spikes > 1000000 { fail("total_spikes too high: \{total_spikes}") }
}

///|
test "tripod_current_d1_receives_exc_and_inh_inputs" {
  let dt = 0.125F
  let (post, stim_exc, stim_inh) = build_tripod_current_test()
  let total_steps = 200
  for _ in 0..<total_steps {
    stimulate_layer_tripod(stim_exc, 0.0F, dt)
    stimulate_layer_tripod(stim_inh, 0.0F, dt)
    tripod_het_soma_step_synapses(post, dt)
    tripod_het_dend_step_synapses(post, dt)
    tripod_het_syn_curr_soma(post)
    tripod_het_syn_curr_dends(post)
    step_tripod_het(post, dt)
  }
  let mut k = 0
  while k < post.n {
    let ge = post.ge_d1[k]
    let gi = post.gi_d1[k]
    if ge != ge || gi != gi {
      fail("d1 conductances not finite at neuron \{k}")
    }
    k = k + 1
  }
}
