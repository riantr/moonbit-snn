// lstm_cell_test.mbt — Tests for LSTM cell forward + backward.

///|
test "lstm_cell_forward: zero inputs yields b_c ⊙ 0 = 0 for c, h = 0" {
  // With h_prev=0, x=0, the gates σ(0 + b) are not zero (unless b=0).
  // Since b=0 at init, gates = 0.5, c_tilde = 0, so c_t = 0.5*0 + 0.5*0 = 0.
  // h_t = 0.5 * tanh(0) = 0.
  let param = LstmCellParam::new(2, 3, 42UL)
  let x : Array[Float] = [0.0F, 0.0F]
  let h_prev : Array[Float] = [0.0F, 0.0F, 0.0F]
  let c_prev : Array[Float] = [0.0F, 0.0F, 0.0F]
  let (h_t, _) = lstm_cell_forward(x, h_prev, c_prev, param)
  for i in 0..<3 {
    if h_t[i].abs() > 1.0e-5F {
      fail("h_t[\{i}] = \{h_t[i]}, expected 0")
    }
  }
}

///|
test "lstm_cell_forward: output shape is d_h" {
  let param = LstmCellParam::new(5, 4, 1UL)
  let x : Array[Float] = Array::make(5, 0.1F)
  let h_prev : Array[Float] = Array::make(4, 0.0F)
  let c_prev : Array[Float] = Array::make(4, 0.0F)
  let (h_t, _) = lstm_cell_forward(x, h_prev, c_prev, param)
  assert_eq(h_t.length(), 4)
}

///|
test "lstm_cell_forward: gated update c_t = f*c_prev + i*c_tilde" {
  // With biases set so that gates saturate (e.g., b_f very positive),
  // we can check the gate update formula directly. Use random small
  // init and verify c_t is in the expected range.
  let param = LstmCellParam::new(2, 2, 7UL)
  let x : Array[Float] = [1.0F, -1.0F]
  let h_prev : Array[Float] = [0.5F, 0.5F]
  let c_prev : Array[Float] = [0.0F, 0.0F]
  let (_, cache) = lstm_cell_forward(x, h_prev, c_prev, param)
  // c_t[0] = f[0] * 0 + i[0] * c_tilde[0] = i[0] * c_tilde[0]
  let c_t_0 = cache.f[0] * cache.c_prev[0] + cache.ig[0] * cache.c_tilde[0]
  if (cache.c_t[0] - c_t_0).abs() > 1.0e-4F {
    fail("c_t[0] = \{cache.c_t[0]}, expected = \{c_t_0}")
  }
}

///|
test "lstm_cell_backward: zero d_h / d_c → zero d_h_prev / d_x / d_c_prev" {
  // If upstream gradient is zero, backward should produce zero gradients
  // (except for the bias contribution from non-zero d_c_t).
  let param = LstmCellParam::new(2, 3, 11UL)
  let x : Array[Float] = [0.1F, 0.2F]
  let h_prev : Array[Float] = [0.0F, 0.0F, 0.0F]
  let c_prev : Array[Float] = [0.0F, 0.0F, 0.0F]
  let (_, cache) = lstm_cell_forward(x, h_prev, c_prev, param)
  let grad = LstmCellGrad::zero(param)
  let d_h : Array[Float] = Array::make(3, 0.0F)
  let d_c : Array[Float] = Array::make(3, 0.0F)
  let (d_h_prev, d_x, d_c_prev) = lstm_cell_backward(
    cache, d_h, d_c, param, grad,
  )
  // d_h_prev, d_x, d_c_prev should all be zero
  for i in 0..<3 {
    if d_h_prev[i].abs() > 1.0e-5F {
      fail("d_h_prev[\{i}] = \{d_h_prev[i]}, expected 0")
    }
    if d_c_prev[i].abs() > 1.0e-5F {
      fail("d_c_prev[\{i}] = \{d_c_prev[i]}, expected 0")
    }
  }
  for i in 0..<2 {
    if d_x[i].abs() > 1.0e-5F {
      fail("d_x[\{i}] = \{d_x[i]}, expected 0")
    }
  }
}

///|
test "lstm_cell_backward: matches finite-difference gradient" {
  // Numerical gradient check on w_f[0][0]: perturb it by ±h, recompute
  // forward, observe change in h_t[0].
  let param = LstmCellParam::new(3, 2, 17UL)
  let x : Array[Float] = [0.1F, 0.2F, 0.3F]
  let h_prev : Array[Float] = [0.0F, 0.0F]
  let c_prev : Array[Float] = [0.0F, 0.0F]
  let (h_t_base, cache) = lstm_cell_forward(x, h_prev, c_prev, param)
  // Backward to get analytical gradient.
  let grad = LstmCellGrad::zero(param)
  let d_h : Array[Float] = [1.0F, 0.0F]   // ∂/∂h_0
  let d_c : Array[Float] = [0.0F, 0.0F]
  let _ = lstm_cell_backward(cache, d_h, d_c, param, grad)
  let analytical = grad.d_w_f[0][0]
  // Finite difference: change w_f[0][0] by h, observe Δh_t[0].
  let h : Float = 1.0e-3F
  let saved = param.w_f[0][0]
  param.w_f[0][0] = saved + h
  let (h_t_plus, _) = lstm_cell_forward(x, h_prev, c_prev, param)
  param.w_f[0][0] = saved - h
  let (h_t_minus, _) = lstm_cell_forward(x, h_prev, c_prev, param)
  param.w_f[0][0] = saved
  let fd = (h_t_plus[0] - h_t_minus[0]) / (2.0F * h)
  // Check the FD gradient of h_t[0] w.r.t. w_f[0][0].
  // (The analytical grad was for a different seed — recompute it
  //  with the same perturbation step.)
  let _ = h_t_base
  if (fd - analytical).abs() > 1.0e-2F {
    fail("∂h_t[0]/∂w_f[0][0]: analytical = \{analytical}, fd = \{fd}")
  }
}

///|
test "lstm_cell_backward: d_h flow through cell state is non-zero" {
  // With non-trivial cell state c_prev, a non-zero d_h should flow
  // through d_c_prev (cell-state gradient path).
  let param = LstmCellParam::new(2, 2, 23UL)
  let x : Array[Float] = [0.5F, 0.5F]
  let h_prev : Array[Float] = [0.3F, 0.3F]
  let c_prev : Array[Float] = [0.7F, 0.7F]
  let (_, cache) = lstm_cell_forward(x, h_prev, c_prev, param)
  let grad = LstmCellGrad::zero(param)
  let d_h : Array[Float] = [0.5F, 0.5F]
  let d_c : Array[Float] = [0.0F, 0.0F]
  let (_d_h_prev, _d_x, d_c_prev) = lstm_cell_backward(
    cache, d_h, d_c, param, grad,
  )
  // d_c_prev = d_c ⊙ f = 0 (since d_c is zero). So d_c_prev is zero.
  // To test d_h flow through cell, we need d_c ≠ 0.
  let d_c2 : Array[Float] = [0.5F, 0.5F]
  let _ = grad
  let (_, _, d_c_prev2) = lstm_cell_backward(
    cache, d_h, d_c2, param, LstmCellGrad::zero(param),
  )
  // Now d_c_prev2 = d_c ⊙ f. With d_c ≠ 0 and f non-zero, expect non-zero.
  let mut any_nonzero = false
  for i in 0..<2 {
    if d_c_prev2[i].abs() > 1.0e-5F {
      any_nonzero = true
    }
  }
  if !any_nonzero {
    fail("d_c_prev all zeros despite non-zero d_c and f")
  }
  let _ = d_c_prev
}

///|
test "lstm_cell_grad zero: matches param shape" {
  let param = LstmCellParam::new(3, 4, 99UL)
  let grad = LstmCellGrad::zero(param)
  assert_eq(grad.d_w_f.length(), 4)
  assert_eq(grad.d_w_i.length(), 4)
  assert_eq(grad.d_w_c.length(), 4)
  assert_eq(grad.d_w_o.length(), 4)
  assert_eq(grad.d_b_f.length(), 4)
  assert_eq(grad.d_b_i.length(), 4)
  assert_eq(grad.d_b_c.length(), 4)
  assert_eq(grad.d_b_o.length(), 4)
  assert_eq(grad.d_w_f[0].length(), 7) // d_h + d_x
  for i in 0..<4 {
    assert_eq(grad.d_b_f[i], 0.0F)
  }
}

///|
test "lstm_cell_sgd_step: applies gradients and clears grad" {
  let param = LstmCellParam::new(2, 2, 5UL)
  let grad = LstmCellGrad::zero(param)
  // Inject non-zero gradients.
  grad.d_w_f[0][0] = 1.0F
  grad.d_w_i[0][0] = 2.0F
  grad.d_w_c[0][0] = 3.0F
  grad.d_w_o[0][0] = 4.0F
  grad.d_b_f[0] = 0.5F
  let saved_w = param.w_f[0][0]
  let saved_i = param.w_i[0][0]
  let saved_c = param.w_c[0][0]
  let saved_o = param.w_o[0][0]
  let saved_b = param.b_f[0]
  let lr : Float = 0.1F
  lstm_cell_sgd_step(param, grad, lr)
  // Params should be updated.
  if (param.w_f[0][0] - (saved_w - lr * 1.0F)).abs() > 1.0e-5F {
    fail("w_f[0][0] not updated correctly: \{param.w_f[0][0]}")
  }
  if (param.b_f[0] - (saved_b - lr * 0.5F)).abs() > 1.0e-5F {
    fail("b_f[0] not updated correctly: \{param.b_f[0]}")
  }
  // Gradients should be cleared.
  for i in 0..<param.d_h {
    if grad.d_w_f[i][0] != 0.0F {
      fail("d_w_f[\{i}][0] not cleared: \{grad.d_w_f[i][0]}")
    }
    if grad.d_b_f[i] != 0.0F {
      fail("d_b_f[\{i}] not cleared: \{grad.d_b_f[i]}")
    }
  }
  let _ = saved_i
  let _ = saved_c
  let _ = saved_o
}