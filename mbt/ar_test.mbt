// ar_test.mbt — Tests for AR(p) autoregressive model.

///|
test "ar_fitted: AR(1) with phi=0.7 reproduces 0.7 * y[t-1]" {
  // y = [10, 17, 21.9, ...] — AR(1) with phi=0.7, intercept=0.
  let y : Array[Float] = [10.0F, 17.0F, 21.9F, 25.33F]
  let param = ArParam::new(1, 0.0F, [0.7F])
  let fitted = ar_fitted(y, param)
  assert_eq(fitted.length(), 3)
  // ŷ[1] = 0.7 * y[0] = 7.0
  if (fitted[0] - 7.0F).abs() > 0.001F {
    fail("ŷ[1] = \{fitted[0]}, expected 7.0")
  }
  // ŷ[2] = 0.7 * y[1] = 11.9
  if (fitted[1] - 11.9F).abs() > 0.001F {
    fail("ŷ[2] = \{fitted[1]}, expected 11.9")
  }
}

///|
test "ar_fitted: AR(2) with phi_1=0.5, phi_2=0.3, intercept=0" {
  // ŷ[t] = 0.5 · y[t-1] + 0.3 · y[t-2]
  let y : Array[Float] = [1.0F, 2.0F, 4.0F, 8.0F, 16.0F]
  let param = ArParam::new(2, 0.0F, [0.5F, 0.3F])
  let fitted = ar_fitted(y, param)
  // fitted[0] = 0.5 * y[1] + 0.3 * y[0] = 0.5 * 2 + 0.3 * 1 = 1.3
  // (source t = 2)
  if (fitted[0] - 1.3F).abs() > 0.001F {
    fail("ŷ[2] = \{fitted[0]}, expected 1.3")
  }
  // fitted[1] = 0.5 * y[2] + 0.3 * y[1] = 2 + 0.6 = 2.6
  if (fitted[1] - 2.6F).abs() > 0.001F {
    fail("ŷ[3] = \{fitted[1]}, expected 2.6")
  }
}

///|
test "ar_residuals: zero for a true AR(1) generated series" {
  // y[t] = 0.7 · y[t-1]; generate and check residuals ≈ 0.
  let y : Array[Float] = Array::make(20, 0.0F)
  y[0] = 1.0F
  for t in 1..<20 {
    y[t] = 0.7F * y[t - 1]
  }
  let param = ArParam::new(1, 0.0F, [0.7F])
  let eps = ar_residuals(y, param)
  // All residuals should be 0 (since y[t] is exactly 0.7 * y[t-1]).
  let max_eps : Float = {
    let mut m = 0.0F
    for i in 0..<eps.length() {
      if eps[i].abs() > m {
        m = eps[i].abs()
      }
    }
    m
  }
  if max_eps > 0.001F {
    fail("max |residual| = \{max_eps}, expected ≈ 0")
  }
}

///|
test "ar_residual_loss: zero for true AR process" {
  let y : Array[Float] = Array::make(50, 0.0F)
  y[0] = 1.0F
  for t in 1..<50 {
    y[t] = 0.8F * y[t - 1]
  }
  let param = ArParam::new(1, 0.0F, [0.8F])
  if ar_residual_loss(y, param) > 0.001F {
    fail("loss should be 0 for true AR(1)")
  }
}

///|
test "ar_grad_coeffs: zero for true AR process (residual loss at min)" {
  let y : Array[Float] = Array::make(30, 0.0F)
  y[0] = 1.0F
  for t in 1..<30 {
    y[t] = 0.6F * y[t - 1]
  }
  let param = ArParam::new(1, 0.0F, [0.6F])
  let g = ar_grad_coeffs(y, param)
  // At the true coefficient, the gradient should be ≈ 0.
  if g[0].abs() > 0.01F {
    fail("∂loss/∂φ = \{g[0]}, expected ≈ 0 at true φ")
  }
}

///|
test "ar_grad_coeffs: sign matches expected direction off the optimum" {
  // ∂loss/∂φ = -2 Σ ε · y[t-1]. If phi is too small (under-fit),
  // ε[t] ≈ (true_phi - phi) · y[t-1] > 0, so ∂loss/∂φ is negative
  // (gradient descent moves phi UP — toward the optimum).
  let y : Array[Float] = Array::make(50, 0.0F)
  y[0] = 1.0F
  for t in 1..<50 {
    y[t] = 0.7F * y[t - 1]
  }
  let param = ArParam::new(1, 0.0F, [0.5F]) // phi = 0.5 < true = 0.7
  let g = ar_grad_coeffs(y, param)
  if g[0] >= 0.0F {
    fail("∂loss/∂φ = \{g[0]}, expected negative (under-fit → push phi up)")
  }
  // If phi is too large (over-fit), gradient is positive
  // (gradient descent moves phi DOWN — toward the optimum).
  let param2 = ArParam::new(1, 0.0F, [0.9F]) // phi = 0.9 > true
  let g2 = ar_grad_coeffs(y, param2)
  if g2[0] <= 0.0F {
    fail("∂loss/∂φ = \{g2[0]}, expected positive (over-fit → push phi down)")
  }
}

///|
test "ar_fit_yule_walker: recovers phi = 0.7 from synthetic AR(1)" {
  // Generate y[t] = 0.7 · y[t-1] + noise. Long series, so Yule-Walker
  // should be very close to 0.7.
  let rng = Xoshiro::from_state(11UL, 12UL, 13UL, 14UL)
  let n = 2000
  let y : Array[Float] = Array::make(n, 0.0F)
  y[0] = 0.0F
  for t in 1..<n {
    let (z, _) = box_muller(rng)
    let eps = Float::from_double(z) * 0.5F // noise
    y[t] = 0.7F * y[t - 1] + eps
  }
  let param = ar_fit_yule_walker(y, 1)
  // phi_1 should be near 0.7 (within ±0.05 for n=2000).
  if (param.coeffs[0] - 0.7F).abs() > 0.05F {
    fail("phi_1 = \{param.coeffs[0]}, expected ≈ 0.7")
  }
}

///|
test "ar_fit_ols: recovers phi = 0.7 from synthetic AR(1)" {
  let rng = Xoshiro::from_state(21UL, 22UL, 23UL, 24UL)
  let n = 2000
  let y : Array[Float] = Array::make(n, 0.0F)
  y[0] = 0.0F
  for t in 1..<n {
    let (z, _) = box_muller(rng)
    let eps = Float::from_double(z) * 0.5F
    y[t] = 0.7F * y[t - 1] + eps
  }
  let param = ar_fit_ols(y, 1)
  if (param.coeffs[0] - 0.7F).abs() > 0.05F {
    fail("phi_1 = \{param.coeffs[0]}, expected ≈ 0.7")
  }
  // Intercept should be near 0 (no constant term in our generator).
  if param.intercept.abs() > 0.5F {
    fail("intercept = \{param.intercept}, expected ≈ 0")
  }
}

///|
test "ar_fit_ols: recovers AR(2) phi_1, phi_2 from synthetic series" {
  // y[t] = 0.5 * y[t-1] + 0.3 * y[t-2] + noise.
  let rng = Xoshiro::from_state(31UL, 32UL, 33UL, 34UL)
  let n = 3000
  let y : Array[Float] = Array::make(n, 0.0F)
  y[0] = 0.0F
  y[1] = 0.0F
  for t in 2..<n {
    let (z, _) = box_muller(rng)
    let eps = Float::from_double(z) * 0.5F
    y[t] = 0.5F * y[t - 1] + 0.3F * y[t - 2] + eps
  }
  let param = ar_fit_ols(y, 2)
  if (param.coeffs[0] - 0.5F).abs() > 0.05F {
    fail("phi_1 = \{param.coeffs[0]}, expected ≈ 0.5")
  }
  if (param.coeffs[1] - 0.3F).abs() > 0.05F {
    fail("phi_2 = \{param.coeffs[1]}, expected ≈ 0.3")
  }
}

///|
test "solve_linear: trivial 2×2 system x + y = 3, 2x - y = 0 → x=1, y=2" {
  let a : Array[Array[Float]] = [[1.0F, 1.0F], [2.0F, -1.0F]]
  let b : Array[Float] = [3.0F, 0.0F]
  let x = solve_linear(a, b)
  if (x[0] - 1.0F).abs() > 1.0e-4F {
    fail("x[0] = \{x[0]}, expected 1.0")
  }
  if (x[1] - 2.0F).abs() > 1.0e-4F {
    fail("x[1] = \{x[1]}, expected 2.0")
  }
}

///|
test "solve_linear: 3×3 system (random coefficients)" {
  // A [3, 2, -1; 2, -2, 4; 1, 0.5, -1] x = [1, -2, 0]
  let a : Array[Array[Float]] = [
    [3.0F, 2.0F, -1.0F],
    [2.0F, -2.0F, 4.0F],
    [1.0F, 0.5F, -1.0F],
  ]
  let b : Array[Float] = [1.0F, -2.0F, 0.0F]
  let x = solve_linear(a, b)
  // Verify A x ≈ b
  for i in 0..<3 {
    let mut row = 0.0F
    for j in 0..<3 {
      row = row + a[i][j] * x[j]
    }
    if (row - b[i]).abs() > 1.0e-3F {
      fail("A·x[\{i}] = \{row}, expected \{b[i]}")
    }
  }
}

///|
test "ar: empty / too-short series handled gracefully" {
  let empty : Array[Float] = []
  let p = ArParam::new(1, 0.0F, [0.5F])
  assert_eq(ar_fitted(empty, p).length(), 0)
  assert_eq(ar_residuals(empty, p).length(), 0)
  // Single-element series (n <= p) → no fitted/residuals
  let one : Array[Float] = [5.0F]
  assert_eq(ar_fitted(one, p).length(), 0)
  // ar_fit_ols on too-short series returns zero-coeff param
  let too_short : Array[Float] = [1.0F, 2.0F]
  let fitted = ar_fit_ols(too_short, 1)
  assert_eq(fitted.coeffs[0], 0.0F)
}