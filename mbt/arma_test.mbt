// arma_test.mbt — Tests for ARMA(p, q) autoregressive moving-average.

///|
test "arma_residuals: pure AR(1) degenerates to AR(p) residuals" {
  // ARMA(1, 0) should match AR(1) exactly. With phi=0.7, theta=[]:
  // ε[t] = y[t] - 0.7 · y[t-1].
  let y : Array[Float] = [10.0F, 17.0F, 21.9F, 25.33F, 27.731F]
  let param = ArmaParam::new(1, 0, 0.0F, [0.7F], [])
  let eps = arma_residuals(y, param)
  // ε[0] = 10
  if (eps[0] - 10.0F).abs() > 0.001F {
    fail("ε[0] = \{eps[0]}, expected 10.0")
  }
  // ε[1] = 17 - 0.7 * 10 = 10
  if (eps[1] - 10.0F).abs() > 0.001F {
    fail("ε[1] = \{eps[1]}, expected 10.0")
  }
  // ε[2] = 21.9 - 0.7 * 17 = 10
  if (eps[2] - 10.0F).abs() > 0.001F {
    fail("ε[2] = \{eps[2]}, expected 10.0")
  }
}

///|
test "arma_residuals: pure MA(1) degenerates to MA(q) residuals" {
  // ARMA(0, 1) with theta=0.5 should match MA(1): ε[t] = y[t] - 0.5·ε[t-1].
  let y : Array[Float] = [1.0F, 2.0F, 3.0F, 4.0F]
  let param = ArmaParam::new(0, 1, 0.0F, [], [0.5F])
  let eps = arma_residuals(y, param)
  // ε[0] = 1
  if (eps[0] - 1.0F).abs() > 0.001F {
    fail("ε[0] = \{eps[0]}, expected 1.0")
  }
  // ε[1] = 2 - 0.5 * 1 = 1.5
  if (eps[1] - 1.5F).abs() > 0.001F {
    fail("ε[1] = \{eps[1]}, expected 1.5")
  }
  // ε[2] = 3 - 0.5 * 1.5 = 2.25
  if (eps[2] - 2.25F).abs() > 0.001F {
    fail("ε[2] = \{eps[2]}, expected 2.25")
  }
}

///|
test "arma_residuals: ARMA(1, 1) recursion" {
  // ε[t] = y[t] - 0.5·y[t-1] - 0.3·ε[t-1]
  // y = [10, 17, ...], phi=0.5, theta=0.3
  // ε[0] = 10
  // ε[1] = 17 - 0.5*10 - 0.3*10 = 17 - 5 - 3 = 9
  let y : Array[Float] = [10.0F, 17.0F, 21.0F, 25.0F]
  let param = ArmaParam::new(1, 1, 0.0F, [0.5F], [0.3F])
  let eps = arma_residuals(y, param)
  if (eps[0] - 10.0F).abs() > 0.001F {
    fail("ε[0] = \{eps[0]}, expected 10.0")
  }
  if (eps[1] - 9.0F).abs() > 0.001F {
    fail("ε[1] = \{eps[1]}, expected 9.0")
  }
}

///|
test "arma_residual_loss: zero for ARMA(1, 1) generated data" {
  // y[t] = 0.5·y[t-1] + ε[t] + 0.3·ε[t-1]
  // Recursion recovers ε exactly.
  let rng = Xoshiro::from_state(1UL, 2UL, 3UL, 4UL)
  let n = 30
  let eps : Array[Float] = Array::make(n, 0.0F)
  for i in 0..<n {
    let (z, _) = box_muller(rng)
    eps[i] = Float::from_double(z)
  }
  let y : Array[Float] = Array::make(n, 0.0F)
  y[0] = eps[0]
  for t in 1..<n {
    y[t] = 0.5F * y[t - 1] + eps[t] + 0.3F * eps[t - 1]
  }
  let param = ArmaParam::new(1, 1, 0.0F, [0.5F], [0.3F])
  let loss = arma_residual_loss(y, param)
  let mut expected = 0.0F
  for i in 0..<n {
    expected = expected + eps[i] * eps[i]
  }
  if (loss - expected).abs() > 0.001F {
    fail("loss = \{loss}, expected ~\{expected}")
  }
}

///|
test "arma_grad: matches finite difference for ARMA(1, 1)" {
  let rng = Xoshiro::from_state(5UL, 6UL, 7UL, 8UL)
  let n = 50
  let eps : Array[Float] = Array::make(n, 0.0F)
  for i in 0..<n {
    let (z, _) = box_muller(rng)
    eps[i] = Float::from_double(z)
  }
  let y : Array[Float] = Array::make(n, 0.0F)
  y[0] = eps[0]
  for t in 1..<n {
    y[t] = 0.5F * y[t - 1] + eps[t] + 0.3F * eps[t - 1]
  }
  let param = ArmaParam::new(1, 1, 0.0F, [0.5F], [0.3F])
  let (g_ar, g_ma) = arma_grad(y, param)
  // FD for ar_coeffs[0]
  let fd_ar = finite_diff(
    fn(v : Float) { arma_loss_value(y, 0.0F, [v], [0.3F]) }, 0.5F, 1.0e-3F,
  )
  let fd_ma = finite_diff(
    fn(v : Float) { arma_loss_value(y, 0.0F, [0.5F], [v]) }, 0.3F, 1.0e-3F,
  )
  if (g_ar[0] - fd_ar).abs() > 1.0e-2F {
    fail("∂/∂φ: hand = \{g_ar[0]}, fd = \{fd_ar}")
  }
  if (g_ma[0] - fd_ma).abs() > 1.0e-2F {
    fail("∂/∂θ: hand = \{g_ma[0]}, fd = \{fd_ma}")
  }
}

///|
test "arma_grad: matches finite difference for ARMA(2, 2)" {
  let rng = Xoshiro::from_state(9UL, 10UL, 11UL, 12UL)
  let n = 60
  let eps : Array[Float] = Array::make(n, 0.0F)
  for i in 0..<n {
    let (z, _) = box_muller(rng)
    eps[i] = Float::from_double(z)
  }
  let y : Array[Float] = Array::make(n, 0.0F)
  y[0] = eps[0]
  y[1] = 0.5F * y[0] + eps[1] + 0.3F * eps[0]
  for t in 2..<n {
    y[t] = 0.4F * y[t - 1] + 0.2F * y[t - 2] + eps[t] + 0.25F * eps[t - 1] +
      0.1F * eps[t - 2]
  }
  let param = ArmaParam::new(2, 2, 0.0F, [0.4F, 0.2F], [0.25F, 0.1F])
  let (g_ar, g_ma) = arma_grad(y, param)
  // FD for each coefficient independently
  let fd_ar_0 = finite_diff(
    fn(v : Float) { arma_loss_value(y, 0.0F, [v, 0.2F], [0.25F, 0.1F]) },
    0.4F,
    1.0e-3F,
  )
  let fd_ar_1 = finite_diff(
    fn(v : Float) { arma_loss_value(y, 0.0F, [0.4F, v], [0.25F, 0.1F]) },
    0.2F,
    1.0e-3F,
  )
  let fd_ma_0 = finite_diff(
    fn(v : Float) { arma_loss_value(y, 0.0F, [0.4F, 0.2F], [v, 0.1F]) },
    0.25F,
    1.0e-3F,
  )
  let fd_ma_1 = finite_diff(
    fn(v : Float) { arma_loss_value(y, 0.0F, [0.4F, 0.2F], [0.25F, v]) },
    0.1F,
    1.0e-3F,
  )
  if (g_ar[0] - fd_ar_0).abs() > 1.0e-1F {
    fail("∂/∂φ_0: hand = \{g_ar[0]}, fd = \{fd_ar_0}")
  }
  if (g_ar[1] - fd_ar_1).abs() > 1.0e-1F {
    fail("∂/∂φ_1: hand = \{g_ar[1]}, fd = \{fd_ar_1}")
  }
  if (g_ma[0] - fd_ma_0).abs() > 1.0e-1F {
    fail("∂/∂θ_0: hand = \{g_ma[0]}, fd = \{fd_ma_0}")
  }
  if (g_ma[1] - fd_ma_1).abs() > 1.0e-1F {
    fail("∂/∂θ_1: hand = \{g_ma[1]}, fd = \{fd_ma_1}")
  }
}

///|
test "arma_fit: gradient descent recovers ARMA(1, 1) coefficients" {
  // Generate ARMA(1, 1) with phi=0.5, theta=0.3, then fit.
  let rng = Xoshiro::from_state(13UL, 14UL, 15UL, 16UL)
  let n = 500
  let eps : Array[Float] = Array::make(n, 0.0F)
  for i in 0..<n {
    let (z, _) = box_muller(rng)
    eps[i] = Float::from_double(z)
  }
  let y : Array[Float] = Array::make(n, 0.0F)
  y[0] = eps[0]
  for t in 1..<n {
    y[t] = 0.5F * y[t - 1] + eps[t] + 0.3F * eps[t - 1]
  }
  // Run gradient descent
  let param = arma_fit(y, 1, 1, 500, 0.0001F)
  // After 500 steps, coefficients should be near the true values.
  if (param.ar_coeffs[0] - 0.5F).abs() > 0.2F {
    fail("fitted φ = \{param.ar_coeffs[0]}, expected ≈ 0.5")
  }
  if (param.ma_thetas[0] - 0.3F).abs() > 0.2F {
    fail("fitted θ = \{param.ma_thetas[0]}, expected ≈ 0.3")
  }
}

///|
test "arma: empty / too-short series handled gracefully" {
  let empty : Array[Float] = []
  let p = ArmaParam::new(1, 1, 0.0F, [0.5F], [0.3F])
  assert_eq(arma_residuals(empty, p).length(), 0)
  assert_eq(arma_residual_loss(empty, p), 0.0F)
  let (g_ar, g_ma) = arma_grad(empty, p)
  assert_eq(g_ar.length(), 1)
  assert_eq(g_ma.length(), 1)
}

///|
test "arma_loss_value: matches forward evaluation" {
  let y : Array[Float] = [1.0F, 2.0F, 3.0F, 4.0F]
  let param = ArmaParam::new(1, 1, 0.0F, [0.5F], [0.3F])
  let direct = arma_residual_loss(y, param)
  let via_helper = arma_loss_value(y, 0.0F, [0.5F], [0.3F])
  if (direct - via_helper).abs() > 0.001F {
    fail("loss mismatch: direct = \{direct}, helper = \{via_helper}")
  }
}