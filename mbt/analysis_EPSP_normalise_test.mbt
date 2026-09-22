// analysis_EPSP_normalise_test.mbt — tests for epsp_normalise.

///|
test "epsp_normalise — constant trace returns all zeros (zero std)" {
  // All samples = -65; std = 0; out = zeros.
  let v : Array[Float] = [-65.0F, -65.0F, -65.0F, -65.0F, -65.0F]
  let z = epsp_normalise(v, 1, 0, 0, 4)
  if z.length() != 5 { fail("expected 5 entries, got \{z.length()}") }
  let mut k = 0
  while k < z.length() {
    if z[k] != 0.0F { fail("z[\{k}] expected 0, got \{z[k]}") }
    k = k + 1
  }
}

///|
test "epsp_normalise — z-scores have mean ≈ 0 and unit variance" {
  // 5 samples [1, 2, 3, 4, 5]. mean = 3, std = sqrt(2) ≈ 1.414.
  // z-scores: [-1.414, -0.707, 0, 0.707, 1.414].
  let v : Array[Float] = [1.0F, 2.0F, 3.0F, 4.0F, 5.0F]
  let z = epsp_normalise(v, 1, 0, 0, 4)
  if z.length() != 5 { fail("expected 5 entries") }
  // Check sum ≈ 0 (mean).
  let mut sum : Float = 0.0F
  let mut k = 0
  while k < z.length() {
    sum = sum + z[k]
    k = k + 1
  }
  if (sum).abs() > 1.0e-4 { fail("sum of z-scores should be ≈0, got \{sum}") }
  // Check sample 2 (z[2]) = 0 (corresponds to v=3 = mean).
  if z[2].abs() > 1.0e-4 { fail("z[2] expected 0, got \{z[2]}") }
  // Check z[0] = -sqrt(2) ≈ -1.414.
  let expected = -1.41421356F
  if (z[0] - expected).abs() > 1.0e-3 {
    fail("z[0] expected ≈-1.414, got \{z[0]}")
  }
}

///|
test "epsp_normalise — multi-compartment selects the right column" {
  // 4 steps × 2 compartments. Soma: constant -65. Dend: [60, 70, 80, 90].
  let v : Array[Float] = [
    -65.0F, 60.0F,
    -65.0F, 70.0F,
    -65.0F, 80.0F,
    -65.0F, 90.0F,
  ]
  // Soma z-scores: all 0 (zero std).
  let z_soma = epsp_normalise(v, 2, 0, 0, 3)
  let mut k = 0
  while k < z_soma.length() {
    if z_soma[k] != 0.0F { fail("soma z[\{k}] expected 0") }
    k = k + 1
  }
  // Dend: mean = 75, std = sqrt(125) ≈ 11.18.
  let z_dend = epsp_normalise(v, 2, 1, 0, 3)
  // z[0] = (60 - 75) / 11.18 ≈ -1.342.
  if (z_dend[0] - (-1.342F)).abs() > 0.05 {
    fail("dend z[0] expected ≈-1.342, got \{z_dend[0]}")
  }
  // z[3] = (90 - 75) / 11.18 ≈ +1.342.
  if (z_dend[3] - 1.342F).abs() > 0.05 {
    fail("dend z[3] expected ≈+1.342, got \{z_dend[3]}")
  }
}

///|
test "epsp_normalise — invalid window returns empty" {
  let v : Array[Float] = [-65.0F, -63.0F, -60.0F]
  let z = epsp_normalise(v, 1, 0, 5, 2)  // t_start > t_end
  if z.length() != 0 { fail("invalid window should return empty") }
  let z2 = epsp_normalise(v, 1, 0, -1, 2)  // negative start
  if z2.length() != 0 { fail("negative start should return empty") }
  let z3 = epsp_normalise(v, 1, 0, 0, 10)  // t_end out of range
  if z3.length() != 0 { fail("out-of-range end should return empty") }
}

///|
test "epsp_normalise — output length matches window size" {
  // 6 steps. window [1, 4] → 4 samples.
  let v : Array[Float] = [1.0F, 2.0F, 4.0F, 8.0F, 16.0F, 32.0F]
  let z = epsp_normalise(v, 1, 0, 1, 4)
  if z.length() != 4 { fail("expected 4 entries, got \{z.length()}") }
}