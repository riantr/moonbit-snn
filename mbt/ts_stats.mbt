// ts_stats.mbt — Time-series preprocessing primitives (v0.28.0).
//
// Building blocks for ARIMA / classical time-series modelling:
//   - mean, variance, standard deviation
//   - first-order and higher-order differencing (the "I" in ARIMA)
//   - cumulative sum (inverse of differencing)
//
// All operations work on 1D `Array[Float]` (Float32 for bit-exact
// Julia parity). The variance divisor is `n` (population variance),
// matching Julia's `Statistics.var` default with `corrected=false`.

///|
/// 1D arithmetic mean. Returns 0.0 for an empty input.
pub fn ts_mean(x : Array[Float]) -> Float {
  let n = x.length()
  if n == 0 {
    return 0.0F
  }
  let mut sum = 0.0F
  for i in 0..<n {
    sum = sum + x[i]
  }
  sum / Float::from_int(n)
}

///|
/// 1D variance with population divisor `n` (matches Julia's
/// `Statistics.var(...; corrected=false)`). Returns 0.0 for empty
/// input.
pub fn ts_var(x : Array[Float]) -> Float {
  let n = x.length()
  if n == 0 {
    return 0.0F
  }
  let mu = ts_mean(x)
  let mut ss = 0.0F
  for i in 0..<n {
    let d = x[i] - mu
    ss = ss + d * d
  }
  ss / Float::from_int(n)
}

///|
/// 1D standard deviation (sqrt of population variance).
pub fn ts_std(x : Array[Float]) -> Float {
  sqrtf(ts_var(x))
}

///|
/// First-order differencing: `d[i] = x[i+1] - x[i]`. Returns an
/// array of length `n - 1`; returns an empty array for inputs of
/// length 0 or 1.
pub fn ts_diff(x : Array[Float]) -> Array[Float] {
  let n = x.length()
  if n < 2 {
    return Array::make(0, 0.0F)
  }
  let out : Array[Float] = Array::make(n - 1, 0.0F)
  for i in 0..<(n - 1) {
    out[i] = x[i + 1] - x[i]
  }
  out
}

///|
/// `d`-order differencing — equivalent to applying `ts_diff` `d`
/// times in sequence. For `d = 0` returns `x` unchanged; for `d ≥ 1`
/// returns an array of length `n - d`. This is the "I(d)" component
/// of ARIMA(p, d, q).
pub fn ts_diff_n(x : Array[Float], d : Int) -> Array[Float] {
  if d <= 0 {
    return x
  }
  let mut current = x
  for _i in 0..<d {
    current = ts_diff(current)
  }
  current
}

///|
/// Cumulative sum: `out[0] = x[0]`, `out[i] = out[i-1] + x[i]`.
/// Returns an empty array for empty input. The inverse of
/// differencing (modulo the leading value): if `d = ts_diff(x)` then
/// `x` can be reconstructed by `[x[0]] ++ (x[0] .+ ts_cumsum(d))`.
pub fn ts_cumsum(x : Array[Float]) -> Array[Float] {
  let n = x.length()
  if n == 0 {
    return Array::make(0, 0.0F)
  }
  let out : Array[Float] = Array::make(n, 0.0F)
  out[0] = x[0]
  for i in 1..<n {
    out[i] = out[i - 1] + x[i]
  }
  out
}