// Tests for PoissonLayerStimulusTripod — Poisson Stimulus targeting
// TripodHet compartments.

///|
test "PoissonLayerStimulusTripod::new: shapes and target fields" {
  let rng = Xoshiro::default()
  let base = AdExParameter::new()
  let param = AdExParameterHet::homogeneous(3, base)
  let post = TripodHet::new(3, param, rng)
  let pl = PoissonLayer::with_n(5.0F * hz, 4)  // 5 Hz, 4 sources
  let s = PoissonLayerStimulusTripod::new(
    pl, post, "d1", "glu", 1.0F, 0.0F, 1.0F, rng,
  )
  if s.weights.length() != 3 * 4 {
    fail("weights.length = \{s.weights.length()}, expected 12")
  }
  if s.connectivity.length() != 3 * 4 {
    fail("connectivity.length = \{s.connectivity.length()}, expected 12")
  }
  if s.target_compartment != "d1" {
    fail("target_compartment = \{s.target_compartment}, expected d1")
  }
  if s.target_kind != "glu" {
    fail("target_kind = \{s.target_kind}, expected glu")
  }
}

///|
test "PoissonLayerStimulusTripod: p=1.0 → all weights = μ (Fixed)" {
  let rng = Xoshiro::default()
  let base = AdExParameter::new()
  let param = AdExParameterHet::homogeneous(2, base)
  let post = TripodHet::new(2, param, rng)
  let pl = PoissonLayer::with_n(100.0F * hz, 2)  // 100 Hz, 2 sources
  let s = PoissonLayerStimulusTripod::new(
    pl, post, "d1", "glu", 3.0F, 0.0F, 1.0F, rng,
  )
  for i in 0..<4 {
    if !s.connectivity[i] {
      fail("connection [\{i}] should exist (p=1)")
    }
    if (s.weights[i] - 3.0F).abs() > 1.0e-6F {
      fail("weight [\{i}] = \{s.weights[i]}, expected 3.0 (Fixed)")
    }
  }
}

///|
test "stimulate_layer_tripod: high rate → glu_d1 accumulates" {
  let rng = Xoshiro::default()
  let base = AdExParameter::new()
  let param = AdExParameterHet::homogeneous(1, base)
  let post = TripodHet::new(1, param, rng)
  // 10 kHz, 1 source, Fixed μ=2.0 → lambda = 10 * 0.125 = 1.25 per step
  let pl = PoissonLayer::with_n(10000.0F * hz, 1)
  let s = PoissonLayerStimulusTripod::new(
    pl, post, "d1", "glu", 2.0F, 0.0F, 1.0F, rng,
  )
  // Run 1000 steps — high rate should produce many fires.
  // 10 kHz × 0.125 ms = lambda = 1.25. P(k≥1) = 1 - e^(-1.25) ≈ 0.713.
  // Expected ~713 fires × 2.0 weight = ~1426 over 1000 steps.
  let mut total_added = 0.0F
  for _ in 0..<1000 {
    let before = post.glu_d1[0]
    stimulate_layer_tripod(s, 0.0F, 0.125F)
    let diff = post.glu_d1[0] - before
    // Per fire, weight = 2.0; so diff is either 0 or 2.0
    if diff > 0.0F {
      if (diff - 2.0F).abs() > 1.0e-5F {
        fail("per-fire delta = \{diff}, expected 2.0")
      }
      total_added = total_added + diff
    }
  }
  // Loose bounds: ~1400 ± 30%.
  if total_added < 1000.0F {
    fail("total_added = \{total_added}, expected ~1426")
  }
  if total_added > 2000.0F {
    fail("total_added = \{total_added}, suspiciously high")
  }
  // glu_s / glu_d2 should NOT receive input (target was :d1).
  if post.glu_s[0] != 0.0F {
    fail("glu_s[0] = \{post.glu_s[0]}, expected 0 (target was :d1)")
  }
  if post.glu_d2[0] != 0.0F {
    fail("glu_d2[0] = \{post.glu_d2[0]}, expected 0")
  }
}

///|
test "stimulate_layer_tripod: :gaba target → gaba_d1 buffer" {
  let rng = Xoshiro::default()
  let base = AdExParameter::new()
  let param = AdExParameterHet::homogeneous(1, base)
  let post = TripodHet::new(1, param, rng)
  let pl = PoissonLayer::with_n(10000.0F * hz, 1)
  let s = PoissonLayerStimulusTripod::new(
    pl, post, "d1", "gaba", 1.5F, 0.0F, 1.0F, rng,
  )
  for _ in 0..<200 {
    stimulate_layer_tripod(s, 0.0F, 0.125F)
  }
  if post.gaba_d1[0] == 0.0F {
    fail("gaba_d1[0] = \{post.gaba_d1[0]}, expected non-zero (high rate gaba)")
  }
  if post.glu_d1[0] != 0.0F {
    fail("glu_d1[0] = \{post.glu_d1[0]}, should stay 0 (target was :gaba)")
  }
}

///|
test "stimulate_layer_tripod: soma target → glu_s only" {
  let rng = Xoshiro::default()
  let base = AdExParameter::new()
  let param = AdExParameterHet::homogeneous(1, base)
  let post = TripodHet::new(1, param, rng)
  let pl = PoissonLayer::with_n(10000.0F * hz, 1)
  let s = PoissonLayerStimulusTripod::new(
    pl, post, "soma", "glu", 1.0F, 0.0F, 1.0F, rng,
  )
  for _ in 0..<200 {
    stimulate_layer_tripod(s, 0.0F, 0.125F)
  }
  if post.glu_s[0] == 0.0F {
    fail("glu_s[0] should receive soma-targeted input")
  }
  if post.glu_d1[0] != 0.0F || post.glu_d2[0] != 0.0F {
    fail("only :soma should receive; d1/d2 leaked")
  }
}

///|
test "stimulate_layer_tripod: zero rate → no conductance change" {
  let rng = Xoshiro::default()
  let base = AdExParameter::new()
  let param = AdExParameterHet::homogeneous(1, base)
  let post = TripodHet::new(1, param, rng)
  let pl = PoissonLayer::new(0.0F)
  let s = PoissonLayerStimulusTripod::new(
    pl, post, "d1", "glu", 1.0F, 0.0F, 1.0F, rng,
  )
  for _ in 0..<100 {
    stimulate_layer_tripod(s, 0.0F, 0.125F)
  }
  if post.glu_d1[0] != 0.0F {
    fail("glu_d1[0] = \{post.glu_d1[0]}, expected 0 (zero rate)")
  }
}

///|
test "stimulate_layer_tripod: inactive source → no fire" {
  let rng = Xoshiro::default()
  let base = AdExParameter::new()
  let param = AdExParameterHet::homogeneous(1, base)
  let post = TripodHet::new(1, param, rng)
  let active : Array[Bool] = [false]
  let pl = PoissonLayer::with_active(10000.0F * hz, 1, active)
  let s = PoissonLayerStimulusTripod::new(
    pl, post, "d1", "glu", 1.0F, 0.0F, 1.0F, rng,
  )
  for _ in 0..<200 {
    stimulate_layer_tripod(s, 0.0F, 0.125F)
  }
  if post.glu_d1[0] != 0.0F {
    fail("glu_d1[0] = \{post.glu_d1[0]}, expected 0 (inactive source)")
  }
}