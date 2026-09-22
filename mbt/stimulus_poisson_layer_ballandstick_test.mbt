// Tests for PoissonLayerStimulusBallAndStick — Poisson Stimulus
// targeting BallAndStick compartments (:soma or :d).

///|
test "PoissonLayerStimulusBallAndStick::new: shapes and target fields" {
  let rng = Xoshiro::default()
  let soma_param = AdExParameter::new()
  let post = BallAndStick::new(3, soma_param, rng)
  let pl = PoissonLayer::with_n(5.0F * hz, 4)
  let s = PoissonLayerStimulusBallAndStick::new(
    pl, post, "d", "glu", 1.0F, 0.0F, 1.0F, rng,
  )
  if s.weights.length() != 3 * 4 {
    fail("weights.length = \{s.weights.length()}, expected 12")
  }
  if s.target_compartment != "d" {
    fail("target_compartment = \{s.target_compartment}, expected d")
  }
  if s.target_kind != "glu" {
    fail("target_kind = \{s.target_kind}, expected glu")
  }
}

///|
test "PoissonLayerStimulusBallAndStick: p=1.0 → Fixed weights = μ" {
  let rng = Xoshiro::default()
  let soma_param = AdExParameter::new()
  let post = BallAndStick::new(2, soma_param, rng)
  let pl = PoissonLayer::with_n(100.0F * hz, 2)
  let s = PoissonLayerStimulusBallAndStick::new(
    pl, post, "d", "glu", 3.0F, 0.0F, 1.0F, rng,
  )
  for i in 0..<4 {
    if !s.connectivity[i] {
      fail("connection [\{i}] should exist (p=1)")
    }
    if (s.weights[i] - 3.0F).abs() > 1.0e-6F {
      fail("weight [\{i}] = \{s.weights[i]}, expected 3.0")
    }
  }
}

///|
test "stimulate_layer_ball: high rate → glu_d accumulates" {
  let rng = Xoshiro::default()
  let soma_param = AdExParameter::new()
  let post = BallAndStick::new(1, soma_param, rng)
  let pl = PoissonLayer::with_n(10000.0F * hz, 1)
  let s = PoissonLayerStimulusBallAndStick::new(
    pl, post, "d", "glu", 2.0F, 0.0F, 1.0F, rng,
  )
  let mut total_added = 0.0F
  for _ in 0..<1000 {
    let before = post.glu_d[0]
    stimulate_layer_ball(s, 0.0F, 0.125F)
    let diff = post.glu_d[0] - before
    if diff > 0.0F {
      if (diff - 2.0F).abs() > 1.0e-5F {
        fail("per-fire delta = \{diff}, expected 2.0")
      }
      total_added = total_added + diff
    }
  }
  // 10 kHz × 0.125 ms = λ=1.25, P(k≥1)≈0.713 → ~1426 expected.
  if total_added < 1000.0F {
    fail("total_added = \{total_added}, expected ~1426")
  }
  if total_added > 2000.0F {
    fail("total_added = \{total_added}, suspiciously high")
  }
  // glu_s should NOT receive input (target was :d).
  if post.glu_s[0] != 0.0F {
    fail("glu_s[0] = \{post.glu_s[0]}, expected 0 (target was :d)")
  }
}

///|
test "stimulate_layer_ball: soma target → glu_s only" {
  let rng = Xoshiro::default()
  let soma_param = AdExParameter::new()
  let post = BallAndStick::new(1, soma_param, rng)
  let pl = PoissonLayer::with_n(10000.0F * hz, 1)
  let s = PoissonLayerStimulusBallAndStick::new(
    pl, post, "soma", "glu", 1.0F, 0.0F, 1.0F, rng,
  )
  for _ in 0..<200 {
    stimulate_layer_ball(s, 0.0F, 0.125F)
  }
  if post.glu_s[0] == 0.0F {
    fail("glu_s[0] should receive soma-targeted input")
  }
  if post.glu_d[0] != 0.0F {
    fail("glu_d[0] = \{post.glu_d[0]}, should stay 0 (target was :soma)")
  }
}

///|
test "stimulate_layer_ball: :gaba target → gaba_d" {
  let rng = Xoshiro::default()
  let soma_param = AdExParameter::new()
  let post = BallAndStick::new(1, soma_param, rng)
  let pl = PoissonLayer::with_n(10000.0F * hz, 1)
  let s = PoissonLayerStimulusBallAndStick::new(
    pl, post, "d", "gaba", 1.5F, 0.0F, 1.0F, rng,
  )
  for _ in 0..<200 {
    stimulate_layer_ball(s, 0.0F, 0.125F)
  }
  if post.gaba_d[0] == 0.0F {
    fail("gaba_d[0] should receive gaba-targeted input")
  }
  if post.glu_d[0] != 0.0F {
    fail("glu_d[0] = \{post.glu_d[0]}, should stay 0 (target was :gaba)")
  }
}

///|
test "stimulate_layer_ball: zero rate → no conductance change" {
  let rng = Xoshiro::default()
  let soma_param = AdExParameter::new()
  let post = BallAndStick::new(1, soma_param, rng)
  let pl = PoissonLayer::new(0.0F)
  let s = PoissonLayerStimulusBallAndStick::new(
    pl, post, "d", "glu", 1.0F, 0.0F, 1.0F, rng,
  )
  for _ in 0..<100 {
    stimulate_layer_ball(s, 0.0F, 0.125F)
  }
  if post.glu_d[0] != 0.0F {
    fail("glu_d[0] = \{post.glu_d[0]}, expected 0 (zero rate)")
  }
}