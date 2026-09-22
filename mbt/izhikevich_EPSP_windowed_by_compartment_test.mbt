// Tests for epsp_normalise_windowed_by_compartment (v0.10.129)
// Distinct baseline window (win_start..win_end) for mean/std
// and observation window (t_start..t_end) for z-scores.

///|
test "epsp_normalise_windowed_by_compartment / distinct windows give non-zero std" {
  let n_compartments = 1
  // 10 time-steps, 1 compartment; baseline = first 5 (constant 0.0),
  // observation = last 5 (constant 1.0). mean over baseline = 0,
  // std over baseline = 0 -> degenerate, all zeros.
  let v : Array[Float] = [0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F]
  let z = @snn_mbt.epsp_normalise_windowed_by_compartment(
    v, n_compartments, 5, 9, 0, 4,
  )
  if z.length() != 1 {
    fail("expected 1 compartment, got \{z.length()}")
  }
  if z[0].length() != 5 {
    fail("expected 5 z-scores, got \{z[0].length()}")
  }
  // Degenerate baseline (std=0) -> all zeros in observation window.
  for i in 0..<5 {
    if z[0][i] != 0.0F {
      fail("degenerate baseline should yield zeros, got \{z[0][i]} at \{i}")
    }
  }
}

///|
test "epsp_normalise_windowed_by_compartment / baseline std > 0 yields non-trivial z" {
  let n_compartments = 1
  // 8 time-steps, 1 compartment; baseline = t=0..3 (values 1,2,3,4),
  // observation = t=4..7 (values 5,6,7,8).
  // mean over baseline = 2.5, std_pop = sqrt(1.25) ≈ 1.118.
  let v : Array[Float] = [1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F, 7.0F, 8.0F]
  let z = @snn_mbt.epsp_normalise_windowed_by_compartment(
    v, n_compartments, 4, 7, 0, 3,
  )
  if z.length() != 1 {
    fail("expected 1 compartment, got \{z.length()}")
  }
  if z[0].length() != 4 {
    fail("expected 4 z-scores, got \{z[0].length()}")
  }
  // z[0] = (5 - 2.5) / sqrt(1.25) = 2.5 / 1.118034 ≈ 2.2360679
  if (z[0][0] - 2.2360679F).abs() > 0.001F {
    fail("first z-score should be ≈2.236, got \{z[0][0]}")
  }
  // z[3] = (8 - 2.5) / sqrt(1.25) = 5.5 / 1.118034 ≈ 4.9193
  if (z[0][3] - 4.9193F).abs() > 0.001F {
    fail("last z-score should be ≈4.919, got \{z[0][3]}")
  }
}

///|
test "epsp_normalise_windowed_by_compartment / multi-compartment per-column z-scores" {
  let n_compartments = 2
  // 4 time-steps, 2 compartments.
  // Flat row-major: v[c + t * n_compartments].
  // comp 0: t=0..3 values 1,2,3,4 -> mean=2.5, std_pop=sqrt(1.25)
  // comp 1: t=0..3 values 10,20,30,40 -> mean=25, std_pop=sqrt(125)
  let v : Array[Float] = [
    1.0F, 10.0F, // t=0
    2.0F, 20.0F, // t=1
    3.0F, 30.0F, // t=2
    4.0F, 40.0F, // t=3
  ]
  // Baseline = t=0..1, observation = t=2..3.
  let z = @snn_mbt.epsp_normalise_windowed_by_compartment(
    v, n_compartments, 2, 3, 0, 1,
  )
  if z.length() != 2 {
    fail("expected 2 compartments, got \{z.length()}")
  }
  // comp 0 baseline: mean over [1,2] = 1.5, std_pop = sqrt(0.25) = 0.5.
  // obs t=2: (3-1.5)/0.5 = 3.0
  // obs t=3: (4-1.5)/0.5 = 5.0
  if (z[0][0] - 3.0F).abs() > 0.001F {
    fail("comp 0 first z-score should be 3.0, got \{z[0][0]}")
  }
  if (z[0][1] - 5.0F).abs() > 0.001F {
    fail("comp 0 second z-score should be 5.0, got \{z[0][1]}")
  }
  // comp 1 baseline: mean over [10,20] = 15, std_pop = sqrt(25) = 5.0.
  // obs t=2: (30-15)/5 = 3.0
  // obs t=3: (40-15)/5 = 5.0
  if (z[1][0] - 3.0F).abs() > 0.001F {
    fail("comp 1 first z-score should be 3.0, got \{z[1][0]}")
  }
  if (z[1][1] - 5.0F).abs() > 0.001F {
    fail("comp 1 second z-score should be 5.0, got \{z[1][1]}")
  }
}

///|
test "epsp_normalise_windowed_by_compartment / invalid observation window returns empty" {
  let n_compartments = 1
  let v : Array[Float] = [1.0F, 2.0F, 3.0F, 4.0F, 5.0F]
  // t_end out of range.
  let z = @snn_mbt.epsp_normalise_windowed_by_compartment(
    v, n_compartments, 0, 10, 0, 4,
  )
  if z.length() != 0 {
    fail("invalid t_end should yield empty, got \{z.length()} compartments")
  }
  // t_start > t_end.
  let z2 = @snn_mbt.epsp_normalise_windowed_by_compartment(
    v, n_compartments, 4, 2, 0, 4,
  )
  if z2.length() != 0 {
    fail("t_start > t_end should yield empty, got \{z2.length()}")
  }
  // Negative t_start.
  let z3 = @snn_mbt.epsp_normalise_windowed_by_compartment(
    v, n_compartments, -1, 4, 0, 4,
  )
  if z3.length() != 0 {
    fail("negative t_start should yield empty, got \{z3.length()}")
  }
}

///|
test "epsp_normalise_windowed_by_compartment / invalid baseline window returns empty" {
  let n_compartments = 1
  let v : Array[Float] = [1.0F, 2.0F, 3.0F, 4.0F, 5.0F]
  // win_end out of range.
  let z = @snn_mbt.epsp_normalise_windowed_by_compartment(
    v, n_compartments, 0, 4, 0, 10,
  )
  if z.length() != 0 {
    fail("invalid win_end should yield empty, got \{z.length()}")
  }
  // win_start > win_end.
  let z2 = @snn_mbt.epsp_normalise_windowed_by_compartment(
    v, n_compartments, 0, 4, 4, 2,
  )
  if z2.length() != 0 {
    fail("win_start > win_end should yield empty, got \{z2.length()}")
  }
  // Negative win_start.
  let z3 = @snn_mbt.epsp_normalise_windowed_by_compartment(
    v, n_compartments, 0, 4, -1, 2,
  )
  if z3.length() != 0 {
    fail("negative win_start should yield empty, got \{z3.length()}")
  }
}