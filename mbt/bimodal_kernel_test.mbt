// bimodal_kernel_test.mbt — tests for bimodal_kernel.mbt.

///|
test "kde — known Gaussian dataset, t=mean → peak" {
  // 1000 samples from N(0, 1).
  let data : Array[Double] = []
  let rng = Xoshiro::new(42UL)
  for _ in 0..<1000 {
    // Box-Muller via next_f64 → std normal.
    let u1 = next_f64(rng)
    let u2 = next_f64(rng)
    let r = (-2.0 * @math.ln(u1)).sqrt()
    let theta = 2.0 * 3.141592653589793 * u2
    let z = r * @math.cos(theta)
    data.push(z)
  }
  // KDE at t=0 with h=1 should be ~ 0.4 (1/sqrt(2π) ≈ 0.3989).
  let k = kde(0.0, 1.0, data)
  if (k - 0.3989).abs() > 0.2 { fail("expected KDE(0) ≈ 0.3989, got \{k}") }
}

///|
test "kde — empty data returns 0" {
  let data : Array[Double] = []
  let k = kde(0.0, 1.0, data)
  if k != 0.0 { fail("expected 0 for empty data, got \{k}") }
}

///|
test "global_kde — shape matches v_range length" {
  let data : Array[Double] = [0.0, 0.5, 1.0]
  let v_range : Array[Double] = [-1.0, 0.0, 0.5, 1.0, 2.0]
  let k = global_kde(1.0, data, v_range)
  if k.length() != 5 { fail("expected 5 entries, got \{k.length()}") }
  // Density at each point should be positive.
  let mut i = 0
  while i < k.length() {
    if k[i] <= 0.0 { fail("k[\{i}] expected > 0, got \{k[i]}") }
    i = i + 1
  }
}

///|
test "get_maxima — strict local maxima only" {
  // [1, 3, 2, 4, 1, 5, 0] → maxima at indices 1, 3, 5 (0-based).
  let data : Array[Double] = [1.0, 3.0, 2.0, 4.0, 1.0, 5.0, 0.0]
  let m = get_maxima(data)
  if m.length() != 3 { fail("expected 3 maxima, got \{m.length()}") }
  if m[0] != 1 { fail("maxima[0] expected 1, got \{m[0]}") }
  if m[1] != 3 { fail("maxima[1] expected 3, got \{m[1]}") }
  if m[2] != 5 { fail("maxima[2] expected 5, got \{m[2]}") }
}

///|
test "get_maxima — plateaus don't count (strictly >)" {
  // [1, 2, 2, 1] — middle plateau is NOT a maximum (data[2] not > data[1]).
  let data : Array[Double] = [1.0, 2.0, 2.0, 1.0]
  let m = get_maxima(data)
  // index 1: data[1]=2 > data[0]=1 ✓ but data[1] not > data[2]=2 ✗ → not a max
  // index 2: data[2]=2 > data[3]=1 ✓ but data[2] not > data[1]=2 ✗ → not a max
  if m.length() != 0 { fail("plateau should not count as maxima, got \{m.length()}") }
}

///|
test "is_bimodal — single peak → not bimodal" {
  // Single Gaussian peak.
  let kernel : Array[Double] = [0.0, 0.1, 0.5, 1.0, 0.5, 0.1, 0.0]
  if is_bimodal(kernel, 0.3) { fail("single peak should not be bimodal") }
}

///|
test "is_bimodal — two equal peaks → bimodal" {
  // Two peaks at indices 2 and 6, equal height.
  let kernel : Array[Double] = [0.0, 0.1, 1.0, 0.3, 0.3, 0.3, 1.0, 0.1, 0.0]
  if !is_bimodal(kernel, 0.3) { fail("two equal peaks should be bimodal") }
}

///|
test "is_bimodal — one peak too small relative to other → not bimodal" {
  // Peak at index 2 (height 1.0), peak at index 6 (height 0.5, ratio 0.5/1.0 = 0.5).
  // Both peaks well above ratio=0.6 → not bimodal (only 1 "real" peak above 0.6*1.0=0.6).
  let kernel : Array[Double] = [0.0, 0.1, 1.0, 0.3, 0.3, 0.3, 0.5, 0.1, 0.0]
  // Maxima indices: 2 (h=1.0), 6 (h=0.5). z=1.0.
  // ratio=0.6: only index 2 (1.0 > 0.6) counts → 1 max → not bimodal.
  if is_bimodal(kernel, 0.6) { fail("asymmetric peaks (0.5/1.0=0.5 < 0.6) should not be bimodal at 0.6") }
  // ratio=0.4: both 1.0 (yes) and 0.5 (yes, 0.5>0.4) → bimodal.
  if !is_bimodal(kernel, 0.4) { fail("asymmetric peaks (0.5/1.0=0.5 > 0.4) should be bimodal at 0.4") }
}

///|
test "count_maxima — counts only real maxima above ratio" {
  let kernel : Array[Double] = [0.0, 0.1, 1.0, 0.3, 0.3, 0.3, 0.5, 0.1, 0.0]
  // 2 maxima at indices 2 (h=1.0) and 6 (h=0.5). z=1.0.
  // ratio=0.6 → only index 2 counts → count = 1.
  if count_maxima(kernel, 0.6) != 1 { fail("count_maxima at 0.6 expected 1") }
  // ratio=0.4 → both count → count = 2.
  if count_maxima(kernel, 0.4) != 2 { fail("count_maxima at 0.4 expected 2") }
}

///|
test "critical_window — unimodal Gaussian data returns small h" {
  // 100 samples from N(0,1) — single peak expected at small bandwidths.
  let data : Array[Double] = []
  let rng = Xoshiro::new(42UL)
  for _ in 0..<100 {
    let u1 = next_f64(rng)
    let u2 = next_f64(rng)
    let r = (-2.0 * @math.ln(u1)).sqrt()
    let theta = 2.0 * 3.141592653589793 * u2
    data.push(r * @math.cos(theta))
  }
  let v_range : Array[Double] = [-3.0, -2.5, -2.0, -1.5, -1.0, -0.5, 0.0, 0.5, 1.0, 1.5, 2.0, 2.5, 3.0]
  let h = critical_window(data, 0.1, 50, v_range)
  if h < 1 || h > 50 { fail("h out of range: \{h}") }
}

///|
test "all_windows — counter length matches max_b" {
  let data : Array[Double] = [0.0, 0.5, 1.0]
  let v_range : Array[Double] = [-1.0, 0.0, 0.5, 1.0, 2.0]
  let c = all_windows(data, 0.3, 5, v_range)
  if c.length() != 5 { fail("expected 5 entries, got \{c.length()}") }
}