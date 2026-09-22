// analysis_smooth.mbt — bit-exact port of
// `refs/SNNModels.jl/src/analysis/spikes.jl::gaussian_smooth`.
//
// Apply a normalized Gaussian kernel to a signal `x` sampled on grid
// `xs`. Kernel half-width is `3σ` (truncated); boundary bins are
// renormalized by accumulated kernel weight so edge values are not
// biased toward zero.
//
// Julia signature:
//   gaussian_smooth(xs, x, sigma; skewed=:none) -> Vector
// where `skewed` is :none / :left / :right. In MoonBit we use the
// `SmoothSkew` enum (no first-class Symbol type).

///|
/// Skew option for `gaussian_smooth` (substitute for Julia's
/// `skewed::Symbol`).
pub(all) enum SmoothSkew {
  /// Symmetric kernel: -half:half.
  None
  /// Causal: only past samples (-half:0).
  Left
  /// Anti-causal: only future samples (0:half).
  Right
}

///|
/// Apply a normalised Gaussian kernel to `x` sampled on `xs`.
///
///   xs    : position grid (Float[]); only step size `xs[1]-xs[0]` is used
///   x     : signal to smooth (Float[], length n)
///   sigma : kernel std-dev in same units as `xs`
///   skew  : SmoothSkew::None / Left / Right
///
/// Returns Float[] of length n. Boundary bins are renormalised by
/// accumulated kernel weight so edge values aren't biased to zero.
pub fn gaussian_smooth(
  xs : Array[Float],
  x : Array[Float],
  sigma : Float,
  skew : SmoothSkew,
) -> Array[Float] {
  let n = x.length()
  let out : Array[Float] = Array::make(n, 0.0F)
  // Zero sigma → copy x.
  if sigma == 0.0F {
    let mut i = 0
    while i < n {
      out[i] = x[i]
      i = i + 1
    }
    return out
  }
  let step_x = xs[1] - xs[0]
  // Compute kernel half-width (in samples).
  let half_f = 3.0F * sigma / step_x
  let half : Int = half_f.to_int()
  // Build offset range based on skew.
  let offsets : Array[Int] = match skew {
    None => {
      let arr : Array[Int] = []
      let mut i = -half
      while i <= half {
        arr.push(i)
        i = i + 1
      }
      arr
    }
    Left => {
      let arr : Array[Int] = []
      let mut i = -half
      while i <= 0 {
        arr.push(i)
        i = i + 1
      }
      arr
    }
    Right => {
      let arr : Array[Int] = []
      let mut i = 0
      while i <= half {
        arr.push(i)
        i = i + 1
      }
      arr
    }
  }
  // Compute kernel values (Gaussian).
  let two_sigma_sq = 2.0F * sigma * sigma
  let kernel : Array[Float] = Array::make(offsets.length(), 0.0F)
  let mut k = 0
  while k < offsets.length() {
    let off_f = Float::from_int(offsets[k]) * step_x
    let x_sq = off_f * off_f
    kernel[k] = expf(-x_sq / two_sigma_sq)
    k = k + 1
  }
  // Normalise kernel.
  let mut k_sum = 0.0F
  let mut j = 0
  while j < kernel.length() {
    k_sum = k_sum + kernel[j]
    j = j + 1
  }
  let mut j2 = 0
  while j2 < kernel.length() {
    kernel[j2] = kernel[j2] / k_sum
    j2 = j2 + 1
  }
  // Apply kernel with boundary renormalisation.
  let lo = offsets[0]
  let mut i = 0
  while i < n {
    let mut s = 0.0F
    let mut w = 0.0F
    let mut jj = 0
    while jj < offsets.length() {
      let idx = i + lo + jj
      if idx < 0 || idx >= n {
        jj = jj + 1
        continue
      }
      s = s + kernel[jj] * x[idx]
      w = w + kernel[jj]
      jj = jj + 1
    }
    out[i] = if w > 0.0F { s / w } else { 0.0F }
    i = i + 1
  }
  out
}