// analysis_protocols_test.mbt — tests for analysis_protocols.mbt.

///|
test "get_poisson_spikes — rate=0 always returns 0" {
  let rng = Xoshiro::new(42UL)
  for _ in 0..<100 {
    let s = get_poisson_spikes(0.0F, 0.1F, rng)
    if s != 0.0F { fail("rate=0 should always be 0, got \{s}") }
  }
}

///|
test "get_poisson_spikes — rate*dt=1 always returns 1" {
  let rng = Xoshiro::new(42UL)
  for _ in 0..<100 {
    let s = get_poisson_spikes(10.0F, 0.1F, rng)
    // lambda = 1.0 → always 1 (since rand() < 1.0 is always true for rand ∈ [0,1))
    if s != 1.0F { fail("rate*dt=1 should always be 1, got \{s}") }
  }
}

///|
test "get_poisson_spikes — seeded reproducibility" {
  let rng1 = Xoshiro::new(42UL)
  let rng2 = Xoshiro::new(42UL)
  let mut same = true
  for _ in 0..<100 {
    let a = get_poisson_spikes(5.0F, 0.1F, rng1)
    let b = get_poisson_spikes(5.0F, 0.1F, rng2)
    if a != b { same = false }
  }
  if !same { fail("same seed should produce same sequence") }
}

///|
test "get_poisson_spikes — frequency approximation (mean ≈ rate*dt)" {
  // 10 kHz × 0.1 ms = 1.0 → all spikes. Use a lower lambda.
  // 2 kHz × 0.1 ms = 0.2 → 20% spikes
  let rng = Xoshiro::new(42UL)
  let mut total : Float = 0.0F
  let n = 10000
  for _ in 0..<n {
    total = total + get_poisson_spikes(2.0F, 0.1F, rng)
  }
  let frac = total / Float::from_int(n)
  // Expected ≈ 0.2; allow ±0.02.
  if (frac - 0.2F).abs() > 0.02F { fail("expected frac ≈ 0.2, got \{frac}") }
}

///|
test "logrange — 5 points from 1 to 100 (decade endpoints)" {
  let r = logrange(1.0F, 100.0F, 5)
  if r.length() != 5 { fail("expected 5 points, got \{r.length()}") }
  // Endpoints exact.
  if (r[0] - 1.0F).abs() > 1.0e-4 { fail("r[0] expected 1.0, got \{r[0]}") }
  if (r[4] - 100.0F).abs() > 1.0e-2 { fail("r[4] expected 100.0, got \{r[4]}") }
  // Geometric mean check — middle element should be sqrt(1*100) = 10.
  if (r[2] - 10.0F).abs() > 1.0e-2 { fail("r[2] expected 10.0, got \{r[2]}") }
}

///|
test "logrange — 1 point entry returns [x1]" {
  let r = logrange(3.0F, 30.0F, 1)
  if r.length() != 1 { fail("expected 1 point") }
  if r[0] != 3.0F { fail("expected 3.0, got \{r[0]}") }
}

///|
test "logrange — n=0 returns empty array" {
  let r = logrange(1.0F, 10.0F, 0)
  if r.length() != 0 { fail("expected empty array") }
}

///|
test "logrange — log spacing is geometric" {
  // 4 points from 1 to 1000: 1, 10, 100, 1000
  let r = logrange(1.0F, 1000.0F, 4)
  if (r[0] - 1.0F).abs() > 1.0e-4 { fail("r[0] expected 1.0, got \{r[0]}") }
  if (r[1] - 10.0F).abs() > 1.0e-2 { fail("r[1] expected 10.0, got \{r[1]}") }
  if (r[2] - 100.0F).abs() > 1.0e-1 { fail("r[2] expected 100.0, got \{r[2]}") }
  if (r[3] - 1000.0F).abs() > 1.0 { fail("r[3] expected 1000.0, got \{r[3]}") }
}