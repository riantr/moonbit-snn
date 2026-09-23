// bilstm_test.mbt — Tests for BiLSTM (v0.32.1).

///|
test "bilstm_new: shapes + per-direction params" {
  let p = BiLstmParam::new(3, 4, 7UL)
  assert_eq(p.d_x, 3)
  assert_eq(p.d_h, 4)
  assert_eq(p.fwd.d_x, 3)
  assert_eq(p.fwd.d_h, 4)
  assert_eq(p.bwd.d_x, 3)
  assert_eq(p.bwd.d_h, 4)
  // in_dim = d_h + d_x = 4 + 3 = 7
  assert_eq(p.fwd.w_f[0].length(), 7)
  assert_eq(p.bwd.w_f[0].length(), 7)
}

///|
test "bilstm_forward: per-direction shapes" {
  let p = BiLstmParam::new(2, 3, 11UL)
  let xs : Array[Array[Float]] = [
    [0.5F, -0.3F],
    [0.1F, 0.2F],
    [-0.4F, 0.6F],
  ]
  let h0_f : Array[Float] = [0.0F, 0.0F, 0.0F]
  let c0_f : Array[Float] = [0.0F, 0.0F, 0.0F]
  let h0_b : Array[Float] = [0.0F, 0.0F, 0.0F]
  let c0_b : Array[Float] = [0.0F, 0.0F, 0.0F]
  let (hs_f, hs_b, cs_f, cs_b, cache) = bilstm_forward(
    xs, h0_f, c0_f, h0_b, c0_b, p,
  )
  assert_eq(hs_f.length(), 3)
  assert_eq(hs_b.length(), 3)
  assert_eq(cs_f.length(), 3)
  assert_eq(cs_b.length(), 3)
  assert_eq(cache.caches_f.length(), 3)
  assert_eq(cache.caches_b.length(), 3)
  for t in 0..<3 {
    assert_eq(hs_f[t].length(), 3)
    assert_eq(hs_b[t].length(), 3)
    assert_eq(cs_f[t].length(), 3)
    assert_eq(cs_b[t].length(), 3)
  }
}

///|
test "bilstm_forward: same-seed deterministic" {
  let p1 = BiLstmParam::new(2, 3, 13UL)
  let p2 = BiLstmParam::new(2, 3, 13UL)
  let xs : Array[Array[Float]] = [
    [0.5F, -0.3F],
    [0.1F, 0.2F],
  ]
  let h0 : Array[Float] = [0.0F, 0.0F, 0.0F]
  let c0 : Array[Float] = [0.0F, 0.0F, 0.0F]
  let (hs_f1, hs_b1, _, _, _) = bilstm_forward(xs, h0, c0, h0, c0, p1)
  let (hs_f2, hs_b2, _, _, _) = bilstm_forward(xs, h0, c0, h0, c0, p2)
  for t in 0..<2 {
    for i in 0..<3 {
      assert_eq(hs_f1[t][i], hs_f2[t][i])
      assert_eq(hs_b1[t][i], hs_b2[t][i])
    }
  }
}

///|
test "bilstm_concat: interleaves fwd then bwd" {
  let hs_f : Array[Array[Float]] = [
    [1.0F, 2.0F],
    [3.0F, 4.0F],
  ]
  let hs_b : Array[Array[Float]] = [
    [5.0F, 6.0F],
    [7.0F, 8.0F],
  ]
  let c0 = bilstm_concat(hs_f, hs_b, 0)
  let c1 = bilstm_concat(hs_f, hs_b, 1)
  assert_eq(c0.length(), 4)
  assert_eq(c1.length(), 4)
  assert_eq(c0[0], 1.0F)
  assert_eq(c0[1], 2.0F)
  assert_eq(c0[2], 5.0F)
  assert_eq(c0[3], 6.0F)
  assert_eq(c1[0], 3.0F)
  assert_eq(c1[1], 4.0F)
  assert_eq(c1[2], 7.0F)
  assert_eq(c1[3], 8.0F)
}

///|
test "bilstm_loss: zero on perfect match" {
  let p = BiLstmParam::new(2, 3, 17UL)
  let xs : Array[Array[Float]] = [[0.5F, -0.3F], [0.1F, 0.2F]]
  let h0 : Array[Float] = [0.0F, 0.0F, 0.0F]
  let c0 : Array[Float] = [0.0F, 0.0F, 0.0F]
  let (hs_f, hs_b, _, _, _) = bilstm_forward(xs, h0, c0, h0, c0, p)
  let target : Array[Array[Float]] = [
    bilstm_concat(hs_f, hs_b, 0),
    bilstm_concat(hs_f, hs_b, 1),
  ]
  let loss = bilstm_sequence_loss(hs_f, hs_b, target)
  assert_eq(loss, 0.0F)
}

///|
test "bilstm_backward: zero d_loss -> zero all grads" {
  let p = BiLstmParam::new(2, 3, 19UL)
  let xs : Array[Array[Float]] = [[0.5F, -0.3F], [0.1F, 0.2F]]
  let h0 : Array[Float] = [0.0F, 0.0F, 0.0F]
  let c0 : Array[Float] = [0.0F, 0.0F, 0.0F]
  let (hs_f, hs_b, _, _, cache) = bilstm_forward(xs, h0, c0, h0, c0, p)
  // Perfect-match target => loss = 0 => all grads = 0.
  let target : Array[Array[Float]] = [
    bilstm_concat(hs_f, hs_b, 0),
    bilstm_concat(hs_f, hs_b, 1),
  ]
  let (d_xs, dh0_f, dc0_f, dh0_b, dc0_b, grad_f, grad_b) = bilstm_backward(
    hs_f, hs_b, target, cache, h0, c0, h0, c0, p,
  )
  assert_eq(d_xs.length(), 2)
  for t in 0..<2 {
    for i in 0..<2 {
      assert_eq(d_xs[t][i], 0.0F)
    }
  }
  for i in 0..<3 {
    assert_eq(dh0_f[i], 0.0F)
    assert_eq(dc0_f[i], 0.0F)
    assert_eq(dh0_b[i], 0.0F)
    assert_eq(dc0_b[i], 0.0F)
    assert_eq(grad_f.d_b_f[i], 0.0F)
    assert_eq(grad_b.d_b_f[i], 0.0F)
  }
}

///|
test "bilstm_backward: shapes" {
  let p = BiLstmParam::new(3, 4, 23UL)
  let xs : Array[Array[Float]] = [
    [0.5F, -0.3F, 0.1F],
    [0.1F, 0.2F, -0.5F],
    [-0.4F, 0.6F, 0.2F],
  ]
  let h0 : Array[Float] = Array::make(4, 0.0F)
  let c0 : Array[Float] = Array::make(4, 0.0F)
  let (hs_f, hs_b, _, _, cache) = bilstm_forward(xs, h0, c0, h0, c0, p)
  let target : Array[Array[Float]] = [
    [0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F],
    [0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F],
    [0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F],
  ]
  let (d_xs, _dh0_f, _dc0_f, _dh0_b, _dc0_b, grad_f, grad_b) = bilstm_backward(
    hs_f, hs_b, target, cache, h0, c0, h0, c0, p,
  )
  assert_eq(d_xs.length(), 3)
  for t in 0..<3 {
    assert_eq(d_xs[t].length(), 3)
  }
  assert_eq(grad_f.d_w_f.length(), 4)
  assert_eq(grad_b.d_w_f.length(), 4)
  // in_dim = d_h + d_x = 4 + 3 = 7
  for i in 0..<4 {
    assert_eq(grad_f.d_w_f[i].length(), 7)
    assert_eq(grad_b.d_w_f[i].length(), 7)
  }
}

///|
test "bilstm_train: loss decreases on identity-shift task" {
  // d_x = d_h = 3, so target dim = 2*3 = 6 = 2*d_h.
  let p = BiLstmParam::new(3, 3, 29UL)
  let (xs, target) = bilstm_identity_dataset(8, 3, 3, 31UL)
  let h0 : Array[Float] = [0.0F, 0.0F, 0.0F]
  let c0 : Array[Float] = [0.0F, 0.0F, 0.0F]
  let (hs_f0, hs_b0, _, _, _) = bilstm_forward(xs, h0, c0, h0, c0, p)
  let init_loss = bilstm_sequence_loss(hs_f0, hs_b0, target)
  let final_loss = bilstm_train_n_steps(
    xs, target, h0, c0, h0, c0, p, 60, 0.01F, 1.0F,
  )
  if final_loss >= init_loss {
    fail("loss didn't decrease: init = \{init_loss}, final = \{final_loss}")
  }
}

///|
test "bilstm_identity_dataset: shapes + cyclic shift" {
  let (xs, target) = bilstm_identity_dataset(5, 2, 2, 7UL)
  assert_eq(xs.length(), 5)
  assert_eq(target.length(), 5)
  for t in 0..<5 {
    assert_eq(xs[t].length(), 2)
    assert_eq(target[t].length(), 4) // 2*d_target
  }
  // target[0] = [xs[4]; xs[1]] in first 2 entries each.
  assert_eq(target[0][0], xs[4][0])
  assert_eq(target[0][1], xs[4][1])
  assert_eq(target[0][2], xs[1][0])
  assert_eq(target[0][3], xs[1][1])
  // target[3] = [xs[2]; xs[4]] in first 2 entries each.
  assert_eq(target[3][0], xs[2][0])
  assert_eq(target[3][1], xs[2][1])
  assert_eq(target[3][2], xs[4][0])
  assert_eq(target[3][3], xs[4][1])
}
