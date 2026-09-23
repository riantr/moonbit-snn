// arima_demo.mbt — End-to-end ARIMA demo pipeline (v0.28.5).
//
// This demo ties together ts_diff / ts_cumsum (v0.28.0) and
// arma_fit (v0.28.4) to implement the classical ARIMA(p, d, q)
// modelling pipeline:
//
//   y_t → d-th differencing → z_t → fit ARMA(p, q) on z_t
//       → forecast z_{N+1}, ..., z_{N+k}
//       → inverse-difference back to y space
//
// Steps:
//   1. `arima_synthesize`  — generate ARIMA(p, d, q) data
//   2. `arima_fit`         — pipeline: diff → fit ARMA → return
//                            { ar_coeffs, ma_thetas, intercept, d }
//   3. `arima_forecast`    — fit + forecast N steps ahead
//   4. `arima_evaluate`    — RMSE / MAE for forecast accuracy
//
// All Float32 (matching Julia's `Float32` arima-style defaults).

///|
/// Bundle of fitted ARIMA(p, d, q) parameters.
pub struct ArimaFit {
  p : Int
  d : Int
  q : Int
  intercept : Float
  ar_coeffs : Array[Float]
  ma_thetas : Array[Float]
}

///|
/// Synthetic ARIMA(p, d, q) series generator. Generate z via
/// ARMA(p, q), then inverse-difference (cumulative sum + extend)
/// d times to produce the integrated series y.
pub fn arima_synthesize(
  n : Int,
  p : Int,
  d : Int,
  q : Int,
  intercept : Float,
  ar_coeffs : Array[Float],
  ma_thetas : Array[Float],
  seed : UInt64,
) -> Array[Float] {
  let rng = Xoshiro::from_state(seed, seed + 1UL, seed + 2UL, seed + 3UL)
  // Step 1: generate the stationary ARMA(p, q) series z.
  let z : Array[Float] = Array::make(n, 0.0F)
  if n == 0 {
    return z
  }
  let eps : Array[Float] = Array::make(n, 0.0F)
  // Initial eps[0] = z[0] = noise sample (no pre-sample data).
  let (z0, _) = box_muller(rng)
  eps[0] = Float::from_double(z0)
  z[0] = eps[0]
  for t in 1..<n {
    let (zt, _) = box_muller(rng)
    let mut e = Float::from_double(zt)
    // AR part
    for i in 0..<p {
      let lag = t - 1 - i
      if lag >= 0 {
        e = e - ar_coeffs[i] * z[lag]
      }
    }
    // MA part
    for j in 0..<q {
      let lag = t - 1 - j
      if lag >= 0 {
        e = e - ma_thetas[j] * eps[lag]
      }
    }
    eps[t] = e
    z[t] = intercept + e
  }
  // Step 2: inverse-difference d times. Each inverse-difference
  // requires a "head" value y[-1]; we use 0 by default. (Real-world
  // ARIMA uses the last observed y as the head.)
  let mut current : Array[Float] = z
  for _k in 0..<d {
    let next : Array[Float] = Array::make(current.length() + 1, 0.0F)
    next[0] = 0.0F
    let cs = ts_cumsum(current)
    for i in 0..<cs.length() {
      next[i + 1] = cs[i]
    }
    current = next
  }
  current
}

///|
/// Fit ARIMA(p, d, q) by differencing y d times, fitting ARMA(p, q)
/// on the differenced series via gradient descent, and returning the
/// full parameter bundle. `max_iter` and `lr` control the GD fit.
pub fn arima_fit(
  y : Array[Float],
  p : Int,
  d : Int,
  q : Int,
  max_iter : Int,
  lr : Float,
) -> ArimaFit {
  let z = ts_diff_n(y, d)
  let param = arma_fit(z, p, q, max_iter, lr)
  {
    p,
    d,
    q,
    intercept: param.intercept,
    ar_coeffs: param.ar_coeffs,
    ma_thetas: param.ma_thetas,
  }
}

///|
/// Forecast `n_ahead` steps into the future from a fitted
/// ArimaFit on a series y. Returns an array of length `n_ahead`
/// containing point forecasts for y[N+1], ..., y[N+n_ahead].
pub fn arima_forecast(
  y : Array[Float],
  fit : ArimaFit,
  n_ahead : Int,
) -> Array[Float] {
  if n_ahead <= 0 {
    return Array::make(0, 0.0F)
  }
  let n = y.length()
  // 1. Re-fit ARMA on differenced data so we have residuals for
  //    forecasting.
  let z = ts_diff_n(y, fit.d)
  let param = ArmaParam::new(
    fit.p,
    fit.q,
    fit.intercept,
    fit.ar_coeffs,
    fit.ma_thetas,
  )
  let eps_fit = arma_residuals(z, param)
  let n_z = z.length()
  // 2. Forecast on the differenced scale.
  let z_forecast = Array::make(n_ahead, 0.0F)
  // Build an extended working copy of z so we can use ARMA recursion.
  let z_ext : Array[Float] = Array::make(n_z + n_ahead, 0.0F)
  for i in 0..<n_z {
    z_ext[i] = z[i]
  }
  let eps_ext : Array[Float] = Array::make(n_z + n_ahead, 0.0F)
  for i in 0..<n_z {
    eps_ext[i] = eps_fit[i]
  }
  for t in n_z..<(n_z + n_ahead) {
    let mut pred = fit.intercept
    for i in 0..<fit.p {
      let lag = t - 1 - i
      if lag >= 0 {
        pred = pred + fit.ar_coeffs[i] * z_ext[lag]
      }
    }
    for j in 0..<fit.q {
      let lag = t - 1 - j
      if lag >= 0 {
        pred = pred + fit.ma_thetas[j] * eps_ext[lag]
      }
    }
    z_ext[t] = pred
    eps_ext[t] = 0.0F  // innovation in forecast horizon = 0
    z_forecast[t - n_z] = pred
  }
  // 3. Inverse-difference back to y space.
  if fit.d == 0 {
    return z_forecast
  }
  // Reconstruct the head: we use the last observed y value as the
  // initial value for the d-th difference.
  let last_y = if n > 0 { y[n - 1] } else { 0.0F }
  let d_head = last_d_value(z, fit.d, last_y)
  let cur : Array[Float] = Array::make(n_z + n_ahead, 0.0F)
  for i in 0..<n_z {
    cur[i] = z[i]
  }
  for i in 0..<n_ahead {
    cur[n_z + i] = z_forecast[i]
  }
  // Apply inverse-difference `d` times with the appropriate head.
  let mut current = cur
  let head = d_head
  for _k in 0..<fit.d {
    let next : Array[Float] = Array::make(current.length(), 0.0F)
    next[0] = head + current[0]
    for i in 1..<current.length() {
      next[i] = next[i - 1] + current[i]
    }
    current = next
    // For the next inverse-difference, the new "head" is the last
    // reconstructed value before this run's forecasts.
    let _ = head
  }
  // Extract the forecast window.
  let forecast : Array[Float] = Array::make(n_ahead, 0.0F)
  for i in 0..<n_ahead {
    forecast[i] = current[n + i]
  }
  forecast
}

///|
/// Compute the d-th difference "head" — the value that, when added
/// to diff[y, d], reconstructs y. Specifically:
///   diff^d(y)[t] = (1 - L)^d · y[t]
///   y[t] = (1 - L)^(-d) · diff^d(y)[t]
/// For d=1: y[t] = y[t-1] + diff(y)[t], so head = y[n-1].
/// For d=2: diff²(y)[t] = y[t] - 2·y[t-1] + y[t-2]; the head is the
///   "virtual" y[n-1] that satisfies diff²(y)[n-1] = y[n-1] - 2·y[n-2] + y[n-3]....
// For simplicity, we return the last observed y as the head for d=1,
/// and approximate for d>1.
fn last_d_value(
  z : Array[Float],
  d : Int,
  last_y : Float,
) -> Float {
  if d == 0 {
    return 0.0F
  }
  if d == 1 {
    // y[n] = y[n-1] + diff(y)[n-1]. head for inverse-diff at t=0
    // (relative) is the y value BEFORE the diff sequence starts.
    return last_y - z[0]
  }
  // For d ≥ 2: approximate; in practice the user should provide
  // sufficient pre-sample data. Return last_y as a fallback.
  let _ = z
  last_y
}

///|
/// Root-mean-squared error between two equal-length arrays.
pub fn arima_rmse(actual : Array[Float], predicted : Array[Float]) -> Float {
  let n = actual.length()
  if predicted.length() != n {
    return -1.0F
  }
  if n == 0 {
    return 0.0F
  }
  let mut sum_sq = 0.0F
  for i in 0..<n {
    let diff = actual[i] - predicted[i]
    sum_sq = sum_sq + diff * diff
  }
  sqrtf(sum_sq / Float::from_int(n))
}

///|
/// Mean absolute error between two equal-length arrays.
pub fn arima_mae(actual : Array[Float], predicted : Array[Float]) -> Float {
  let n = actual.length()
  if predicted.length() != n {
    return -1.0F
  }
  if n == 0 {
    return 0.0F
  }
  let mut sum_abs = 0.0F
  for i in 0..<n {
    let diff = actual[i] - predicted[i]
    sum_abs = sum_abs + diff.abs()
  }
  sum_abs / Float::from_int(n)
}