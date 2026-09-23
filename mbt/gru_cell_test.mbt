// gru_cell_test.mbt — Tests for GRU cell forward + backward + SGD (v0.30.0).

///|
test "gru_cell_new: shapes and zero biases" {
  let p = GruCellParam::new(3, 4, 42UL)
  assert_eq(p.d_x, 3)
  assert_eq(p.d_h, 4)
  assert_eq(p.w_z.length(), 4)
  assert_eq(p.w_r.length(), 4)
  assert_eq(p.w_n.length(), 4)
  for i in 0..<4 {
    assert_eq(p.w_z[i].length(), 7) // d_h + d_x = 4 + 3
    assert_eq(p.w_r[i].length(), 7)
    assert_eq(p.w_n[i].length(), 7)
  }
  assert_eq(p.b_z.length(), 4)
  assert_eq(p.b_r.length(), 4)
  assert_eq(p.b_n.length(), 4)
  for i in 0..<4 {
    assert_eq(p.b_z[i], 0.0F)
    assert_eq(p.b_r[i], 0.0F)
    assert_eq(p.b_n[i], 0.0F)
  }
}

///|
test "gru_cell_forward: output shape + value range" {
  let p = GruCellParam::new(2, 3, 7UL)
  let x : Array[Float] = [0.5F, -0.3F]
  let h_prev : Array[Float] = [0.1F, 0.2F, -0.1F]
  let (h_t, cache) = gru_cell_forward(x, h_prev, p)
  assert_eq(h_t.length(), 3)
  assert_eq(cache.z.length(), 3)
  assert_eq(cache.r.length(), 3)
  assert_eq(cache.n.length(), 3)
  assert_eq(cache.s.length(), 3)
  // Gates in (0, 1), h_t bounded by [-1, 1] element-wise.
  for i in 0..<3 {
    assert_true(cache.z[i] > 0.0F && cache.z[i] < 1.0F)
    assert_true(cache.r[i] > 0.0F && cache.r[i] < 1.0F)
    assert_true(cache.n[i] >= -1.0F && cache.n[i] <= 1.0F)
    assert_true(h_t[i] >= -1.0F && h_t[i] <= 1.0F)
  }
}

///|
test "gru_cell_forward: deterministic with same seed" {
  let p1 = GruCellParam::new(2, 3, 99UL)
  let p2 = GruCellParam::new(2, 3, 99UL)
  let x : Array[Float] = [0.5F, -0.3F]
  let h_prev : Array[Float] = [0.1F, 0.2F, -0.1F]
  let (h1, _) = gru_cell_forward(x, h_prev, p1)
  let (h2, _) = gru_cell_forward(x, h_prev, p2)
  for i in 0..<3 {
    assert_eq(h1[i], h2[i])
  }
}

///|
test "gru_cell_backward: zero d_h -> zero d_h_prev + zero d_x" {
  let p = GruCellParam::new(2, 3, 5UL)
  let x : Array[Float] = [0.5F, -0.3F]
  let h_prev : Array[Float] = [0.1F, 0.2F, -0.1F]
  let (_h_t, cache) = gru_cell_forward(x, h_prev, p)
  let grad = GruCellGrad::zero(p)
  let d_h_t : Array[Float] = [0.0F, 0.0F, 0.0F]
  let (d_h_prev, d_x) = gru_cell_backward(cache, d_h_t, p, grad)
  assert_eq(d_h_prev.length(), 3)
  assert_eq(d_x.length(), 2)
  for i in 0..<3 {
    assert_eq(d_h_prev[i], 0.0F)
  }
  for i in 0..<2 {
    assert_eq(d_x[i], 0.0F)
  }
  for i in 0..<3 {
    assert_eq(grad.d_b_z[i], 0.0F)
    assert_eq(grad.d_b_r[i], 0.0F)
    assert_eq(grad.d_b_n[i], 0.0F)
    for j in 0..<5 {
      assert_eq(grad.d_w_z[i][j], 0.0F)
      assert_eq(grad.d_w_r[i][j], 0.0F)
      assert_eq(grad.d_w_n[i][j], 0.0F)
    }
  }
}

///|
test "gru_cell_backward: gradient check vs central difference on w_z[0,0]" {
  let p = GruCellParam::new(2, 2, 11UL)
  let x : Array[Float] = [0.5F, -0.3F]
  let h_prev : Array[Float] = [0.1F, 0.2F]
  let (_h_t, cache) = gru_cell_forward(x, h_prev, p)
  // Choose d_h_t to break symmetry.
  let d_h_t : Array[Float] = [0.7F, -0.4F]
  // Analytical gradient (forward, backward, accumulate).
  let grad = GruCellGrad::zero(p)
  let (_d_h_prev, _d_x) = gru_cell_backward(cache, d_h_t, p, grad)
  let analytical = grad.d_w_z[0][0]
  // Numerical via central difference on loss = sum(h_t * d_h_t).
  let eps = 1.0e-3F
  let p_plus = p
  p_plus.w_z[0][0] = p.w_z[0][0] + eps
  let (h_plus, _) = gru_cell_forward(x, h_prev, p_plus)
  let l_plus = h_plus[0] * d_h_t[0] + h_plus[1] * d_h_t[1]
  let p_minus = p
  p_minus.w_z[0][0] = p.w_z[0][0] - eps
  let (h_minus, _) = gru_cell_forward(x, h_prev, p_minus)
  let l_minus = h_minus[0] * d_h_t[0] + h_minus[1] * d_h_t[1]
  let numerical = (l_plus - l_minus) / (2.0F * eps)
  let diff = if analytical > numerical {
    analytical - numerical
  } else {
    numerical - analytical
  }
  let tol = 5.0e-2F
  assert_true(diff < tol)
}

///|
test "gru_cell_backward: gradient check vs central difference on w_n[0,1]" {
  // w_n is special (its input sees r ⊙ h_prev); pick a non-(0,0) cell.
  let p = GruCellParam::new(2, 2, 13UL)
  let x : Array[Float] = [0.5F, -0.3F]
  let h_prev : Array[Float] = [0.1F, 0.2F]
  let (_h_t, cache) = gru_cell_forward(x, h_prev, p)
  let d_h_t : Array[Float] = [0.7F, -0.4F]
  let grad = GruCellGrad::zero(p)
  let (_d_h_prev, _d_x) = gru_cell_backward(cache, d_h_t, p, grad)
  let analytical = grad.d_w_n[0][1]
  let eps = 1.0e-3F
  let p_plus = p
  p_plus.w_n[0][1] = p.w_n[0][1] + eps
  let (h_plus, _) = gru_cell_forward(x, h_prev, p_plus)
  let l_plus = h_plus[0] * d_h_t[0] + h_plus[1] * d_h_t[1]
  let p_minus = p
  p_minus.w_n[0][1] = p.w_n[0][1] - eps
  let (h_minus, _) = gru_cell_forward(x, h_prev, p_minus)
  let l_minus = h_minus[0] * d_h_t[0] + h_minus[1] * d_h_t[1]
  let numerical = (l_plus - l_minus) / (2.0F * eps)
  let diff = if analytical > numerical {
    analytical - numerical
  } else {
    numerical - analytical
  }
  let tol = 5.0e-2F
  assert_true(diff < tol)
}

///|
test "gru_cell_sgd_step: parameters change, grad resets" {
  let p = GruCellParam::new(2, 2, 23UL)
  let grad = GruCellGrad::zero(p)
  // Inject some non-zero gradients.
  grad.d_w_z[0][0] = 1.0F
  grad.d_w_r[1][1] = 0.5F
  grad.d_w_n[0][2] = -0.2F
  grad.d_b_z[0] = 0.1F
  let w_z_00_before = p.w_z[0][0]
  let w_r_11_before = p.w_r[1][1]
  let w_n_02_before = p.w_n[0][2]
  let b_z_0_before = p.b_z[0]
  gru_cell_sgd_step(p, grad, 0.1F)
  assert_eq(p.w_z[0][0], w_z_00_before - 0.1F * 1.0F)
  assert_eq(p.w_r[1][1], w_r_11_before - 0.1F * 0.5F)
  assert_eq(p.w_n[0][2], w_n_02_before - 0.1F * -0.2F)
  assert_eq(p.b_z[0], b_z_0_before - 0.1F * 0.1F)
  // Gradients reset to zero.
  for i in 0..<2 {
    for j in 0..<4 {
      assert_eq(grad.d_w_z[i][j], 0.0F)
      assert_eq(grad.d_w_r[i][j], 0.0F)
      assert_eq(grad.d_w_n[i][j], 0.0F)
    }
    assert_eq(grad.d_b_z[i], 0.0F)
    assert_eq(grad.d_b_r[i], 0.0F)
    assert_eq(grad.d_b_n[i], 0.0F)
  }
}

///|
test "gru_cell_zero_grad: shapes match param" {
  let p = GruCellParam::new(3, 5, 31UL)
  let g = GruCellGrad::zero(p)
  assert_eq(g.d_w_z.length(), 5)
  assert_eq(g.d_w_r.length(), 5)
  assert_eq(g.d_w_n.length(), 5)
  assert_eq(g.d_b_z.length(), 5)
  assert_eq(g.d_b_r.length(), 5)
  assert_eq(g.d_b_n.length(), 5)
  for i in 0..<5 {
    assert_eq(g.d_w_z[i].length(), 8)
    assert_eq(g.d_w_r[i].length(), 8)
    assert_eq(g.d_w_n[i].length(), 8)
  }
}
