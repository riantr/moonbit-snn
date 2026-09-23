// ar.mbt — Autoregressive AR(p) model (v0.28.2).
//
// AR(p) model:
//   y[t] = c + φ₁·y[t-1] + φ₂·y[t-2] + ... + φₚ·y[t-p] + ε[t]
// where c is the intercept, φᵢ are the autoregressive coefficients,
// and ε[t] is white noise.
//
// This module provides:
//   - `ArParam` struct holding the p-order parameters
//   - `ar_fitted`     — fitted values ŷ[t] for t ≥ p
//   - `ar_residuals`  — ε[t] = y[t] - ŷ[t]
//   - `ar_residual_loss` — Σ ε[t]² (sum of squared residuals)
//   - `ar_fit_yule_walker` — fit via Yule-Walker (PACF[1..p])
//   - `ar_fit_ols`     — fit via OLS normal equations (Gauss-Jordan)
//   - `ar_grad_coeffs` — ∂loss/∂φᵢ (coefficient gradient, hand-derived)
//
// Two complementary fitting methods are provided: Yule-Walker gives a
// closed-form solution exploiting the autocorrelation structure; OLS
// minimises the sum of squared residuals directly. For long series
// they agree asymptotically; for short series OLS is generally more
// accurate.

///|
/// AR(p) parameter bundle: p-order intercept + p coefficients.
/// `coeffs[i]` corresponds to the lag-(i+1) coefficient φᵢ₊₁.
pub struct ArParam {
  p : Int
  intercept : Float
  coeffs : Array[Float]
}

///|
/// Build an AR(p) parameter from explicit coefficients. `coeffs` is
/// expected to have length `p`; the first coefficient lags y[t-1].
pub fn ArParam::new(
  p : Int,
  intercept : Float,
  coeffs : Array[Float],
) -> ArParam {
  { p, intercept, coeffs }
}

///|
/// Fitted values ŷ[t] = c + Σᵢ φᵢ · y[t-i] for t = p, p+1, ..., n-1.
/// Returns an array of length `n - p` whose index k corresponds to
/// source time `t = p + k`.
pub fn ar_fitted(y : Array[Float], param : ArParam) -> Array[Float] {
  let n = y.length()
  if n <= param.p {
    return Array::make(0, 0.0F)
  }
  let out : Array[Float] = Array::make(n - param.p, 0.0F)
  for t in param.p..<n {
    let mut fitted = param.intercept
    for i in 0..<param.p {
      fitted = fitted + param.coeffs[i] * y[t - 1 - i]
    }
    out[t - param.p] = fitted
  }
  out
}

///|
/// Residuals ε[t] = y[t] - ŷ[t] for t ≥ p. Length `n - p`.
pub fn ar_residuals(y : Array[Float], param : ArParam) -> Array[Float] {
  let n = y.length()
  if n <= param.p {
    return Array::make(0, 0.0F)
  }
  let fitted = ar_fitted(y, param)
  let eps : Array[Float] = Array::make(n - param.p, 0.0F)
  for t in param.p..<n {
    eps[t - param.p] = y[t] - fitted[t - param.p]
  }
  eps
}

///|
/// Sum of squared residuals Σₜ ε[t]² — the AR(p) fitting objective.
pub fn ar_residual_loss(y : Array[Float], param : ArParam) -> Float {
  let eps = ar_residuals(y, param)
  let mut loss = 0.0F
  for i in 0..<eps.length() {
    loss = loss + eps[i] * eps[i]
  }
  loss
}

///|
/// Gradient of the residual loss w.r.t. each coefficient φᵢ (length
/// `p`). From ∂/∂φᵢ Σₜ ε[t]² = -2 Σₜ ε[t] · y[t-i]. Used for sanity
/// checking the OLS / Yule-Walker fits or for gradient-based fitting.
pub fn ar_grad_coeffs(
  y : Array[Float],
  param : ArParam,
) -> Array[Float] {
  let n = y.length()
  let grad : Array[Float] = Array::make(param.p, 0.0F)
  if n <= param.p {
    return grad
  }
  let eps = ar_residuals(y, param)
  let n_eps = eps.length()
  for t in param.p..<n {
    let e = eps[t - param.p]
    for i in 0..<param.p {
      grad[i] = grad[i] - 2.0F * e * y[t - 1 - i]
    }
  }
  // Defensive: ignore n_eps (length check already done by if)
  let _ = n_eps
  grad
}

///|
/// Fit AR(p) via the Yule-Walker equations. De-means the series,
/// computes PACF[1..p], and uses those as the AR coefficients. The
/// intercept is set to the series mean.
pub fn ar_fit_yule_walker(y : Array[Float], p : Int) -> ArParam {
  let n = y.length()
  if n == 0 || p == 0 {
    return ArParam::new(p, ts_mean(y), Array::make(p, 0.0F))
  }
  let mu = ts_mean(y)
  // De-mean
  let yc : Array[Float] = Array::make(n, 0.0F)
  for i in 0..<n {
    yc[i] = y[i] - mu
  }
  // PACF of de-meaned series
  let pacf = ts_pacf(yc, p)
  // coeffs[i] = PACF[i+1] for i in 0..p
  let coeffs : Array[Float] = Array::make(p, 0.0F)
  for i in 0..<p {
    coeffs[i] = pacf[i + 1]
  }
  ArParam::new(p, mu, coeffs)
}

///|
/// Fit AR(p) via OLS normal equations. Solves XᵀX · θ = Xᵀy via
/// Gauss-Jordan elimination, where the design matrix X has columns
/// [y[t-1], ..., y[t-p], 1] (last column = intercept).
pub fn ar_fit_ols(y : Array[Float], p : Int) -> ArParam {
  let n = y.length()
  if n <= p {
    return ArParam::new(p, 0.0F, Array::make(p, 0.0F))
  }
  let dim = p + 1
  // XᵀX and Xᵀy. Each row must be a separate allocation — MoonBit's
  // `Array::make(dim, init)` shares the same `init` value across rows
  // for mutable inner arrays, which would corrupt our accumulation.
  let xtx : Array[Array[Float]] = []
  for _i in 0..<dim {
    xtx.push(Array::make(dim, 0.0F))
  }
  let xty : Array[Float] = Array::make(dim, 0.0F)
  for t in p..<n {
    let xti : Array[Float] = Array::make(dim, 0.0F)
    for i in 0..<p {
      xti[i] = y[t - 1 - i]
    }
    xti[p] = 1.0F
    for i in 0..<dim {
      for j in 0..<dim {
        xtx[i][j] = xtx[i][j] + xti[i] * xti[j]
      }
      xty[i] = xty[i] + xti[i] * y[t]
    }
  }
  let theta = solve_linear(xtx, xty)
  // theta[p] = intercept, theta[0..p] = coefficients
  let coeffs : Array[Float] = Array::make(p, 0.0F)
  for i in 0..<p {
    coeffs[i] = theta[i]
  }
  ArParam::new(p, theta[p], coeffs)
}

///|
/// Solve a linear system A · x = b via Gauss-Jordan elimination with
/// partial pivoting. `a` is dim × dim, `b` is dim. Returns x. If A
/// is singular (no pivot available), returns a zero vector.
pub fn solve_linear(
  a : Array[Array[Float]],
  b : Array[Float],
) -> Array[Float] {
  let dim = a.length()
  // Augmented matrix [A | b]. Each row is allocated separately to
  // avoid MoonBit's `Array::make(dim, init)` shared-row gotcha for
  // mutable inner arrays.
  let aug : Array[Array[Float]] = []
  for _i in 0..<dim {
    aug.push(Array::make(dim + 1, 0.0F))
  }
  for i in 0..<dim {
    for j in 0..<dim {
      aug[i][j] = a[i][j]
    }
    aug[i][dim] = b[i]
  }
  // Forward elimination with partial pivoting
  for k in 0..<dim {
    // Find pivot row
    let mut pivot_row = k
    let mut max_val = aug[k][k].abs()
    for i in (k + 1)..<dim {
      if aug[i][k].abs() > max_val {
        max_val = aug[i][k].abs()
        pivot_row = i
      }
    }
    // Swap rows
    if pivot_row != k {
      for j in 0..<(dim + 1) {
        let tmp = aug[k][j]
        aug[k][j] = aug[pivot_row][j]
        aug[pivot_row][j] = tmp
      }
    }
    let pivot = aug[k][k]
    if pivot.abs() < 1.0e-12F {
      // Singular; bail out with zeros.
      return Array::make(dim, 0.0F)
    }
    // Eliminate below
    for i in (k + 1)..<dim {
      let factor = aug[i][k] / pivot
      if factor != 0.0F {
        for j in k..<(dim + 1) {
          aug[i][j] = aug[i][j] - factor * aug[k][j]
        }
      }
    }
  }
  // Back substitution
  let x : Array[Float] = Array::make(dim, 0.0F)
  let mut k = dim - 1
  while k >= 0 {
    let mut sum = aug[k][dim]
    let mut j = k + 1
    while j < dim {
      sum = sum - aug[k][j] * x[j]
      j = j + 1
    }
    x[k] = sum / aug[k][k]
    k = k - 1
  }
  x
}