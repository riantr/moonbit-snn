// Tests for the STTC (Spike-Time Tiling Coefficient) analysis module.
// Bit-exact port of SNNModels.jl/test/analysis/sttc.jl internals tests.

///|
test "tile_fraction — empty train returns 0" {
  let tf = tile_fraction([], 0.1F, 0.0F, 1.0F)
  assert_eq(tf, 0.0F)
}

///|
test "tile_fraction — single spike inside interval" {
  // One spike at t=0.5, dt=0.1, istart=0, iend=1.0
  // Tile length = Δt (first spike) + Δt (last spike) = 0.2
  // Denominator = 1.0 - 0.0 + 2*0.1 = 1.2
  // Tile fraction = 0.2 / 1.2 ≈ 0.1667
  let tf = tile_fraction([0.5F], 0.1F, 0.0F, 1.0F)
  let expected : Float = 0.2F / 1.2F
  if tf != expected {
    fail("tile_fraction = \{tf} != expected \{expected}")
  }
}

///|
test "tile_fraction — two spikes far apart (non-overlapping)" {
  // Spikes at 0.2 and 0.8, gap = 0.6 > 2*dt = 0.2 → tile length = dt + gap + dt = 0.4
  // (Julia formula: width = dt; then for each gap, + min(gap, 2*dt);
  // final + dt; total = dt + min(gap, 2*dt) + dt = 0.1 + 0.2 + 0.1 = 0.4)
  // Denominator = 1.0 + 2*0.1 = 1.2
  // Tile fraction = 0.4 / 1.2 = 0.333
  let tf = tile_fraction([0.2F, 0.8F], 0.1F, 0.0F, 1.0F)
  let expected : Float = 0.4F / 1.2F
  if tf != expected {
    fail("tile_fraction = \{tf} != expected \{expected}")
  }
}

///|
test "tile_fraction — two spikes close (overlapping tiles)" {
  // Spikes at 0.5 and 0.55, gap = 0.05 < 2*dt = 0.2 → tile length = dt + gap + dt = 0.25
  // Denominator = 1.2
  // Tile fraction = 0.25 / 1.2 = 0.208
  let tf = tile_fraction([0.5F, 0.55F], 0.1F, 0.0F, 1.0F)
  let expected : Float = 0.25F / 1.2F
  if tf != expected {
    fail("tile_fraction = \{tf} != expected \{expected}")
  }
}

///|
test "coincident_fraction — empty A returns 0" {
  let cf = coincident_fraction([], [0.5F], 0.1F)
  assert_eq(cf, 0.0F)
}

///|
test "coincident_fraction — exact match returns 1" {
  let cf = coincident_fraction([0.5F], [0.5F], 0.1F)
  assert_eq(cf, 1.0F)
}

///|
test "coincident_fraction — within window returns 1" {
  // 0.5 ± 0.09 = within dt=0.1
  let cf = coincident_fraction([0.5F], [0.59F], 0.1F)
  assert_eq(cf, 1.0F)
}

///|
test "coincident_fraction — outside window returns 0" {
  // 0.5 + 0.11 > 0.1 (outside ±dt)
  let cf = coincident_fraction([0.5F], [0.61F], 0.1F)
  assert_eq(cf, 0.0F)
}

///|
test "coincident_fraction — half coincident" {
  // A = [0.5, 0.9]; B = [0.5, 0.95]
  // A[0]=0.5 → B[0]=0.5 within dt=0.1 → coincident
  // A[1]=0.9 → B[1]=0.95, |0.05| < 0.1 → coincident
  // → 2/2 = 1.0
  let cf = coincident_fraction([0.5F, 0.9F], [0.5F, 0.95F], 0.1F)
  assert_eq(cf, 1.0F)
  // Now widen the gap: A = [0.5, 0.9]; B = [0.5, 1.0]
  // A[1]=0.9 → no B within [0.8, 1.0] (B[1]=1.0 not ≤ 1.0±0.1=1.1? Wait
  // coincidence is |t_A - t_B| ≤ dt. For t_A=0.9, t_B=1.0: diff=0.1 ≤ 0.1 → coincident.
  let cf2 = coincident_fraction([0.5F, 0.9F], [0.5F, 1.0F], 0.1F)
  assert_eq(cf2, 1.0F)
}

///|
test "sttc_pair — identical trains give 1.0" {
  let a : Array[Float] = [0.1F, 0.3F, 0.5F, 0.7F, 0.9F]
  let s = sttc_pair(a, a, 0.05F, 0.0F, 1.0F)
  assert_eq(s, 1.0F)
}

///|
test "sttc_pair — symmetric (STTC(A,B) = STTC(B,A))" {
  let a : Array[Float] = [0.1F, 0.3F, 0.5F, 0.7F, 0.9F]
  let b : Array[Float] = [0.15F, 0.35F, 0.55F, 0.75F, 0.95F]
  let sa = sttc_pair(a, b, 0.05F, 0.0F, 1.0F)
  let sb = sttc_pair(b, a, 0.05F, 0.0F, 1.0F)
  if sa != sb {
    fail("STTC(A,B) = \{sa} != STTC(B,A) = \{sb}")
  }
  // Value should be positive (high overlap) and < 1.0.
  if sa <= 0.0F || sa >= 1.0F {
    fail("STTC = \{sa} not in (0, 1)")
  }
}

///|
test "sttc_pair — non-overlapping trains give negative STTC" {
  // Two trains with no temporal overlap (gap >> dt)
  let c : Array[Float] = [0.1F, 0.2F, 0.3F]
  let d : Array[Float] = [0.7F, 0.8F, 0.9F]
  let s = sttc_pair(c, d, 0.01F, 0.0F, 1.0F)
  // PA = PB = 0 (no coincidence), TB > 0, TA > 0
  // → STTC = 0.5 * ((0 - TB) / (1 - 0) + (0 - TA) / (1 - 0))
  //        = -0.5 * (TA + TB) < 0
  if s >= 0.0F {
    fail("STTC = \{s} should be negative for non-overlapping trains")
  }
}

///|
test "sttc_matrix — diagonal is 1.0, symmetric, bounded in [-1, 1]" {
  let t1 : Array[Float] = [0.1F, 0.3F, 0.5F, 0.7F, 0.9F]
  let t2 : Array[Float] = [0.15F, 0.35F, 0.55F, 0.75F, 0.95F]
  let t3 : Array[Float] = [0.4F, 0.6F, 0.8F]  // partial overlap with t1, t2
  let trains : Array[Array[Float]] = [t1, t2, t3]
  let mat = sttc_matrix(trains, 0.05F, 0.0F, 1.0F)
  assert_eq(mat.length(), 3)
  for i in 0..<3 {
    assert_eq(mat[i].length(), 3)
    // Diagonal = 1
    assert_eq(mat[i][i], 1.0F)
    // Off-diagonal in [-1, 1]
    for j in 0..<3 {
      if i == j { continue }
      if mat[i][j] < -1.0F || mat[i][j] > 1.0F {
        fail("mat[\{i}][\{j}] = \{mat[i][j]} out of [-1, 1]")
      }
      // Symmetric
      if mat[i][j] != mat[j][i] {
        fail("mat[\{i}][\{j}] = \{mat[i][j]} != mat[\{j}][\{i}] = \{mat[j][i]}")
      }
    }
  }
}