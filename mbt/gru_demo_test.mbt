// gru_demo_test.mbt — Tests for GRU sequence + BPTT + training (v0.30.1).

///|
test "gru_sequence_forward: returns one cache per timestep" {
  let p = GruCellParam::new(2, 3, 7UL)
  let xs : Array[Array[Float]] = [
    [0.5F, -0.3F],
    [0.1F, 0.2F],
    [-0.4F, 0.6F],
  ]
  let h0 : Array[Float] = [0.1F, 0.0F, -0.2F]
  let (hs, caches) = gru_sequence_forward(xs, h0, p)
  assert_eq(hs.length(), 3)
  assert_eq(caches.length(), 3)
  for t in 0..<3 {
    assert_eq(hs[t].length(), 3)
    assert_eq(caches[t].z.length(), 3)
    assert_eq(caches[t].r.length(), 3)
    assert_eq(caches[t].n.length(), 3)
    assert_eq(caches[t].h_t.length(), 3)
  }
}

///|
test "gru_sequence_forward: deterministic with same seed" {
  let p1 = GruCellParam::new(2, 3, 99UL)
  let p2 = GruCellParam::new(2, 3, 99UL)
  let xs : Array[Array[Float]] = [
    [0.5F, -0.3F],
    [0.1F, 0.2F],
  ]
  let h0 : Array[Float] = [0.1F, 0.0F, -0.2F]
  let (hs1, _) = gru_sequence_forward(xs, h0, p1)
  let (hs2, _) = gru_sequence_forward(xs, h0, p2)
  for t in 0..<2 {
    for i in 0..<3 {
      assert_eq(hs1[t][i], hs2[t][i])
    }
  }
}

///|
test "gru_sequence_loss: zero loss for matching sequences" {
  let p = GruCellParam::new(2, 3, 5UL)
  let xs : Array[Array[Float]] = [[0.5F, -0.3F], [0.1F, 0.2F]]
  let h0 : Array[Float] = [0.1F, 0.0F, -0.2F]
  let (hs, _) = gru_sequence_forward(xs, h0, p)
  let loss = gru_sequence_loss(hs, hs)
  assert_eq(loss, 0.0F)
}

///|
test "gru_sequence_loss: returns -1 on length mismatch" {
  let p = GruCellParam::new(2, 3, 5UL)
  let xs : Array[Array[Float]] = [[0.5F, -0.3F], [0.1F, 0.2F]]
  let h0 : Array[Float] = [0.1F, 0.0F, -0.2F]
  let (hs, _) = gru_sequence_forward(xs, h0, p)
  let bad : Array[Array[Float]] = [[0.0F, 0.0F, 0.0F]]
  let loss = gru_sequence_loss(hs, bad)
  assert_eq(loss, -1.0F)
}

///|
test "gru_sequence_backward: zero d_output -> zero d_xs + zero grad" {
  let p = GruCellParam::new(2, 3, 11UL)
  let xs : Array[Array[Float]] = [[0.5F, -0.3F], [0.1F, 0.2F], [-0.4F, 0.6F]]
  let h0 : Array[Float] = [0.1F, 0.0F, -0.2F]
  let (hs, caches) = gru_sequence_forward(xs, h0, p)
  let target = hs // match exactly so loss is zero
  let (d_xs, d_h0, grad) = gru_sequence_backward(target, target, caches, h0, p)
  assert_eq(d_xs.length(), 3)
  for t in 0..<3 {
    assert_eq(d_xs[t].length(), 2)
    for i in 0..<2 {
      assert_eq(d_xs[t][i], 0.0F)
    }
  }
  assert_eq(d_h0.length(), 3)
  for i in 0..<3 {
    assert_eq(d_h0[i], 0.0F)
    assert_eq(grad.d_b_z[i], 0.0F)
    assert_eq(grad.d_b_r[i], 0.0F)
    assert_eq(grad.d_b_n[i], 0.0F)
  }
}

///|
test "gru_sequence_backward: d_xs + d_h0 + grad shapes" {
  let p = GruCellParam::new(2, 3, 13UL)
  let xs : Array[Array[Float]] = [[0.5F, -0.3F], [0.1F, 0.2F], [-0.4F, 0.6F]]
  let h0 : Array[Float] = [0.1F, 0.0F, -0.2F]
  let (hs, caches) = gru_sequence_forward(xs, h0, p)
  let target : Array[Array[Float]] = [
    [0.0F, 0.0F, 0.0F],
    [0.0F, 0.0F, 0.0F],
    [0.0F, 0.0F, 0.0F],
  ]
  let (d_xs, d_h0, grad) = gru_sequence_backward(hs, target, caches, h0, p)
  assert_eq(d_xs.length(), 3)
  for t in 0..<3 {
    assert_eq(d_xs[t].length(), 2)
  }
  assert_eq(d_h0.length(), 3)
  assert_eq(grad.d_w_z.length(), 3)
  assert_eq(grad.d_w_r.length(), 3)
  assert_eq(grad.d_w_n.length(), 3)
  for i in 0..<3 {
    assert_eq(grad.d_w_z[i].length(), 5)
    assert_eq(grad.d_w_r[i].length(), 5)
    assert_eq(grad.d_w_n[i].length(), 5)
  }
}

///|
test "gru_clip_grad: clamps all values" {
  let p = GruCellParam::new(2, 2, 17UL)
  let grad = GruCellGrad::zero(p)
  grad.d_w_z[0][0] = 5.0F
  grad.d_w_z[0][1] = -5.0F
  grad.d_w_r[1][0] = 3.0F
  grad.d_w_n[0][3] = -10.0F
  grad.d_b_z[0] = 2.0F
  grad.d_b_r[1] = -2.0F
  grad.d_b_n[0] = 7.0F
  gru_clip_grad(grad, 1.0F)
  assert_eq(grad.d_w_z[0][0], 1.0F)
  assert_eq(grad.d_w_z[0][1], -1.0F)
  assert_eq(grad.d_w_r[1][0], 1.0F)
  assert_eq(grad.d_w_n[0][3], -1.0F)
  assert_eq(grad.d_b_z[0], 1.0F)
  assert_eq(grad.d_b_r[1], -1.0F)
  assert_eq(grad.d_b_n[0], 1.0F)
}

///|
test "gru_train_n_steps: loss decreases after K-step SGD" {
  // Train on identity-shift task with d_x = d_target = d_h = 4 so
  // target dimension matches hidden state dimension.
  let p = GruCellParam::new(4, 4, 23UL)
  let (xs, target) = gru_identity_dataset(8, 4, 4, 31UL)
  let h0 : Array[Float] = [0.0F, 0.0F, 0.0F, 0.0F]
  let (hs0, _) = gru_sequence_forward(xs, h0, p)
  let init_loss = gru_sequence_loss(hs0, target)
  let final_loss = gru_train_n_steps(xs, target, h0, p, 60, 0.01F, 1.0F)
  if final_loss >= init_loss {
    fail("loss didn't decrease: init = \{init_loss}, final = \{final_loss}")
  }
}

///|
test "gru_identity_dataset: shapes + cyclic shift" {
  let (xs, target) = gru_identity_dataset(5, 3, 2, 7UL)
  assert_eq(xs.length(), 5)
  assert_eq(target.length(), 5)
  for t in 0..<5 {
    assert_eq(xs[t].length(), 3)
    assert_eq(target[t].length(), 2)
  }
  // target[0] = xs[4] in first min(d_x, d_target)=2 entries.
  for i in 0..<2 {
    assert_eq(target[0][i], xs[4][i])
  }
  // target[3] = xs[2] in first 2 entries.
  for i in 0..<2 {
    assert_eq(target[3][i], xs[2][i])
  }
}
