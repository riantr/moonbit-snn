// ts_acf.mbt — Autocorrelation / partial autocorrelation (v0.28.1).
//
// Used to identify the order of ARIMA models:
//   - PACF cuts off after lag p for a pure AR(p) process → use PACF
//     plot to choose the AR order `p`.
//   - ACF cuts off after lag q for a pure MA(q) process → use ACF
//     plot to choose the MA order `q`.
//
// All operations work on 1D `Array[Float]` (Float32 for bit-exact
// Julia parity). The ACF uses the *biased* estimator (divisor n in
// the denominator) matching Julia's `StatsBase.autocor` default.

///|
/// Autocorrelation function (ACF) at lags `0..max_lag`. Returns an
/// array of length `max_lag + 1` where `out[0] = 1` always. Uses the
/// biased estimator: `ρ(k) = Σ(x[i]-μ)(x[i+k]-μ) / Σ(x[i]-μ)²`,
/// i.e. the denominator is fixed at the lag-0 value (rather than
/// `n - k`).
pub fn ts_acf(x : Array[Float], max_lag : Int) -> Array[Float] {
  let n = x.length()
  let out : Array[Float] = Array::make(max_lag + 1, 0.0F)
  if n == 0 {
    return out
  }
  let mu = ts_mean(x)
  // Lag-0 variance (denominator for all lags).
  let mut denom = 0.0F
  for i in 0..<n {
    let d = x[i] - mu
    denom = denom + d * d
  }
  if denom == 0.0F {
    // Constant series — every lag is perfectly correlated. Fill 1s.
    for k in 0..<(max_lag + 1) {
      out[k] = 1.0F
    }
    return out
  }
  // Lag 0 = 1.
  out[0] = 1.0F
  for k in 1..<(max_lag + 1) {
    let mut num = 0.0F
    for i in 0..<(n - k) {
      num = num + (x[i] - mu) * (x[i + k] - mu)
    }
    out[k] = num / denom
  }
  out
}

///|
/// Partial autocorrelation function (PACF) at lags `0..max_lag`.
/// Computed via the Levinson-Durbin recursion on top of `ts_acf`.
/// Returns an array of length `max_lag + 1` where `out[0] = 1`. The
/// PACF value at lag `k` is the correlation between `x[t]` and
/// `x[t-k]` after removing the linear effect of the intermediate
/// `x[t-1], ..., x[t-k+1]`. For a pure AR(p) process the PACF cuts
/// off after lag `p`, which is how ARIMA(p, 'p') selects its order.
/// For a constant series (zero variance), every PACF lag is 1.0
/// (matching the all-1 behaviour of ACF on the same edge case).
pub fn ts_pacf(x : Array[Float], max_lag : Int) -> Array[Float] {
  let r = ts_acf(x, max_lag)
  let pacf_out : Array[Float] = Array::make(max_lag + 1, 0.0F)
  // Detect constant series: ACF is all 1s and the Levinson-Durbin
  // recursion degenerates to 0/0 at lag ≥ 2. Short-circuit to 1.
  let all_one = if max_lag >= 2 {
    r[1] >= 1.0F - 1.0e-6F && r[2] >= 1.0F - 1.0e-6F
  } else if max_lag == 1 {
    r[1] >= 1.0F - 1.0e-6F
  } else {
    false
  }
  if all_one {
    for k in 0..<(max_lag + 1) {
      pacf_out[k] = 1.0F
    }
    return pacf_out
  }
  if max_lag >= 1 {
    pacf_out[0] = 1.0F
    pacf_out[1] = r[1]
  } else {
    pacf_out[0] = 1.0F
    return pacf_out
  }
  // Levinson-Durbin recursion. `phi_prev` holds φ[k-1] for j = 1..k-1
  // (we use 0-indexed slots; phi_prev[0] is unused).
  let mut phi_prev : Array[Float] = Array::make(2, 0.0F)
  phi_prev[1] = r[1]
  for k in 2..<(max_lag + 1) {
    // Numerator: ρ(k) - Σ_{j=1..k-1} φ[k-1][j] · ρ(k-j)
    let mut num = r[k]
    let mut den = 1.0F
    for j in 1..<k {
      num = num - phi_prev[j] * r[k - j]
      den = den - phi_prev[j] * r[j]
    }
    let phi_kk = if den.abs() < 1.0e-12F { 0.0F } else { num / den }
    // φ[k][j] = φ[k-1][j] - φ[k][k] · φ[k-1][k-j]   for j = 1..k-1
    let phi_curr : Array[Float] = Array::make(k + 1, 0.0F)
    phi_curr[k] = phi_kk
    for j in 1..<k {
      phi_curr[j] = phi_prev[j] - phi_kk * phi_prev[k - j]
    }
    pacf_out[k] = phi_kk
    phi_prev = phi_curr
  }
  pacf_out
}