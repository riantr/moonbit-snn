// ts_acf_test.mbt — Tests for autocorrelation / partial ACF.

///|
test "ts_acf: lag 0 is always 1" {
  let x : Array[Float] = [1.0F, 3.0F, 2.0F, 5.0F, 7.0F, 4.0F, 6.0F]
  let a = ts_acf(x, 3)
  if (a[0] - 1.0F).abs() > 0.0001F {
    fail("a[0] = \{a[0]}, expected 1.0")
  }
}

///|
test "ts_acf: result is bounded in [-1, 1]" {
  let x : Array[Float] = [1.0F, 3.0F, 2.0F, 5.0F, 7.0F, 4.0F, 6.0F, 8.0F]
  let a = ts_acf(x, 4)
  for k in 0..<a.length() {
    if a[k] < -1.0F - 0.0001F || a[k] > 1.0F + 0.0001F {
      fail("a[\{k}] = \{a[k]}, out of [-1, 1]")
    }
  }
}

///|
test "ts_acf: constant series has ACF = 1 everywhere" {
  let x : Array[Float] = [5.0F, 5.0F, 5.0F, 5.0F, 5.0F, 5.0F, 5.0F]
  let a = ts_acf(x, 3)
  for k in 0..<a.length() {
    if (a[k] - 1.0F).abs() > 0.0001F {
      fail("a[\{k}] = \{a[k]}, expected 1.0")
    }
  }
}

///|
test "ts_acf: deterministic small-series correctness check" {
  // x = [1, 2, 3, 4, 5] — perfectly linear trend. μ = 3.
  // Lag-1 numerator: (1-3)(2-3) + (2-3)(3-3) + (3-3)(4-3) + (4-3)(5-3)
  //                = (-2)(-1) + (-1)(0) + (0)(1) + (1)(2) = 2 + 0 + 0 + 2 = 4
  // Lag-0 denom: (1-3)² + (2-3)² + (3-3)² + (4-3)² + (5-3)² = 4+1+0+1+4 = 10
  // ρ(1) = 4/10 = 0.4
  let x : Array[Float] = [1.0F, 2.0F, 3.0F, 4.0F, 5.0F]
  let a = ts_acf(x, 2)
  if (a[1] - 0.4F).abs() > 0.0001F {
    fail("a[1] = \{a[1]}, expected 0.4")
  }
}

///|
test "ts_pacf: lag 0 is always 1" {
  let x : Array[Float] = [1.0F, 3.0F, 2.0F, 5.0F, 7.0F, 4.0F, 6.0F]
  let p = ts_pacf(x, 3)
  if (p[0] - 1.0F).abs() > 0.0001F {
    fail("p[0] = \{p[0]}, expected 1.0")
  }
}

///|
test "ts_pacf: constant series has PACF = 1 everywhere" {
  let x : Array[Float] = [5.0F, 5.0F, 5.0F, 5.0F, 5.0F, 5.0F, 5.0F]
  let p = ts_pacf(x, 3)
  for k in 0..<p.length() {
    if (p[k] - 1.0F).abs() > 0.0001F {
      fail("p[\{k}] = \{p[k]}, expected 1.0")
    }
  }
}

///|
test "ts_pacf: recovers AR(1) coefficient from a synthetic series" {
  // AR(1): x[t] = phi * x[t-1] + ε, with ε ~ N(0, 1).
  // For long series, PACF[1] should be near phi.
  let phi_true = 0.7F
  let rng = Xoshiro::from_state(7UL, 8UL, 9UL, 10UL)
  let series_data : Array[Float] = Array::make(2000, 0.0F)
  // Burn-in: first 200 samples discarded later.
  for i in 1..<2000 {
    let (z, _) = box_muller(rng)
    let eps = Float::from_double(z)
    series_data[i] = phi_true * series_data[i - 1] + eps
  }
  // Take the last 1500 samples (after burn-in).
  let x : Array[Float] = Array::make(1500, 0.0F)
  for i in 0..<1500 {
    x[i] = series_data[i + 500]
  }
  let p = ts_pacf(x, 4)
  // PACF[1] should be near 0.7 (within ±0.1 for n=1500).
  if (p[1] - phi_true).abs() > 0.1F {
    fail("PACF[1] = \{p[1]}, expected near \{phi_true}")
  }
  // PACF[2..4] should be near 0 (cuts off — AR(1) only).
  for k in 2..<5 {
    if p[k].abs() > 0.1F {
      fail("PACF[\{k}] = \{p[k]}, expected near 0 (AR(1) cuts off at lag 1)")
    }
  }
}

///|
test "ts_pacf: max_lag = 0 returns just [1]" {
  let x : Array[Float] = [1.0F, 2.0F, 3.0F, 4.0F, 5.0F]
  let p = ts_pacf(x, 0)
  assert_eq(p.length(), 1)
  assert_eq(p[0], 1.0F)
}

///|
test "ts_pacf: max_lag = 1 returns [1, ACF[1]]" {
  // For any series, PACF[1] == ACF[1] by definition.
  let x : Array[Float] = [1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F, 7.0F, 8.0F]
  let a = ts_acf(x, 1)
  let p = ts_pacf(x, 1)
  if (p[1] - a[1]).abs() > 0.0001F {
    fail("PACF[1] = \{p[1]}, ACF[1] = \{a[1]}, should be equal")
  }
}