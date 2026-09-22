// analysis_EPSP_normalise_by_compartment_test.mbt — tests for
// epsp_normalise_by_compartment.

///|
test "epsp_normalise_by_compartment — multi-compartment returns per-column z-scores" {
  // 5 steps × 3 compartments.
  // Compartment 0: flat -65 (zeros after normalisation).
  // Compartment 1: rising then falling [60, 70, 80, 70, 60] (mean=68, std≈7.48).
  // Compartment 2: falling [80, 70, 60, 50, 40] (mean=60, std≈14.14).
  let v : Array[Float] = [
    -65.0F, 60.0F, 80.0F,
    -65.0F, 70.0F, 70.0F,
    -65.0F, 80.0F, 60.0F,
    -65.0F, 70.0F, 50.0F,
    -65.0F, 60.0F, 40.0F,
  ]
  let result = epsp_normalise_by_compartment(v, 3, 0, 4)
  if result.length() != 3 { fail("expected 3 columns, got \{result.length()}") }
  // Compartment 0: all zeros.
  if result[0].length() != 5 { fail("col 0 length expected 5") }
  let mut k = 0
  while k < 5 {
    if result[0][k] != 0.0F { fail("col 0 z[\{k}] expected 0, got \{result[0][k]}") }
    k = k + 1
  }
  // Compartment 1: mean ≈ 68, std ≈ 7.48.
  if (result[1].length()) != 5 { fail("col 1 length expected 5") }
  // Sum should be ≈ 0.
  let mut sum1 : Float = 0.0F
  let mut k1 = 0
  while k1 < 5 {
    sum1 = sum1 + result[1][k1]
    k1 = k1 + 1
  }
  if sum1.abs() > 1.0e-3 { fail("col 1 sum should be ≈0, got \{sum1}") }
}

///|
test "epsp_normalise_by_compartment — invalid window returns empty" {
  let v : Array[Float] = [-65.0F, 60.0F, 80.0F]
  let result = epsp_normalise_by_compartment(v, 1, 5, 2)  // t_start > t_end
  if result.length() != 0 { fail("invalid window should return empty") }
  let result2 = epsp_normalise_by_compartment(v, 1, -1, 2)
  if result2.length() != 0 { fail("negative start should return empty") }
  let result3 = epsp_normalise_by_compartment(v, 1, 0, 10)
  if result3.length() != 0 { fail("out-of-range end should return empty") }
}

///|
test "epsp_normalise_by_compartment — single-compartment case matches epsp_normalise" {
  // With n_compartments=1, the result should be equivalent to
  // calling epsp_normalise on that compartment.
  let v : Array[Float] = [1.0F, 2.0F, 3.0F, 4.0F, 5.0F]
  let result_multi = epsp_normalise_by_compartment(v, 1, 0, 4)
  let result_single = epsp_normalise(v, 1, 0, 0, 4)
  if result_multi.length() != 1 { fail("expected 1 column") }
  if result_single.length() != 5 { fail("single should have 5 entries") }
  let mut k = 0
  while k < 5 {
    if (result_multi[0][k] - result_single[k]).abs() > 1.0e-6 {
      fail("multi[\{k}] vs single mismatch")
    }
    k = k + 1
  }
}

///|
test "epsp_normalise_by_compartment — degenerate compartment returns zeros" {
  // 3 steps × 2 compartments. Compartment 1 is constant (zero std).
  let v : Array[Float] = [
    -65.0F, 0.0F,
    -63.0F, 0.0F,
    -60.0F, 0.0F,
  ]
  let result = epsp_normalise_by_compartment(v, 2, 0, 2)
  // Compartment 0: rising, should have z-scores.
  // Compartment 1: constant zero → z-scores all 0.
  let mut k = 0
  while k < 3 {
    if result[1][k] != 0.0F { fail("constant col 1 z[\{k}] expected 0") }
    k = k + 1
  }
  // Compartment 0: sum should be ≈0.
  let mut sum0 : Float = 0.0F
  let mut k0 = 0
  while k0 < 3 {
    sum0 = sum0 + result[0][k0]
    k0 = k0 + 1
  }
  if sum0.abs() > 1.0e-3 { fail("rising col 0 sum should be ≈0, got \{sum0}") }
}

///|
test "epsp_normalise_by_compartment — different std per compartment" {
  // Compartment 0: small std (range [-1, 1]). Compartment 1: large std.
  let v : Array[Float] = [
    -1.0F, 0.0F,
    0.0F, 100.0F,
    1.0F, -100.0F,
  ]
  let result = epsp_normalise_by_compartment(v, 2, 0, 2)
  // Compartment 0: mean = 0, std = sqrt(2/3) ≈ 0.816. z[0] = -1.225.
  if (result[0][0] - (-1.2247F)).abs() > 0.01 {
    fail("col 0 z[0] expected ≈-1.225, got \{result[0][0]}")
  }
  // Compartment 1: mean = 0, std = sqrt(10000) = 100. z[0] = 0.
  if (result[1][0] - 0.0F).abs() > 0.01 {
    fail("col 1 z[0] expected ≈0, got \{result[1][0]}")
  }
}