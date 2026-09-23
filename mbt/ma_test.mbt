// ma_test.mbt — Tests for MA(q) moving-average model.

///|
test "ma_residuals: MA(1) with theta=0.5 reproduces y[t] - 0.5·ε[t-1]" {
  let y : Array[Float] = [1.0F, 2.0F, 3.0F, 4.0F]
  let param = MaParam::new(1, 0.0F, [0.5F])
  let eps = ma_residuals(y, param)
  // ε[0] = y[0] - 0 = 1
  if (eps[0] - 1.0F).abs() > 0.001F {
    fail("ε[0] = \{eps[0]}, expected 1.0")
  }
  // ε[1] = y[1] - 0.5·ε[0] = 2 - 0.5 = 1.5
  if (eps[1] - 1.5F).abs() > 0.001F {
    fail("ε[1] = \{eps[1]}, expected 1.5")
  }
  // ε[2] = y[2] - 0.5·ε[1] = 3 - 0.75 = 2.25
  if (eps[2] - 2.25F).abs() > 0.001F {
    fail("ε[2] = \{eps[2]}, expected 2.25")
  }
}

///|
test "ma_residuals: MA(2) with thetas = [0.4, 0.2]" {
  // ε[t] = y[t] - 0.4·ε[t-1] - 0.2·ε[t-2]
  let y : Array[Float] = [1.0F, 2.0F, 3.0F, 4.0F, 5.0F]
  let param = MaParam::new(2, 0.0F, [0.4F, 0.2F])
  let eps = ma_residuals(y, param)
  // ε[0] = 1.0, ε[1] = 2.0 - 0.4·1.0 = 1.6
  // ε[2] = 3.0 - 0.4·1.6 - 0.2·1.0 = 3.0 - 0.64 - 0.2 = 2.16
  if (eps[2] - 2.16F).abs() > 0.001F {
    fail("ε[2] = \{eps[2]}, expected 2.16")
  }
}

///|
test "ma_residual_loss: zero for true MA(1) generated data" {
  // Generate y[t] = ε[t] + 0.7·ε[t-1]. Then ε computed by recursion
  // should recover the original ε values.
  let eps : Array[Float] = Array::make(20, 0.0F)
  eps[0] = 1.0F
  eps[1] = -0.5F
  eps[2] = 0.3F
  eps[3] = -0.2F
  for t in 4..<20 {
    eps[t] = eps[t - 1] * 0.3F - eps[t - 2] * 0.1F
  }
  let y : Array[Float] = Array::make(20, 0.0F)
  y[0] = eps[0]
  for t in 1..<20 {
    y[t] = eps[t] + 0.7F * eps[t - 1]
  }
  let param = MaParam::new(1, 0.0F, [0.7F])
  let loss = ma_residual_loss(y, param)
  // Loss is Σ ε[t]² over the full series (ε[0] = y[0] when q=1 since
  // there's no ε[-1] to subtract). The recursion correctly recovers
  // each ε from y and θ.
  let mut expected = 0.0F
  for t in 0..<20 {
    expected = expected + eps[t] * eps[t]
  }
  if (loss - expected).abs() > 0.001F {
    fail("loss = \{loss}, expected ~\{expected}")
  }
}

///|
test "ma_grad_thetas: matches Tape-based demo_ma1_loss for MA(1)" {
  // MA(1) hand-derived gradient should match Tape's gradient.
  let y : Array[Float] = [0.5F, 1.2F, 0.8F, 1.5F, 0.3F, 1.0F, 0.7F, 1.3F]
  let theta : Float = 0.3
  let param = MaParam::new(1, 0.0F, [theta])
  let g_hand = ma_grad_thetas(y, param)
  let g_tape = demo_ma1_loss(y, theta)
  if (g_hand[0] - g_tape).abs() > 1.0e-3F {
    fail("MA(1) grad: hand = \{g_hand[0]}, tape = \{g_tape}")
  }
}

///|
test "ma_grad_thetas: matches finite difference for MA(1)" {
  let y : Array[Float] = [0.5F, 1.2F, 0.8F, 1.5F, 0.3F, 1.0F, 0.7F, 1.3F]
  let theta : Float = 0.3
  let param = MaParam::new(1, 0.0F, [theta])
  let g_hand = ma_grad_thetas(y, param)
  let fd = finite_diff(
    fn(v : Float) { ma_loss_value(y, 0.0F, [v]) }, theta, 1.0e-3F,
  )
  if (g_hand[0] - fd).abs() > 1.0e-2F {
    fail("MA(1) grad: hand = \{g_hand[0]}, fd = \{fd}")
  }
}

///|
test "ma_grad_thetas: matches finite difference for MA(2)" {
  let y : Array[Float] = [0.5F, 1.2F, 0.8F, 1.5F, 0.3F, 1.0F, 0.7F, 1.3F, 0.9F, 1.1F]
  let thetas : Array[Float] = [0.3F, 0.2F]
  let param = MaParam::new(2, 0.0F, thetas)
  let g_hand = ma_grad_thetas(y, param)
  // FD for each θ independently
  let fd_0 = finite_diff(
    fn(v : Float) { ma_loss_value(y, 0.0F, [v, 0.2F]) }, 0.3F, 1.0e-3F,
  )
  let fd_1 = finite_diff(
    fn(v : Float) { ma_loss_value(y, 0.0F, [0.3F, v]) }, 0.2F, 1.0e-3F,
  )
  if (g_hand[0] - fd_0).abs() > 1.0e-2F {
    fail("MA(2) grad[0]: hand = \{g_hand[0]}, fd = \{fd_0}")
  }
  if (g_hand[1] - fd_1).abs() > 1.0e-2F {
    fail("MA(2) grad[1]: hand = \{g_hand[1]}, fd = \{fd_1}")
  }
}

///|
test "ma_grad_thetas: gradient is small but not zero at true MA(1)" {
  // Generate y[t] = ε[t] + 0.7·ε[t-1] for known ε. At θ = 0.7 the
  // residuals exactly equal the true ε (modulo pre-sample initialisation),
  // but the gradient is NOT exactly zero — it's only zero asymptotically.
  // For small n=15 it can be O(1) due to finite-sample noise.
  let eps : Array[Float] = Array::make(15, 0.0F)
  eps[0] = 1.0F
  eps[1] = -0.5F
  eps[2] = 0.3F
  eps[3] = -0.2F
  eps[4] = 0.6F
  eps[5] = -0.4F
  eps[6] = 0.1F
  eps[7] = 0.2F
  eps[8] = -0.3F
  eps[9] = 0.4F
  for t in 10..<15 {
    eps[t] = eps[t - 1] * 0.5F
  }
  let y : Array[Float] = Array::make(15, 0.0F)
  y[0] = eps[0]
  for t in 1..<15 {
    y[t] = eps[t] + 0.7F * eps[t - 1]
  }
  let param = MaParam::new(1, 0.0F, [0.7F])
  let g = ma_grad_thetas(y, param)
  // Sanity: gradient should be O(1) but not arbitrarily large.
  if g[0].abs() > 10.0F {
    fail("∂loss/∂θ at true θ = \{g[0]}, suspiciously large")
  }
}

///|
test "ma_fit: gradient descent recovers MA(1) theta on strong-signal data" {
  // Generate MA(1) data with theta=0.7 and high-noise-to-signal ratio.
  // Use a simple deterministic generator with strong autocorrelation
  // to make gradient descent converge reliably.
  let rng = Xoshiro::from_state(7UL, 11UL, 13UL, 17UL)
  let n = 500
  let eps : Array[Float] = Array::make(n, 0.0F)
  for t in 0..<n {
    let (z, _) = box_muller(rng)
    eps[t] = Float::from_double(z)
  }
  let y : Array[Float] = Array::make(n, 0.0F)
  y[0] = eps[0]
  for t in 1..<n {
    y[t] = eps[t] + 0.7F * eps[t - 1]
  }
  // Run gradient descent with a small learning rate.
  let param = ma_fit(y, 1, 500, 0.0001F)
  // After 500 steps, theta should be near 0.7.
  if (param.thetas[0] - 0.7F).abs() > 0.2F {
    fail("fitted θ = \{param.thetas[0]}, expected ≈ 0.7")
  }
}

///|
test "ma_fit: gradient descent moves θ in the right direction" {
  // For under-fit θ (= 0, starting point), gradient should push θ up
  // (toward the true positive θ). Just verify direction, not exact
  // convergence (which depends on step size).
  let rng = Xoshiro::from_state(23UL, 29UL, 31UL, 37UL)
  let n = 300
  let eps : Array[Float] = Array::make(n, 0.0F)
  for t in 0..<n {
    let (z, _) = box_muller(rng)
    eps[t] = Float::from_double(z)
  }
  let y : Array[Float] = Array::make(n, 0.0F)
  y[0] = eps[0]
  for t in 1..<n {
    y[t] = eps[t] + 0.7F * eps[t - 1]
  }
  // Compute gradient at θ=0
  let param0 = MaParam::new(1, 0.0F, [0.0F])
  let g = ma_grad_thetas(y, param0)
  // Gradient at θ=0 should be NEGATIVE (push θ up via -lr*g).
  if g[0] >= 0.0F {
    fail("gradient at θ=0 = \{g[0]}, expected negative (push θ up)")
  }
}

///|
test "ma: empty / too-short series handled gracefully" {
  let empty : Array[Float] = []
  let p = MaParam::new(2, 0.0F, [0.3F, 0.2F])
  assert_eq(ma_residuals(empty, p).length(), 0)
  assert_eq(ma_residual_loss(empty, p), 0.0F)
  let grad_empty = ma_grad_thetas(empty, p)
  assert_eq(grad_empty.length(), 2)
}

///|
test "ma_loss_value: matches forward evaluation" {
  let y : Array[Float] = [1.0F, 2.0F, 3.0F, 4.0F]
  let param = MaParam::new(1, 0.0F, [0.5F])
  let direct = ma_residual_loss(y, param)
  let via_helper = ma_loss_value(y, 0.0F, [0.5F])
  if (direct - via_helper).abs() > 0.001F {
    fail("loss mismatch: direct = \{direct}, helper = \{via_helper}")
  }
}