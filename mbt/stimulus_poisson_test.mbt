// Tests for the Poisson stimulus and Knuth's algorithm.

///|
test "sample_poisson(0) = 0" {
  let rng = Xoshiro::default()
  for _ in 0..<10 {
    if sample_poisson(rng, 0.0F) != 0 {
      fail("sample_poisson(0) should be 0")
    }
  }
}

///|
test "sample_poisson(negative) = 0" {
  let rng = Xoshiro::default()
  if sample_poisson(rng, -1.0F) != 0 {
    fail("sample_poisson(negative) should be 0")
  }
}

///|
test "sample_poisson mean is approximately lambda" {
  // Sample many times and check the mean is close to λ.
  // For λ=0.5, E[k] = 0.5 over 1000 samples → expect ~500.
  let rng = Xoshiro::from_state(0xDEADBEEFUL, 0xCAFEBABEUL, 0x1234567890ABCDEFUL, 0xFEDCBA0987654321UL)
  let n = 10000
  let mut sum = 0
  for _ in 0..<n {
    sum = sum + sample_poisson(rng, 0.5F)
  }
  let mean = Float::from_int(sum) / Float::from_int(n)
  // Tolerance: 3 standard deviations. Var(Poisson) = λ, so std = sqrt(0.5/n) ≈ 0.007
  let tol = 0.05F
  if (mean - 0.5F).abs() > tol {
    fail("mean = \{mean}, expected ~0.5 (tol=\{tol})")
  }
}

///|
test "ornstein_uhlenbeck_step: stationary mean ~ μ" {
  // OU process X_t with X_0=μ should remain close to μ for θ>0,
  // σ small, many steps.
  let rng = Xoshiro::from_state(0x12345678UL, 0x9ABCDEF0UL, 0xFEDCBA98UL, 0x76543210UL)
  let mu = 5.0F
  let theta = 0.5F
  let sigma = 0.1F
  let dt = 1.0F
  let n = 10000
  let mut x = mu
  let mut sum = 0.0F
  for _ in 0..<n {
    x = ornstein_uhlenbeck_step(rng, x, theta, mu, sigma, dt)
    sum = sum + x
  }
  let mean = sum / Float::from_int(n)
  // Mean should be close to μ (stationary distribution mean).
  if (mean - mu).abs() > 0.5F {
    fail("OU mean should be ~μ=" + mu.to_string() + ", got " + mean.to_string())
  }
}

///|
test "ornstein_uhlenbeck_step: clamps negative to 0" {
  // With large θ and μ=0, large σ*ξ*sqrt(dt) can drive X negative.
  // The function should clamp to 0.
  let rng = Xoshiro::from_state(0x11111111UL, 0x22222222UL, 0x33333333UL, 0x44444444UL)
  let x = 0.5F
  let result = ornstein_uhlenbeck_step(rng, x, 1.0F, 0.0F, 10.0F, 1.0F)
  if result < 0.0F {
    fail("OU result must be >= 0, got " + result.to_string())
  }
}
