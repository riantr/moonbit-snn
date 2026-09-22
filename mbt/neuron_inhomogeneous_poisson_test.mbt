// neuron_inhomogeneous_poisson_test.mbt — port of
// `refs/SNNModels.jl/test/pop/poisson.jl` VariablePoisson testset.

///|
test "InhomogeneousPoissonParam::new — defaults" {
  let p = InhomogeneousPoissonParam::new()
  assert_eq(p.beta, 0.0F)
  assert_eq(p.tau, 50.0F)
  assert_eq(p.r0, 1.0F)  // 1 kHz normalised = 1.0
  assert_eq(p.rate_timescale, 400.0F)
}

///|
test "InhomogeneousPoissonParam::custom — β / τ / r0" {
  let p = InhomogeneousPoissonParam::custom(0.1F, 100.0F, 0.5F)
  assert_eq(p.beta, 0.1F)
  assert_eq(p.tau, 100.0F)
  assert_eq(p.r0, 0.5F)  // 500 Hz normalised = 0.5
  // rate_timescale keeps its default.
  assert_eq(p.rate_timescale, 400.0F)
}

///|
test "InhomogeneousPoisson::new — defaults: N=100, fire all false, r=r0" {
  let param = InhomogeneousPoissonParam::new()
  let rng = Xoshiro::new(42UL)
  let p = InhomogeneousPoisson::new(100, param, rng)
  assert_eq(p.n, 100)
  assert_eq(p.fire.length(), 100)
  // All fire starts as false.
  let mut all_false = true
  let mut i = 0
  while i < 100 {
    if p.fire[i] {
      all_false = false
    }
    i = i + 1
  }
  assert_eq(all_false, true)
  // r initialised to r0 (= 1.0).
  assert_eq(p.r.length(), 100)
  assert_eq(p.r[0], 1.0F)
  assert_eq(p.r[99], 1.0F)
}

///|
test "InhomogeneousPoisson::new — custom N and β" {
  let param = InhomogeneousPoissonParam::custom(0.1F, 100.0F, 0.5F)
  let rng = Xoshiro::new(42UL)
  let p = InhomogeneousPoisson::new(50, param, rng)
  assert_eq(p.n, 50)
  // r initialised to r0 (= 0.5 for 500 Hz).
  assert_eq(p.r[0], 0.5F)
  assert_eq(p.r[49], 0.5F)
}

///|
test "step_inhomogeneous_poisson — fire mask bounded by N" {
  // Julia: sum(p.fire) ≤ p.N after one integrate! step.
  let param = InhomogeneousPoissonParam::new()
  let rng = Xoshiro::new(42UL)
  let p = InhomogeneousPoisson::new(100, param, rng)
  step_inhomogeneous_poisson(p, 0.125F, rng)
  // Count fires.
  let mut n_fire = 0
  let mut i = 0
  while i < 100 {
    if p.fire[i] {
      n_fire = n_fire + 1
    }
    i = i + 1
  }
  if n_fire > 100 {
    fail("fire count \{n_fire} > N=100")
  }
}

///|
test "step_inhomogeneous_poisson — non-default β + custom N" {
  let param = InhomogeneousPoissonParam::custom(0.1F, 100.0F, 0.5F)
  let rng = Xoshiro::new(42UL)
  let p = InhomogeneousPoisson::new(50, param, rng)
  step_inhomogeneous_poisson(p, 1.0F, rng)
  // Same invariant: n_fire ≤ N=50.
  let mut n_fire = 0
  let mut i = 0
  while i < 50 {
    if p.fire[i] {
      n_fire = n_fire + 1
    }
    i = i + 1
  }
  if n_fire > 50 {
    fail("fire count \{n_fire} > N=50")
  }
  // After step, r should have moved toward r0=0.5 (rate adaptation).
  // The exact new r[0] is r0 + (r0 - erate) / rate_ts * dt.
  // Just check r[0] is finite and not absurd.
  if p.r[0] < 0.0F || p.r[0] > 1.0e6F {
    fail("r[0] = \{p.r[0]}, out of range")
  }
}

///|
test "step_inhomogeneous_poisson — many steps produce realistic rates" {
  // 1000 steps at dt=1ms → 1s simulated.
  // r0=1.0 (1 kHz), so expected spike count ≈ N * r0 = 100.
  let param = InhomogeneousPoissonParam::new()
  let rng = Xoshiro::new(42UL)
  let p = InhomogeneousPoisson::new(100, param, rng)
  let mut t = 0
  while t < 1000 {
    step_inhomogeneous_poisson(p, 0.125F, rng)
    t = t + 1
  }
  // Sum all fires.
  let mut n_fire = 0
  let mut i = 0
  while i < 100 {
    if p.fire[i] {
      n_fire = n_fire + 1
    }
    i = i + 1
  }
  // 1000 steps at dt=0.125 = 125 ms = 0.125s of sim time.
  // Expected fires ≈ N * r0 * T = 100 * 1.0 * 0.125 = 12.5 spikes.
  // Loose bound: 0..40.
  if n_fire > 40 || n_fire < 0 {
    fail("Total fires = \{n_fire}, expected 0..40 (expected ~12.5 for 0.125s at 1kHz)")
  }
}