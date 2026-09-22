// inhomogeneous_poisson_bitexact_test.mbt — regression test for the
// InhomogeneousPoisson population.
//
// Verifies that under the rate-adaptation dynamics, the long-run
// firing rate converges to r0 (the target rate). The first ~τ_rate
// (400ms) of the simulation has transient adaptation; after that,
// r[i] should hover near r0, and the empirical spike rate per
// neuron should be close to r0 (Hz).
//
// This complements the unit tests in
// `neuron_inhomogeneous_poisson_test.mbt` (which check shape
// invariants) by checking dynamic behaviour over a long simulation.

///|
test "InhomogeneousPoisson — long-run rate converges to r0" {
  // 100 neurons at r0 = 1.0 kHz = 1.0 (normalised), τ_rate = 400ms.
  // Run 4 seconds = 32,000 steps at dt = 0.125ms.
  // Note: fire[j] is reset at the start of each step (line 112),
  // so we need to track total fires with an external counter.
  let param = InhomogeneousPoissonParam::new()
  let rng = Xoshiro::new(42UL)
  let p = InhomogeneousPoisson::new(100, param, rng)
  let dt = 0.125F
  let n_steps = 32000  // 4s sim time
  let mut total_fires = 0
  let mut t = 0
  while t < n_steps {
    step_inhomogeneous_poisson(p, dt, rng)
    // After the step, p.fire[i] holds this step's per-neuron fires.
    let mut i = 0
    while i < 100 {
      if p.fire[i] {
        total_fires = total_fires + 1
      }
      i = i + 1
    }
    t = t + 1
  }
  // Empirical rate (Hz) = total_fires / (100 neurons * 4s).
  // r0 = 1.0 in our normalised units corresponds to 1 kHz = 1000 Hz.
  let rate_hz = Float::from_int(total_fires) / 100.0F / 4.0F
  // Expected: ~1000 Hz. Loose bound: 700..1300 (30% tolerance).
  if rate_hz < 700.0F || rate_hz > 1300.0F {
    fail("empirical rate = \{rate_hz} Hz, expected ~1000 Hz (r0)")
  }
}

///|
test "InhomogeneousPoisson — no NaN states after long sim" {
  let param = InhomogeneousPoissonParam::new()
  let rng = Xoshiro::new(7UL)
  let p = InhomogeneousPoisson::new(50, param, rng)
  let dt = 0.125F
  let n_steps = 10000
  let mut t = 0
  while t < n_steps {
    step_inhomogeneous_poisson(p, dt, rng)
    t = t + 1
  }
  // NaN check.
  if p.r[0] != p.r[0] {
    fail("r[0] is NaN after long sim")
  }
  if p.noise[0] != p.noise[0] {
    fail("noise[0] is NaN after long sim")
  }
  // fire[i] should be Boolean (no NaN issue here).
}

///|
test "InhomogeneousPoisson — different seed produces different trajectory" {
  // Two simulations with different RNG seeds should have different
  // fire patterns (sanity check that the RNG is actually being used).
  let param = InhomogeneousPoissonParam::new()
  let rng1 = Xoshiro::new(1UL)
  let rng2 = Xoshiro::new(2UL)
  let p1 = InhomogeneousPoisson::new(20, param, rng1)
  let p2 = InhomogeneousPoisson::new(20, param, rng2)
  let dt = 0.125F
  let n_steps = 100
  let mut t = 0
  while t < n_steps {
    step_inhomogeneous_poisson(p1, dt, rng1)
    step_inhomogeneous_poisson(p2, dt, rng2)
    t = t + 1
  }
  // Sum differences: count fires where p1[i] != p2[i].
  let mut diff_count = 0
  let mut i = 0
  while i < 20 {
    if p1.fire[i] != p2.fire[i] {
      diff_count = diff_count + 1
    }
    i = i + 1
  }
  // We expect at least some differences across 100 steps × 20 neurons.
  // With r0=1.0Hz × 100 steps × 0.125ms = ~12.5% chance per (i, t)
  // → probability of same fire pattern across all 20 at this step is
  // tiny. Just check that at least 1 neuron differs in the final state.
  if diff_count == 0 {
    fail(
      "Different seeds produced identical fire patterns (RNG might not be seeded correctly)",
    )
  }
}

///|
test "InhomogeneousPoisson — β = 0 reduces to homogeneous Poisson" {
  // With β = 0, the noise term doesn't modulate rate (1.0 floor)
  // and the process becomes a homogeneous Poisson with rate r0.
  // Verify the empirical rate is close to r0 (1.0).
  let param = InhomogeneousPoissonParam::custom(0.0F, 50.0F, 1.0F)
  let rng = Xoshiro::new(42UL)
  let p = InhomogeneousPoisson::new(100, param, rng)
  let dt = 0.125F
  let n_steps = 16000    // 2s
  let mut total_fires = 0
  let mut t = 0
  while t < n_steps {
    step_inhomogeneous_poisson(p, dt, rng)
    let mut i = 0
    while i < 100 {
      if p.fire[i] {
        total_fires = total_fires + 1
      }
      i = i + 1
    }
    t = t + 1
  }
  let rate_hz = Float::from_int(total_fires) / 100.0F / 2.0F
  // Expected ~1000 Hz (r0 = 1.0 normalised → 1 kHz).
  if rate_hz < 700.0F || rate_hz > 1300.0F {
    fail("β=0 rate = \{rate_hz} Hz, expected ~1000 Hz (homogeneous Poisson)")
  }
}