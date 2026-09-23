// ma.mbt — Moving Average MA(q) model (v0.28.3).
//
// MA(q) model:
//   y[t] = μ + ε[t] + θ₁·ε[t-1] + θ₂·ε[t-2] + ... + θ_q·ε[t-q]
// where μ is the intercept, ε[t] is white noise (zero-mean innovations),
// and θᵢ are the moving-average coefficients.
//
// The innovations ε[t] are not observed. Given the data y and the
// coefficients θ, residuals are computed recursively:
//
//   ε[t] = y[t] - μ - θ₁·ε[t-1] - ... - θ_q·ε[t-q]
//
// with the initial residuals initialised to `y[t] - μ` for t < 0
// (treating pre-sample residuals as observed deviations from the mean).
//
// This module provides:
//   - `MaParam` struct (q-order parameters)
//   - `ma_residuals`     — recursive residual computation
//   - `ma_residual_loss` — Σ ε[t]²
//   - `ma_grad_thetas`   — hand-derived ∂loss/∂θⱼ (length q)
//   - `ma_fit_css`       — iterative fit via conditional sum-of-squares
//
// The gradient can also be derived via the Tape autodiff (v0.31.x);
// `ma_grad_thetas` is a hand-coded version for cases where Tape is
// not desired (e.g., to keep MA self-contained without the autodiff
// dependency).

///|
/// MA(q) parameter bundle: intercept + q moving-average coefficients.
/// `thetas[i]` corresponds to lag-(i+1) coefficient θᵢ₊₁.
pub struct MaParam {
  q : Int
  intercept : Float
  thetas : Array[Float]
}

///|
/// Build an MA(q) parameter from explicit coefficients. `thetas` is
/// expected to have length `q`; the first entry lags ε[t-1].
pub fn MaParam::new(
  q : Int,
  intercept : Float,
  thetas : Array[Float],
) -> MaParam {
  { q, intercept, thetas }
}

///|
/// Compute innovations ε[t] for t = 0, 1, ..., n-1 using the MA(q)
/// recursion. Pre-sample residuals (t < 0) are initialised to 0,
/// which is the standard "no pre-sample data" assumption used in
/// most textbook implementations.
pub fn ma_residuals(y : Array[Float], param : MaParam) -> Array[Float] {
  let n = y.length()
  let eps : Array[Float] = Array::make(n, 0.0F)
  if n == 0 {
    return eps
  }
  // Forward sweep.
  for t in 0..<n {
    let mut e = y[t] - param.intercept
    for j in 0..<param.q {
      let lag = t - 1 - j
      if lag >= 0 {
        e = e - param.thetas[j] * eps[lag]
      }
      // For lag < 0: pre-sample residual is 0 (no pre-sample data).
    }
    eps[t] = e
  }
  eps
}

///|
/// Sum of squared residuals Σₜ ε[t]² — the MA(q) fitting objective
/// (a.k.a. conditional sum-of-squares).
pub fn ma_residual_loss(y : Array[Float], param : MaParam) -> Float {
  let eps = ma_residuals(y, param)
  let mut loss = 0.0F
  for i in 0..<eps.length() {
    loss = loss + eps[i] * eps[i]
  }
  loss
}

///|
/// Gradient ∂loss/∂θⱼ for j = 0..q-1 (length q). The MA recursion
/// couples every θⱼ to every residual, so we have to track partial
/// derivatives of all residuals w.r.t. all parameters jointly:
///
///   ∂ε[t]/∂θⱼ = -Σ_{k=0..q-1} θₖ · ∂ε[t-1-k]/∂θⱼ - ε[t-1-j]
///            = -Σ_k θₖ · de[t-1-k] - ε[t-1-j]
///
/// Initialised at t = 0: ∂ε[0]/∂θⱼ = 0 (no recursion yet). Then
/// propagated forward; gradient accumulated as ∂loss/∂θⱼ = 2 Σₜ ε[t]
/// · ∂ε[t]/∂θⱼ.
pub fn ma_grad_thetas(
  y : Array[Float],
  param : MaParam,
) -> Array[Float] {
  let n = y.length()
  let grad : Array[Float] = Array::make(param.q, 0.0F)
  if n == 0 || param.q == 0 {
    return grad
  }
  // Partial derivatives of residuals w.r.t. each θ.
  let de : Array[Array[Float]] = []
  for _i in 0..<n {
    de.push(Array::make(param.q, 0.0F))
  }
  let eps = ma_residuals(y, param)
  for t in 0..<n {
    for j in 0..<param.q {
      // ∂ε[t]/∂θⱼ = -ε[t-1-j] - Σ_k θₖ · ∂ε[t-1-k]/∂θⱼ
      // For t=0: ∂ε[0]/∂θⱼ = 0 (initialise all rows of de to 0).
      let mut sum = 0.0F
      if t > 0 {
        for k in 0..<param.q {
          let idx = t - 1 - k
          if idx >= 0 {
            sum = sum + param.thetas[k] * de[idx][j]
          }
        }
        let lag = t - 1 - j
        if lag >= 0 {
          sum = sum + eps[lag]
        }
      }
      de[t][j] = -sum
      grad[j] = grad[j] + 2.0F * eps[t] * de[t][j]
    }
  }
  grad
}

///|
/// Fit MA(q) by gradient descent on the conditional sum-of-squares
/// loss. Returns the converged `MaParam`. Simple fixed-step Adam
/// without momentum — adequate for short series / small q.
pub fn ma_fit(
  y : Array[Float],
  q : Int,
  max_iter : Int,
  lr : Float,
) -> MaParam {
  let intercept = ts_mean(y)
  let thetas : Array[Float] = Array::make(q, 0.0F)
  let mut param = MaParam::new(q, intercept, thetas)
  for _iter in 0..<max_iter {
    let g = ma_grad_thetas(y, param)
    let new_thetas : Array[Float] = Array::make(q, 0.0F)
    for j in 0..<q {
      new_thetas[j] = param.thetas[j] - lr * g[j]
    }
    param = MaParam::new(q, intercept, new_thetas)
  }
  param
}

///|
/// Forward-only MA(q) loss used for finite-difference validation of
/// `ma_grad_thetas`. Returns the conditional sum-of-squares loss for
/// the given `thetas` (length q) and intercept.
pub fn ma_loss_value(
  y : Array[Float],
  intercept : Float,
  thetas : Array[Float],
) -> Float {
  let param = MaParam::new(thetas.length(), intercept, thetas)
  ma_residual_loss(y, param)
}