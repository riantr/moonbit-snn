// ts_stats_test.mbt — Tests for time-series preprocessing primitives.

///|
test "ts_mean: arithmetic mean of a constant series" {
  let x : Array[Float] = [3.0F, 3.0F, 3.0F, 3.0F, 3.0F]
  if (ts_mean(x) - 3.0F).abs() > 0.0001F {
    fail("ts_mean = \{ts_mean(x)}, expected 3.0")
  }
}

///|
test "ts_mean: 1..5 mean = 3" {
  let x : Array[Float] = [1.0F, 2.0F, 3.0F, 4.0F, 5.0F]
  if (ts_mean(x) - 3.0F).abs() > 0.0001F {
    fail("ts_mean = \{ts_mean(x)}, expected 3.0")
  }
}

///|
test "ts_mean: empty input returns 0" {
  let empty : Array[Float] = []
  if ts_mean(empty) != 0.0F {
    fail("empty ts_mean should be 0.0")
  }
}

///|
test "ts_var: 1..5 variance = 2.0 (population divisor)" {
  // mean = 3; deviations = [-2,-1,0,1,2]; squared = [4,1,0,1,4]; sum = 10
  // population variance = 10 / 5 = 2.0
  let x : Array[Float] = [1.0F, 2.0F, 3.0F, 4.0F, 5.0F]
  if (ts_var(x) - 2.0F).abs() > 0.0001F {
    fail("ts_var = \{ts_var(x)}, expected 2.0")
  }
}

///|
test "ts_var: constant series has zero variance" {
  let x : Array[Float] = [5.0F, 5.0F, 5.0F]
  if ts_var(x) != 0.0F {
    fail("ts_var of constant should be 0.0, got \{ts_var(x)}")
  }
}

///|
test "ts_std: matches sqrt(var)" {
  let x : Array[Float] = [1.0F, 2.0F, 3.0F, 4.0F, 5.0F]
  let s = ts_std(x)
  let expected = sqrtf(2.0F)
  if (s - expected).abs() > 0.0001F {
    fail("ts_std = \{s}, expected ~\{expected}")
  }
}

///|
test "ts_diff: first-order differencing of arithmetic-like series" {
  let x : Array[Float] = [10.0F, 13.0F, 15.0F, 18.0F, 22.0F]
  let d = ts_diff(x)
  assert_eq(d.length(), 4)
  assert_eq(d[0], 3.0F)
  assert_eq(d[1], 2.0F)
  assert_eq(d[2], 3.0F)
  assert_eq(d[3], 4.0F)
}

///|
test "ts_diff: empty / single-element inputs" {
  let empty : Array[Float] = []
  assert_eq(ts_diff(empty).length(), 0)
  let one : Array[Float] = [42.0F]
  assert_eq(ts_diff(one).length(), 0)
}

///|
test "ts_diff: differencing a constant series yields zeros" {
  let x : Array[Float] = [7.0F, 7.0F, 7.0F, 7.0F]
  let d = ts_diff(x)
  assert_eq(d.length(), 3)
  for i in 0..<3 {
    if d[i] != 0.0F {
      fail("d[\{i}] = \{d[i]}, expected 0")
    }
  }
}

///|
test "ts_diff_n: second-order differencing of quadratic series" {
  // x = [1, 3, 6, 10, 15, 21] = triangular numbers
  // diff(x)   = [2, 3, 4, 5, 6]
  // diff^2(x) = [1, 1, 1, 1]
  let x : Array[Float] = [1.0F, 3.0F, 6.0F, 10.0F, 15.0F, 21.0F]
  let d2 = ts_diff_n(x, 2)
  assert_eq(d2.length(), 4)
  for i in 0..<4 {
    if (d2[i] - 1.0F).abs() > 0.0001F {
      fail("d2[\{i}] = \{d2[i]}, expected 1.0")
    }
  }
}

///|
test "ts_diff_n: d=0 returns input unchanged (no differencing)" {
  let x : Array[Float] = [1.0F, 4.0F, 9.0F, 16.0F]
  let y = ts_diff_n(x, 0)
  assert_eq(y.length(), x.length())
  for i in 0..<x.length() {
    if y[i] != x[i] {
      fail("d=0 should return same values, mismatch at \{i}")
    }
  }
}

///|
test "ts_cumsum: cumulative sum of small series" {
  let x : Array[Float] = [1.0F, 2.0F, 3.0F, 4.0F, 5.0F]
  let cs = ts_cumsum(x)
  assert_eq(cs.length(), 5)
  assert_eq(cs[0], 1.0F)
  assert_eq(cs[1], 3.0F)
  assert_eq(cs[2], 6.0F)
  assert_eq(cs[3], 10.0F)
  assert_eq(cs[4], 15.0F)
}

///|
test "ts_cumsum: empty input returns empty array" {
  let empty : Array[Float] = []
  assert_eq(ts_cumsum(empty).length(), 0)
}

///|
test "ts_diff + ts_cumsum: round-trip reconstruction (modulo head)" {
  // Reconstruction: y[0] = x[0]; y[i+1] = x[0] + ts_cumsum(diff(x))[i]
  let x : Array[Float] = [1.0F, 3.0F, 6.0F, 10.0F, 15.0F]
  let d = ts_diff(x)
  let cs = ts_cumsum(d)
  let reconstructed : Array[Float] = Array::make(x.length(), 0.0F)
  reconstructed[0] = x[0]
  for i in 0..<cs.length() {
    reconstructed[i + 1] = x[0] + cs[i]
  }
  for i in 0..<x.length() {
    if (reconstructed[i] - x[i]).abs() > 0.0001F {
      fail("round-trip mismatch at \{i}: got \{reconstructed[i]}, expected \{x[i]}")
    }
  }
}

///|
test "ts_diff_n + ts_cumsum: 2-round-trip on quadratic series" {
  // x = [1, 3, 6, 10, 15, 21]
  // d1 = [2, 3, 4, 5, 6]
  // d2 = [1, 1, 1, 1]
  // Cumsum(d2) = [1, 2, 3, 4]
  // Cumsum(d1) = [2, 5, 9, 14, 20]
  // Reconstruct d1 from d2: d1[0]=2; d1[i+1] = 2 + Cumsum(d2)[i]
  //                       = [2, 3, 4, 5, 6] ✓
  let x : Array[Float] = [1.0F, 3.0F, 6.0F, 10.0F, 15.0F, 21.0F]
  let d2 = ts_diff_n(x, 2)
  let cs2 = ts_cumsum(d2)
  let d1_recon : Array[Float] = Array::make(cs2.length() + 1, 0.0F)
  d1_recon[0] = 2.0F
  for i in 0..<cs2.length() {
    d1_recon[i + 1] = 2.0F + cs2[i]
  }
  let expected_d1 : Array[Float] = [2.0F, 3.0F, 4.0F, 5.0F, 6.0F]
  for i in 0..<expected_d1.length() {
    if (d1_recon[i] - expected_d1[i]).abs() > 0.0001F {
      fail("2-round-trip mismatch at \{i}: got \{d1_recon[i]}, expected \{expected_d1[i]}")
    }
  }
}