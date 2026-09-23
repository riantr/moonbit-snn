// autodiff_demo_test.mbt — Gradient verification for autodiff demos.

///|
test "demo_polynomial: df/dx = 3x² - 2 at x = 2 → 10" {
  // f(x) = x³ - 2x + 1; f'(x) = 3x² - 2; f'(2) = 12 - 2 = 10.
  let g = demo_polynomial(2.0F)
  if (g - 10.0F).abs() > 0.0001F {
    fail("df/dx = \{g}, expected 10.0")
  }
}

///|
test "demo_polynomial: closed-form vs finite difference at x = 1.5" {
  let x = 1.5F
  let tape_grad = demo_polynomial(x)
  // Finite differences need the *value* function, not the gradient.
  let fd_grad = finite_diff(polynomial_value, x, 1.0e-3F)
  if (tape_grad - fd_grad).abs() > 1.0e-3F {
    fail("tape = \{tape_grad}, fd = \{fd_grad}, mismatch")
  }
}

///|
test "demo_polynomial: zero gradient at critical point x = ±√(2/3)" {
  // df/dx = 3x² - 2 = 0 → x² = 2/3 → x = ±sqrt(2/3) ≈ ±0.816.
  let x_crit = sqrtf(2.0F / 3.0F)
  let g = demo_polynomial(x_crit)
  if g.abs() > 0.0001F {
    fail("df/dx at critical = \{g}, expected ≈ 0")
  }
}

///|
test "demo_rosenbrock: gradients match closed form at (1, 1)" {
  // At (x=1, y=1): f = 0 (the global minimum). df/dx and df/dy should
  // also vanish at the minimum.
  // df/dx = -2(a-x) - 4bx(y - x²)
  // df/dy = 2b(y - x²)
  let (gx, gy) = demo_rosenbrock(1.0F, 1.0F)
  if gx.abs() > 0.0001F {
    fail("df/dx = \{gx}, expected ≈ 0 at minimum")
  }
  if gy.abs() > 0.0001F {
    fail("df/dy = \{gy}, expected ≈ 0 at minimum")
  }
}

///|
test "demo_rosenbrock: closed-form vs finite difference at (0.5, 0.5)" {
  let x : Float = 0.5
  let y : Float = 0.5
  let (gx_tape, gy_tape) = demo_rosenbrock(x, y)
  // Finite differences need the *value* function, not the gradient.
  let h : Float = 1.0e-3
  let gx_fd = finite_diff(fn(v : Float) { rosenbrock_value(v, y) }, x, h)
  let gy_fd = finite_diff(fn(v : Float) { rosenbrock_value(x, v) }, y, h)
  if (gx_tape - gx_fd).abs() > 1.0e-2F {
    fail("df/dx: tape = \{gx_tape}, fd = \{gx_fd}")
  }
  if (gy_tape - gy_fd).abs() > 1.0e-2F {
    fail("df/dy: tape = \{gy_tape}, fd = \{gy_fd}")
  }
}

///|
test "demo_ma1_loss: matches finite difference on small series" {
  // Series y = [1, 2, 3, 4]. Loss(θ) = Σ (y[t] - θ ε[t-1])².
  // Forward: ε[0]=1, ε[1]=2-θ, ε[2]=3-θ(2-θ), ε[3]=4-θ(3-θ(2-θ)).
  let y : Array[Float] = [1.0F, 2.0F, 3.0F, 4.0F]
  let theta : Float = 0.5
  let tape_grad = demo_ma1_loss(y, theta)
  let fd_grad = finite_diff(
    fn(v : Float) { ma1_loss_value(y, v) }, theta, 1.0e-3F,
  )
  if (tape_grad - fd_grad).abs() > 1.0e-2F {
    fail("dloss/dθ: tape = \{tape_grad}, fd = \{fd_grad}")
  }
}

///|
test "demo_ma1_loss: gradient is non-zero and matches FD for noisy data" {
  // Random-ish series. Both tape and FD should give the same answer
  // (within tolerance) regardless of the magnitude.
  let y : Array[Float] = [0.5F, 1.2F, 0.8F, 1.5F, 0.3F, 1.0F, 0.7F, 1.3F]
  let theta : Float = 0.3
  let tape_grad = demo_ma1_loss(y, theta)
  let fd_grad = finite_diff(
    fn(v : Float) { ma1_loss_value(y, v) }, theta, 1.0e-3F,
  )
  if (tape_grad - fd_grad).abs() > 1.0e-2F {
    fail("dloss/dθ: tape = \{tape_grad}, fd = \{fd_grad}")
  }
}

///|
test "demo_ma1_loss: matches finite difference at MA(1) optimum" {
  // Generate y from MA(1) with θ_true = 0.7; verify Tape and FD
  // agree at θ = 0.7 (they don't have to be exactly 0 — with only
  // 8 samples the sample-MLE is noisy around θ_true).
  let eps : Array[Float] = Array::make(8, 0.0F)
  eps[0] = 0.5F
  eps[1] = -0.3F
  eps[2] = 0.4F
  eps[3] = -0.2F
  eps[4] = 0.6F
  eps[5] = -0.4F
  eps[6] = 0.1F
  eps[7] = 0.0F
  // y[t] = ε[t] + 0.7 · ε[t-1]
  let y : Array[Float] = Array::make(8, 0.0F)
  y[0] = eps[0]
  for t in 1..<8 {
    y[t] = eps[t] + 0.7F * eps[t - 1]
  }
  let theta : Float = 0.7
  let tape_grad = demo_ma1_loss(y, theta)
  let fd_grad = finite_diff(
    fn(v : Float) { ma1_loss_value(y, v) }, theta, 1.0e-3F,
  )
  if (tape_grad - fd_grad).abs() > 1.0e-2F {
    fail("dloss/dθ: tape = \{tape_grad}, fd = \{fd_grad}")
  }
}

///|
test "finite_diff: identity check (linear function)" {
  // f(x) = 3x + 1 → df/dx = 3. fd(3, h) should return ≈ 3.
  let g = finite_diff(fn(x : Float) { 3.0F * x + 1.0F }, 0.5F, 1.0e-4F)
  if (g - 3.0F).abs() > 0.001F {
    fail("fd(3x+1) = \{g}, expected 3.0")
  }
}

///|
test "ma1_loss_value: matches manual sum for y = [1, 2, 3, 4]" {
  // θ = 0.5:
  // ε[0] = y[0] = 1 (treated as observed, not part of the loss).
  // ε[1] = 2 - 0.5·1 = 1.5
  // ε[2] = 3 - 0.5·1.5 = 2.25
  // ε[3] = 4 - 0.5·2.25 = 2.875
  // loss (t≥1) = 2.25 + 5.0625 + 8.265625 = 15.578125
  let y : Array[Float] = [1.0F, 2.0F, 3.0F, 4.0F]
  let loss = ma1_loss_value(y, 0.5F)
  let expected : Float = 2.25F + 5.0625F + 8.265625F
  if (loss - expected).abs() > 0.001F {
    fail("loss = \{loss}, expected ~\{expected}")
  }
}

///|
test "demo_ma1_loss: returns finite value (no NaN/Inf) on long series" {
  // Long series should still produce a finite gradient.
  let y : Array[Float] = Array::make(100, 0.0F)
  for i in 0..<100 {
    y[i] = Float::from_int(i) * 0.1F
  }
  let g = demo_ma1_loss(y, 0.5F)
  if g != g {
    fail("gradient is NaN")
  }
  // 0.0 == 0.0 is true; ensure not infinite.
  let big : Float = 1.0e10F
  if g > big || g < -big {
    fail("gradient magnitude = \{g}, suspiciously large")
  }
}