// izhikevich_julia_ge_gi_test.mbt — tests for sample_julia_initial_ge_gi.

///|
test "sample_julia_initial_ge_gi — output length matches n" {
  let rng = Xoshiro::new(42UL)
  let (ge, gi) = sample_julia_initial_ge_gi(10, rng)
  if ge.length() != 10 { fail("ge length expected 10") }
  if gi.length() != 10 { fail("gi length expected 10") }
}

///|
test "sample_julia_initial_ge_gi — ge mean ≈ 40 (4*10=40), gi mean ≈ 200 (20*10=200)" {
  // Mean of (1.5*z + 4)*10 = 4*10 = 40; mean of (12*z + 20)*10 = 20*10 = 200.
  // With N(0,1) Box-Muller draws, expect means within ±20 of those.
  let rng = Xoshiro::new(42UL)
  let (ge, gi) = sample_julia_initial_ge_gi(1000, rng)
  let mut sum_ge : Float = 0.0F
  let mut sum_gi : Float = 0.0F
  let mut k = 0
  while k < 1000 {
    sum_ge = sum_ge + ge[k]
    sum_gi = sum_gi + gi[k]
    k = k + 1
  }
  let mean_ge = sum_ge / 1000.0F
  let mean_gi = sum_gi / 1000.0F
  if (mean_ge - 40.0F).abs() > 20.0F {
    fail("mean_ge expected ≈40, got \{mean_ge}")
  }
  if (mean_gi - 200.0F).abs() > 100.0F {
    fail("mean_gi expected ≈200, got \{mean_gi}")
  }
}

///|
test "sample_julia_initial_ge_gi — seeded reproducibility" {
  let rng1 = Xoshiro::new(42UL)
  let (ge1, gi1) = sample_julia_initial_ge_gi(5, rng1)
  let rng2 = Xoshiro::new(42UL)
  let (ge2, gi2) = sample_julia_initial_ge_gi(5, rng2)
  let mut k = 0
  while k < 5 {
    if ge1[k] != ge2[k] { fail("ge[\{k}] mismatch: \{ge1[k]} vs \{ge2[k]}") }
    if gi1[k] != gi2[k] { fail("gi[\{k}] mismatch") }
    k = k + 1
  }
}

///|
test "sample_julia_initial_ge_gi — ge never negative (clamped)" {
  // Even with extreme Box-Muller draws, ge is clamped to ≥ 0
  // (matches Julia's `ge = max(0, (1.5randn + 4) * 10)`).
  let rng = Xoshiro::new(7UL)
  let (ge, _) = sample_julia_initial_ge_gi(100, rng)
  let mut k = 0
  while k < 100 {
    if ge[k] < 0.0F { fail("ge[\{k}] should be ≥ 0, got \{ge[k]}") }
    k = k + 1
  }
}

///|
test "sample_julia_initial_ge_gi — ge/gi distributions differ" {
  // gi should typically have larger values than ge (×10 vs ×0.2 mean
  // scaling factor in Julia source).
  let rng = Xoshiro::new(42UL)
  let (ge, gi) = sample_julia_initial_ge_gi(100, rng)
  let mut sum_ge : Float = 0.0F
  let mut sum_gi : Float = 0.0F
  let mut k = 0
  while k < 100 {
    sum_ge = sum_ge + ge[k]
    sum_gi = sum_gi + gi[k]
    k = k + 1
  }
  // gi mean should be >> ge mean.
  if sum_gi <= sum_ge {
    fail("sum_gi should exceed sum_ge (gi is stronger initial drive)")
  }
}