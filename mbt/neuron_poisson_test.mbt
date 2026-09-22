// Tests for the Poisson population neuron.

///|
test "Poisson::new initial state" {
  let rng = Xoshiro::default()
  let p = Poisson::new(5, 50.0F, rng)
  for k in 0..<5 {
    assert_eq(p.fire[k], false)
  }
}

///|
test "Poisson rate matches frequency" {
  // 50 Hz in our internal units = 50 * 0.001 = 0.05.
  // rate * dt = 0.05 * 0.125 = 6.25e-3 per step.
  // Over 1 s (8000 steps), expected fires = 0.00625 * 8000 = 50.
  let rng = Xoshiro::from_state(1UL, 2UL, 3UL, 4UL)
  let p = Poisson::new(1, 50.0F * 0.001F, rng)
  let mut total_fires = 0
  for _ in 0..<8000 {
    step_poisson(p, 0.125F)
    if p.fire[0] {
      total_fires = total_fires + 1
    }
  }
  // 1s * 50Hz = 50 expected. Tolerance: ±20.
  if total_fires < 30 || total_fires > 70 {
    fail("Expected ~50 fires in 1s, got " + total_fires.to_string())
  }
}

///|
test "Poisson high rate always fires" {
  // 10000 Hz in internal units = 10. rate * dt = 10 * 0.125 = 1.25.
  // Almost always fires.
  let rng = Xoshiro::default()
  let p = Poisson::new(1, 10000.0F * 0.001F, rng)
  let mut fires = 0
  for _ in 0..<100 {
    step_poisson(p, 0.125F)
    if p.fire[0] {
      fires = fires + 1
    }
  }
  // Should fire essentially all 100 steps.
  if fires < 90 {
    fail("High-rate Poisson should always fire, got " + fires.to_string() + "/100")
  }
}
