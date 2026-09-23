// stacked_lstm_test.mbt — Tests for multi-layer (stacked) LSTM (v0.32.0).

///|
test "stacked_lstm_new: layer count + per-layer shape" {
  let p = StackedLstmParam::new(2, 3, 4, 42UL)
  assert_eq(p.d_x, 2)
  assert_eq(p.d_h, 3)
  assert_eq(p.n_layers, 4)
  assert_eq(p.layers.length(), 4)
  // Layer 0: d_x=2, d_h=3 -> in_dim = 5.
  assert_eq(p.layers[0].d_x, 2)
  assert_eq(p.layers[0].d_h, 3)
  assert_eq(p.layers[0].w_f[0].length(), 5)
  // Layer l > 0: d_x = d_h = 3, in_dim = 6.
  for l in 1..<4 {
    assert_eq(p.layers[l].d_x, 3)
    assert_eq(p.layers[l].d_h, 3)
    assert_eq(p.layers[l].w_f[0].length(), 6)
  }
}

///|
test "stacked_lstm_forward: output shapes" {
  let p = StackedLstmParam::new(2, 3, 2, 7UL)
  let xs : Array[Array[Float]] = [
    [0.5F, -0.3F],
    [0.1F, 0.2F],
    [-0.4F, 0.6F],
  ]
  let (h0s, c0s) = stacked_lstm_zero_init(2, 3)
  let (hs, cs, cache) = stacked_lstm_forward(xs, h0s, c0s, p)
  assert_eq(hs.length(), 3)
  assert_eq(cs.length(), 3)
  assert_eq(cache.caches.length(), 3)
  for t in 0..<3 {
    assert_eq(hs[t].length(), 2)
    assert_eq(cs[t].length(), 2)
    assert_eq(cache.caches[t].length(), 2)
    for l in 0..<2 {
      assert_eq(hs[t][l].length(), 3)
      assert_eq(cs[t][l].length(), 3)
    }
  }
}

///|
test "stacked_lstm_forward: layer 0 input is xs[t]" {
  // 1-layer stack should behave like a single LSTM.
  // StackedLstmParam adds (l*7+1) to seed for layer l, so seed 4
  // gives layer 0 the same RNG state as LstmCellParam with seed 5.
  let p1 = StackedLstmParam::new(2, 3, 1, 4UL)
  let p_single = LstmCellParam::new(2, 3, 5UL)
  let xs : Array[Array[Float]] = [
    [0.5F, -0.3F],
    [0.1F, 0.2F],
  ]
  let h0 : Array[Float] = [0.0F, 0.0F, 0.0F]
  let c0 : Array[Float] = [0.0F, 0.0F, 0.0F]
  let (h0s, c0s) = stacked_lstm_zero_init(1, 3)
  let (hs, _, _) = stacked_lstm_forward(xs, h0s, c0s, p1)
  let (hs_single, _, _) = lstm_sequence_forward(xs, h0, c0, p_single)
  for t in 0..<2 {
    for i in 0..<3 {
      let diff = if hs[t][0][i] > hs_single[t][i] {
        hs[t][0][i] - hs_single[t][i]
      } else {
        hs_single[t][i] - hs[t][0][i]
      }
      assert_true(diff < 1.0e-5F)
    }
  }
}

///|
test "stacked_lstm_loss: zero on perfect match" {
  let p = StackedLstmParam::new(2, 3, 2, 11UL)
  let xs : Array[Array[Float]] = [[0.5F, -0.3F], [0.1F, 0.2F]]
  let (h0s, c0s) = stacked_lstm_zero_init(2, 3)
  let (hs, _, _) = stacked_lstm_forward(xs, h0s, c0s, p)
  let target : Array[Array[Float]] = [hs[0][1], hs[1][1]]
  let loss = stacked_lstm_sequence_loss(hs, target)
  assert_eq(loss, 0.0F)
}

///|
test "stacked_lstm_loss: -1 on length mismatch" {
  let p = StackedLstmParam::new(2, 3, 2, 11UL)
  let xs : Array[Array[Float]] = [[0.5F, -0.3F], [0.1F, 0.2F]]
  let (h0s, c0s) = stacked_lstm_zero_init(2, 3)
  let (hs, _, _) = stacked_lstm_forward(xs, h0s, c0s, p)
  let bad : Array[Array[Float]] = [[0.0F, 0.0F, 0.0F]]
  let loss = stacked_lstm_sequence_loss(hs, bad)
  assert_eq(loss, -1.0F)
}

///|
test "stacked_lstm_backward: zero d_loss -> zero all grads" {
  let p = StackedLstmParam::new(2, 3, 2, 13UL)
  let xs : Array[Array[Float]] = [[0.5F, -0.3F], [0.1F, 0.2F]]
  let (h0s, c0s) = stacked_lstm_zero_init(2, 3)
  let (hs, _, cache) = stacked_lstm_forward(xs, h0s, c0s, p)
  // target = top-layer output => loss = 0 => all gradients = 0.
  let target : Array[Array[Float]] = [hs[0][1], hs[1][1]]
  let (d_xs, d_h0s, d_c0s, grads) = stacked_lstm_backward(
    hs, target, cache, h0s, c0s, p,
  )
  assert_eq(d_xs.length(), 2)
  for t in 0..<2 {
    for i in 0..<2 {
      assert_eq(d_xs[t][i], 0.0F)
    }
  }
  assert_eq(d_h0s.length(), 2)
  assert_eq(d_c0s.length(), 2)
  for l in 0..<2 {
    for i in 0..<3 {
      assert_eq(d_h0s[l][i], 0.0F)
      assert_eq(d_c0s[l][i], 0.0F)
      assert_eq(grads[l].d_b_f[i], 0.0F)
      assert_eq(grads[l].d_b_i[i], 0.0F)
      assert_eq(grads[l].d_b_c[i], 0.0F)
      assert_eq(grads[l].d_b_o[i], 0.0F)
    }
  }
}

///|
test "stacked_lstm_backward: d_xs length + per-layer grad shape" {
  let p = StackedLstmParam::new(3, 4, 2, 17UL)
  let xs : Array[Array[Float]] = [
    [0.5F, -0.3F, 0.1F],
    [0.1F, 0.2F, -0.5F],
    [-0.4F, 0.6F, 0.2F],
  ]
  let (h0s, c0s) = stacked_lstm_zero_init(2, 4)
  let (hs, _, cache) = stacked_lstm_forward(xs, h0s, c0s, p)
  let target : Array[Array[Float]] = [
    [0.0F, 0.0F, 0.0F, 0.0F],
    [0.0F, 0.0F, 0.0F, 0.0F],
    [0.0F, 0.0F, 0.0F, 0.0F],
  ]
  let (d_xs, d_h0s, d_c0s, grads) = stacked_lstm_backward(
    hs, target, cache, h0s, c0s, p,
  )
  assert_eq(d_xs.length(), 3)
  for t in 0..<3 {
    assert_eq(d_xs[t].length(), 3)
  }
  assert_eq(d_h0s.length(), 2)
  assert_eq(d_c0s.length(), 2)
  assert_eq(grads.length(), 2)
  for l in 0..<2 {
    assert_eq(grads[l].d_w_f.length(), 4)
    if l == 0 {
      assert_eq(grads[l].d_w_f[0].length(), 7) // d_h + d_x = 4 + 3
    } else {
      assert_eq(grads[l].d_w_f[0].length(), 8) // d_h + d_x = 4 + 4
    }
  }
}

///|
test "stacked_lstm_train: loss decreases on identity-shift task" {
  // 2-layer stacked LSTM with d_x = d_h = 3 so target matches.
  let p = StackedLstmParam::new(3, 3, 2, 23UL)
  let (xs, target) = stacked_lstm_identity_dataset(8, 3, 3, 31UL)
  let (h0s, c0s) = stacked_lstm_zero_init(2, 3)
  let (hs0, _, _) = stacked_lstm_forward(xs, h0s, c0s, p)
  let init_loss = stacked_lstm_sequence_loss(hs0, target)
  let final_loss = stacked_lstm_train_n_steps(
    xs, target, h0s, c0s, p, 60, 0.01F, 1.0F,
  )
  if final_loss >= init_loss {
    fail("loss didn't decrease: init = \{init_loss}, final = \{final_loss}")
  }
}

///|
test "stacked_lstm_zero_init: shapes" {
  let (h0s, c0s) = stacked_lstm_zero_init(3, 5)
  assert_eq(h0s.length(), 3)
  assert_eq(c0s.length(), 3)
  for l in 0..<3 {
    assert_eq(h0s[l].length(), 5)
    assert_eq(c0s[l].length(), 5)
    for i in 0..<5 {
      assert_eq(h0s[l][i], 0.0F)
      assert_eq(c0s[l][i], 0.0F)
    }
  }
}
