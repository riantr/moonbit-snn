// arma.mbt — Autoregressive Moving-Average ARMA(p, q) model (v0.28.4).
//
// ARMA(p, q) model:
//   y[t] = c + Σ_{i=1..p} φᵢ · y[t-i] + ε[t] + Σ_{j=1..q} θⱼ · ε[t-j]
//
// Rearranged as a residual recursion:
//   ε[t] = y[t] - c - Σᵢ φᵢ · y[t-i] - Σⱼ θⱼ · ε[t-j]
//
// Pre-sample residuals (t < 0) and y values (t < 0) are taken as 0 —
// the standard "no pre-sample data" assumption.
//
// Components:
//   - `ArmaParam` struct (p, q, intercept, ar_coeffs, ma_thetas)
//   - `arma_residuals`     — recursive residual computation
//   - `arma_residual_loss` — Σ ε[t]²
//   - `arma_grad`          — ∂loss/∂φᵢ and ∂loss/∂θⱼ (hand-derived)
//   - `arma_fit`           — joint gradient-descent fit

///|
/// ARMA(p, q) parameter bundle.
pub struct ArmaParam {
  p : Int
  q : Int
  intercept : Float
  ar_coeffs : Array[Float]   // length p: ar_coeffs[i] = phi_{i+1}
  ma_thetas : Array[Float]   // length q: ma_thetas[j] = theta_{j+1}
}

///|
/// Build an ARMA(p, q) parameter from explicit coefficients. Both
/// `ar_coeffs` and `ma_thetas` may be empty (then the corresponding
/// component is omitted from the model).
pub fn ArmaParam::new(
  p : Int,
  q : Int,
  intercept : Float,
  ar_coeffs : Array[Float],
  ma_thetas : Array[Float],
) -> ArmaParam {
  { p, q, intercept, ar_coeffs, ma_thetas }
}

///|
/// Compute innovations ε[t] for t = 0, 1, ..., n-1 using the ARMA
/// recursion. Pre-sample y values and residuals are taken as 0.
pub fn arma_residuals(
  y : Array[Float],
  param : ArmaParam,
) -> Array[Float] {
  let n = y.length()
  let eps : Array[Float] = Array::make(n, 0.0F)
  if n == 0 {
    return eps
  }
  for t in 0..<n {
    let mut e = y[t] - param.intercept
    // AR part: Σᵢ φᵢ · y[t-i]
    for i in 0..<param.p {
      let lag = t - 1 - i
      if lag >= 0 {
        e = e - param.ar_coeffs[i] * y[lag]
      }
    }
    // MA part: Σⱼ θⱼ · ε[t-1-j]
    for j in 0..<param.q {
      let lag = t - 1 - j
      if lag >= 0 {
        e = e - param.ma_thetas[j] * eps[lag]
      }
    }
    eps[t] = e
  }
  eps
}

///|
/// Sum of squared residuals Σ ε[t]² — the ARMA fitting objective.
pub fn arma_residual_loss(
  y : Array[Float],
  param : ArmaParam,
) -> Float {
  let eps = arma_residuals(y, param)
  let mut loss = 0.0F
  for i in 0..<eps.length() {
    loss = loss + eps[i] * eps[i]
  }
  loss
}

///|
/// Gradient of the residual loss w.r.t. both AR and MA coefficients.
/// Returns `(ar_grad, ma_grad)` as a tuple. Lengths match `ar_coeffs`
/// and `ma_thetas`.
//
// The recursion is derived by differentiating the residual recursion:
//   ∂ε[t]/∂φᵢ = -y[t-i] - Σⱼ θⱼ · ∂ε[t-1-j]/∂φᵢ
//   ∂ε[t]/∂θⱼ = -ε[t-1-j] - Σⱼ θⱼ · ∂ε[t-1-j]/∂θⱼ
// (No φₖ term because the recursion uses lagged ε, not current ε.)
// Then ∂loss/∂φᵢ = 2 Σₜ ε[t] · ∂ε[t]/∂φᵢ, etc.
pub fn arma_grad(
  y : Array[Float],
  param : ArmaParam,
) -> (Array[Float], Array[Float]) {
  let n = y.length()
  let ar_grad : Array[Float] = Array::make(param.p, 0.0F)
  let ma_grad : Array[Float] = Array::make(param.q, 0.0F)
  if n == 0 {
    return (ar_grad, ma_grad)
  }
  // Partial derivatives of residuals w.r.t. each parameter.
  // de_ar[t][i] = ∂ε[t]/∂φᵢ
  // de_ma[t][j] = ∂ε[t]/∂θⱼ
  // Allocate outer arrays first, then fill rows — avoids the
  // shared-row gotcha for nested mutable arrays.
  let de_ar : Array[Array[Float]] = Array::make(n, [])
  let de_ma : Array[Array[Float]] = Array::make(n, [])
  for t in 0..<n {
    de_ar[t] = Array::make(param.p, 0.0F)
    de_ma[t] = Array::make(param.q, 0.0F)
  }
  let eps = arma_residuals(y, param)
  for t in 0..<n {
    // ∂ε[t]/∂φᵢ
    for i in 0..<param.p {
      let mut sum = 0.0F
      // -y[t-i]
      let lag_y = t - 1 - i
      if lag_y >= 0 {
        sum = sum + y[lag_y]
      }
      // -Σⱼ θⱼ · ∂ε[t-1-j]/∂φᵢ
      for j in 0..<param.q {
        let lag_eps = t - 1 - j
        if lag_eps >= 0 {
          sum = sum + param.ma_thetas[j] * de_ar[lag_eps][i]
        }
      }
      de_ar[t][i] = -sum
      ar_grad[i] = ar_grad[i] + 2.0F * eps[t] * de_ar[t][i]
    }
    // ∂ε[t]/∂θⱼ
    for j in 0..<param.q {
      let mut sum = 0.0F
      // -ε[t-1-j]
      let lag_eps = t - 1 - j
      if lag_eps >= 0 {
        sum = sum + eps[lag_eps]
      }
      // -Σⱼ θⱼ · ∂ε[t-1-j]/∂θⱼ
      for k in 0..<param.q {
        let lag_eps2 = t - 1 - k
        if lag_eps2 >= 0 {
          sum = sum + param.ma_thetas[k] * de_ma[lag_eps2][j]
        }
      }
      de_ma[t][j] = -sum
      ma_grad[j] = ma_grad[j] + 2.0F * eps[t] * de_ma[t][j]
    }
  }
  (ar_grad, ma_grad)
}

///|
/// Fit ARMA(p, q) by gradient descent on the residual loss. Returns
/// the converged `ArmaParam`. Simple fixed-step Adam without momentum.
pub fn arma_fit(
  y : Array[Float],
  p : Int,
  q : Int,
  max_iter : Int,
  lr : Float,
) -> ArmaParam {
  let intercept = ts_mean(y)
  let ar_coeffs : Array[Float] = Array::make(p, 0.0F)
  let ma_thetas : Array[Float] = Array::make(q, 0.0F)
  let mut param = ArmaParam::new(p, q, intercept, ar_coeffs, ma_thetas)
  for _iter in 0..<max_iter {
    let (g_ar, g_ma) = arma_grad(y, param)
    let new_ar : Array[Float] = Array::make(p, 0.0F)
    let new_ma : Array[Float] = Array::make(q, 0.0F)
    for i in 0..<p {
      new_ar[i] = param.ar_coeffs[i] - lr * g_ar[i]
    }
    for j in 0..<q {
      new_ma[j] = param.ma_thetas[j] - lr * g_ma[j]
    }
    param = ArmaParam::new(p, q, intercept, new_ar, new_ma)
  }
  param
}

///|
/// Forward-only ARMA loss used for finite-difference validation of
/// `arma_grad`. Returns the residual loss for the given parameters.
pub fn arma_loss_value(
  y : Array[Float],
  intercept : Float,
  ar_coeffs : Array[Float],
  ma_thetas : Array[Float],
) -> Float {
  let param = ArmaParam::new(
    ar_coeffs.length(),
    ma_thetas.length(),
    intercept,
    ar_coeffs,
    ma_thetas,
  )
  arma_residual_loss(y, param)
}