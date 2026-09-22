// bimodal_kernel.mbt — bimodality detection in voltage traces.
//
// Port of `refs/SNNUtils.jl/src/stimuli/balance_EI/bimodal_kernel.jl`.
// Implements the kernel density estimation (KDE) bimodality test from
// Silverman 1981 (JRSS-B):
//   1. Approximate the distribution with Normal kernels of width h
//      at sample points t in v_range.
//   2. Detect local maxima in the resulting KDE curve.
//   3. Classify the curve as bimodal if >1 maximum is significant
//      (above `ratio * global_max`).
//   4. Find the critical bandwidth `h*` below which the KDE becomes
//      bimodal (used to decide if the underlying distribution is
//      genuinely bimodal vs an artifact of smoothing).
//
// All math uses Float64 (Double) internally — Float32 loses precision
// for KDE densities, and the Julia reference uses Float64 throughout.

///|
/// Normal-kernel density estimate at sample point `t`, using window
/// width `h` over `data`. Returns Float64.
/// Formula: `KDE(t) = (1/N) * (1/h) * sum exp(-((x_i - t)^2) / h)`
pub fn kde(t : Double, h : Double, data : Array[Double]) -> Double {
  let n_d = Float::from_int(data.length()).to_double()
  if n_d == 0.0 {
    return 0.0
  }
  let mut s : Double = 0.0
  for i in 0..<data.length() {
    let diff = data[i] - t
    s = s + @math.exp(-(diff * diff) / h)
  }
  s / n_d / h
}

///|
/// Compute the global KDE curve over `v_range` for a given `h`.
/// Returns Array[Double] of length `v_range.length()`.
/// Float64 throughout (Julia's Float64 reference).
pub fn global_kde(
  h : Double,
  data : Array[Double],
  v_range : Array[Double],
) -> Array[Double] {
  let n = v_range.length()
  let result : Array[Double] = Array::make(n, 0.0)
  for i in 0..<n {
    result[i] = kde(v_range[i], h, data)
  }
  result
}

///|
/// Find indices of local maxima in `data` (strict: `data[i] > data[i-1]`
/// AND `data[i] > data[i+1]`). Excludes endpoints.
/// Returns Array[Int] of 0-based indices into `data`.
pub fn get_maxima(data : Array[Double]) -> Array[Int] {
  let n = data.length()
  let result : Array[Int] = []
  if n < 3 {
    return result
  }
  for i in 1..<(n - 1) {
    if data[i] > data[i - 1] && data[i] > data[i + 1] {
      result.push(i)
    }
  }
  result
}

///|
/// Check if `data` (a KDE curve) is bimodal at the given `ratio`
/// threshold. A local maximum is "real" if `kernel[max] > ratio * global_max`.
/// Returns true if ≥ 2 real maxima.
pub fn is_bimodal(kernel : Array[Double], ratio : Double) -> Bool {
  let maxima = get_maxima(kernel)
  if maxima.length() == 0 {
    return false
  }
  // Find global max value across `maxima`.
  let mut z : Double = 0.0
  for i in 0..<maxima.length() {
    let v = kernel[maxima[i]]
    if v > z {
      z = v
    }
  }
  if z == 0.0 {
    return false
  }
  // Count "real" maxima above ratio * z.
  let mut real_count : Int = 0
  for i in 0..<maxima.length() {
    let m = kernel[maxima[i]]
    let abs_ratio = m / z
    if abs_ratio > ratio {
      real_count = real_count + 1
    }
  }
  real_count > 1
}

///|
/// Count the number of "real" maxima in `kernel` above the `ratio` threshold.
/// Mirrors Julia's `count_maxima`.
pub fn count_maxima(kernel : Array[Double], ratio : Double) -> Int {
  let maxima = get_maxima(kernel)
  if maxima.length() == 0 {
    return 0
  }
  let mut z : Double = 0.0
  for i in 0..<maxima.length() {
    let v = kernel[maxima[i]]
    if v > z {
      z = v
    }
  }
  if z == 0.0 {
    return 0
  }
  let mut real_count : Int = 0
  for i in 0..<maxima.length() {
    let m = kernel[maxima[i]]
    let abs_ratio = m / z
    if abs_ratio > ratio {
      real_count = real_count + 1
    }
  }
  real_count
}

///|
/// Find the critical window `h*` below which the KDE becomes bimodal.
/// Searches `h ∈ {1, 3, 5, ..., max_b}` (odd integers). Returns the
/// first `h` where `is_bimodal(global_kde(h, data, v_range), ratio)` is
/// false (unimodal). Returns `max_b` if no such `h` exists.
pub fn critical_window(
  data : Array[Double],
  ratio : Double,
  max_b : Int,
  v_range : Array[Double],
) -> Int {
  let mut h : Int = 1
  while h <= max_b {
    let h_d = Float::from_int(h).to_double()
    let kernel = global_kde(h_d, data, v_range)
    let bimodal = is_bimodal(kernel, ratio)
    if !bimodal {
      return h
    }
    h = h + 2
  }
  max_b
}

///|
/// Count maxima per bandwidth `h ∈ {1, 2, ..., max_b}`. Mirrors Julia's
/// `all_windows(data, ratio, max_b)` — returns an `Array[Int]` of length
/// `max_b` where entry `[h-1]` is `count_maxima(global_kde(h, data, v_range), ratio)`.
pub fn all_windows(
  data : Array[Double],
  ratio : Double,
  max_b : Int,
  v_range : Array[Double],
) -> Array[Int] {
  let counter : Array[Int] = Array::make(max_b, 0)
  for h in 1..<=max_b {
    let h_d = Float::from_int(h).to_double()
    let kernel = global_kde(h_d, data, v_range)
    counter[h - 1] = count_maxima(kernel, ratio)
  }
  counter
}