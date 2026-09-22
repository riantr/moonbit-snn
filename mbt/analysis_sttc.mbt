// analysis_sttc.mbt — Spike-Time Tiling Coefficient (STTC) analysis.
// Bit-exact port of SNNModels.jl/src/analysis/targets.jl (STTC functions).
//
// STTC (Cutts & Eglen 2014) measures correlation between two spike
// trains over a time interval. Returns a value in [-1, 1]:
//   -1 : perfectly anti-correlated (one fires, the other is silent)
//    0 : independent
//   +1 : perfectly correlated (one fires iff the other does, ±dt)
//
// Formula (symmetric form):
//   PA = (#A spikes with a B partner within ±dt) / (#A spikes)
//   PB = (#B spikes with an A partner within ±dt) / (#B spikes)
//   TA = sum of ±dt tile lengths / T (tile fraction for A)
//   TB = sum of ±dt tile lengths / T (tile fraction for B)
//   STTC = 0.5 * ((PA - TB) / (1 - PA*TB) + (PB - TA) / (1 - PB*TA))

///|
/// Tile fraction for a sorted spike train in [istart, iend] with
/// window ±dt. The total length covered by union of [t-dt, t+dt]
/// intervals around each spike, divided by (iend - istart + 2*dt).
/// Mirrors Julia's `_tile_fraction(spiketrain, Δt, istart, iend)`.
pub fn tile_fraction(
  spiketrain : Array[Float],
  dt : Float,
  istart : Float,
  iend : Float,
) -> Float {
  if spiketrain.length() == 0 {
    return 0.0F
  }
  // Start with the first spike's tile length (Δt).
  let mut width : Float = dt
  for n in 1..<spiketrain.length() {
    let t = spiketrain[n]
    // Skip spikes outside the interval.
    if t < istart {
      continue
    }
    if t > iend {
      continue
    }
    // Gap to previous spike (which is also within the interval).
    let gap = t - spiketrain[n - 1]
    width = width + (if gap < 2.0F * dt { gap } else { 2.0F * dt })
  }
  // Add the last spike's right-side tile.
  width = width + dt
  let denom = iend - istart + 2.0F * dt
  if denom <= 0.0F {
    return 0.0F
  }
  width / denom
}

///|
/// Fraction of spikes in A that have a coincident spike in B within
/// ±dt. Returns 0.0 if A is empty. B must be pre-sorted; A is
/// iterated linearly and binary-searched in B (O(N_A log N_B)).
pub fn coincident_fraction(
  a : Array[Float],
  b : Array[Float],
  dt : Float,
) -> Float {
  if a.length() == 0 {
    return 0.0F
  }
  let n_b = b.length()
  let mut count = 0
  for t in a {
    // Binary search: first index in B such that B[idx] >= t - dt.
    let lo_val = t - dt
    let mut lo = 0
    let mut hi = n_b
    while lo < hi {
      let mid = (lo + hi) / 2
      if b[mid] < lo_val {
        lo = mid + 1
      } else {
        hi = mid
      }
    }
    // Check if B[lo] (if exists) is within ±dt of t.
    if lo < n_b && b[lo] <= t + dt {
      count = count + 1
    }
  }
  Float::from_int(count) / a.length().to_float()
}

///|
/// Compute the STTC value between two spike trains (must be pre-sorted
/// ascending; use the wrapper `sttc_pair` which sorts for you).
pub fn sttc_pair_sorted(
  a : Array[Float],
  b : Array[Float],
  ta : Float,
  tb : Float,
  dt : Float,
) -> Float {
  let pa = coincident_fraction(a, b, dt)
  let pb = coincident_fraction(b, a, dt)
  let denom_a = 1.0F - pa * tb
  let denom_b = 1.0F - pb * ta
  // Guard against degenerate case where both terms blow up
  // (PA = PB = 1 and TA = TB = 0 — perfectly correlated with no
  // overlap; STTC = 1 by convention).
  let term_a = if denom_a > 1.0e-30F { (pa - tb) / denom_a } else { 1.0F }
  let term_b = if denom_b > 1.0e-30F { (pb - ta) / denom_b } else { 1.0F }
  0.5F * (term_a + term_b)
}

///|
/// STTC between two spike trains. Sorts the inputs (in-place copies)
/// before computing.
pub fn sttc_pair(
  a : Array[Float],
  b : Array[Float],
  dt : Float,
  istart : Float,
  iend : Float,
) -> Float {
  let a_sorted = sort_floats(a)
  let b_sorted = sort_floats(b)
  let ta = tile_fraction(a_sorted, dt, istart, iend)
  let tb = tile_fraction(b_sorted, dt, istart, iend)
  sttc_pair_sorted(a_sorted, b_sorted, ta, tb, dt)
}

///|
/// STTC matrix for N spike trains. Returns a N×N Float matrix
/// (symmetric, diagonal = 1.0). Each (i, j) entry is `sttc_pair` for
/// the i-th and j-th trains.
pub fn sttc_matrix(
  trains : Array[Array[Float]],
  dt : Float,
  istart : Float,
  iend : Float,
) -> Array[Array[Float]] {
  let n = trains.length()
  let mat : Array[Array[Float]] = Array::make(n, [])
  for i in 0..<n {
    let row : Array[Float] = Array::make(n, 0.0F)
    for j in 0..<n {
      if i == j {
        row[j] = 1.0F
      } else {
        row[j] = sttc_pair(trains[i], trains[j], dt, istart, iend)
      }
    }
    mat[i] = row
  }
  mat
}

///|
/// Simple ascending sort (Float). Returns a new array. Used as a
/// helper for `sttc_pair` since both Julia and our STTC require
/// sorted inputs.
fn sort_floats(arr : Array[Float]) -> Array[Float] {
  let n = arr.length()
  let result : Array[Float] = Array::make(n, 0.0F)
  for i in 0..<n {
    result[i] = arr[i]
  }
  // Insertion sort (n is small for typical spike trains; ~10-100 spikes).
  for i in 1..<n {
    let key = result[i]
    let mut j = i - 1
    while j >= 0 && result[j] > key {
      result[j + 1] = result[j]
      j = j - 1
    }
    result[j + 1] = key
  }
  result
}