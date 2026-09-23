// lstm_demo_test.mbt — Tests for LSTM sequence + training.

///|
test "lstm_sequence_forward: output has seq_len hidden states" {
  let param = LstmCellParam::new(2, 3, 42UL)
  let xs : Array[Array[Float]] = [
    [0.1F, 0.2F],
    [0.3F, 0.4F],
    [0.5F, 0.6F],
  ]
  let h0 : Array[Float] = [0.0F, 0.0F, 0.0F]
  let c0 : Array[Float] = [0.0F, 0.0F, 0.0F]
  let (hs, cs, caches) = lstm_sequence_forward(xs, h0, c0, param)
  assert_eq(hs.length(), 3)
  assert_eq(caches.length(), 3)
  assert_eq(cs.length(), 3)
  for t in 0..<3 {
    assert_eq(hs[t].length(), 3)
  }
}

///|
test "lstm_sequence_forward: hidden state changes from initial" {
  // With non-trivial inputs, the hidden states at later times should
  // differ from the initial h0.
  let param = LstmCellParam::new(2, 3, 7UL)
  let xs : Array[Array[Float]] = [
    [0.5F, 0.5F],
    [0.5F, 0.5F],
    [0.5F, 0.5F],
  ]
  let h0 : Array[Float] = [0.0F, 0.0F, 0.0F]
  let c0 : Array[Float] = [0.0F, 0.0F, 0.0F]
  let (hs, _, _) = lstm_sequence_forward(xs, h0, c0, param)
  // hs[2] should differ from h0 in at least one component.
  let mut differ = false
  for i in 0..<3 {
    if hs[2][i].abs() > 1.0e-3F {
      differ = true
    }
  }
  if !differ {
    fail("hs[2] = h0 despite 3 input steps")
  }
}

///|
test "lstm_sequence_loss: zero for identical sequences" {
  let xs : Array[Array[Float]] = [
    [1.0F, 2.0F],
    [3.0F, 4.0F],
  ]
  let loss = lstm_sequence_loss(xs, xs)
  if loss != 0.0F {
    fail("loss = \{loss}, expected 0")
  }
}

///|
test "lstm_sequence_loss: matches manual mean((p-t)²)" {
  let predicted : Array[Array[Float]] = [
    [1.0F, 2.0F],
    [3.0F, 4.0F],
  ]
  let target : Array[Array[Float]] = [
    [2.0F, 3.0F],
    [4.0F, 5.0F],
  ]
  // diffs: [-1,-1,-1,-1] → squared: [1,1,1,1] → mean: 1.0
  let loss = lstm_sequence_loss(predicted, target)
  if (loss - 1.0F).abs() > 1.0e-4F {
    fail("loss = \{loss}, expected 1.0")
  }
}

///|
test "lstm_sequence_backward: gradient shapes match inputs" {
  let param = LstmCellParam::new(2, 3, 5UL)
  let xs : Array[Array[Float]] = [
    [0.1F, 0.2F],
    [0.3F, 0.4F],
    [0.5F, 0.6F],
  ]
  let target : Array[Array[Float]] = [
    [0.0F, 0.0F, 0.0F],
    [0.0F, 0.0F, 0.0F],
    [0.0F, 0.0F, 0.0F],
  ]
  let h0 : Array[Float] = [0.0F, 0.0F, 0.0F]
  let c0 : Array[Float] = [0.0F, 0.0F, 0.0F]
  let (hs, _, caches) = lstm_sequence_forward(xs, h0, c0, param)
  let (d_xs, d_h0, d_c0, grad) = lstm_sequence_backward(
    hs, target, caches, h0, c0, param,
  )
  assert_eq(d_xs.length(), 3)
  for t in 0..<3 {
    assert_eq(d_xs[t].length(), 2)
  }
  assert_eq(d_h0.length(), 3)
  assert_eq(d_c0.length(), 3)
  assert_eq(grad.d_w_f.length(), 3)
  assert_eq(grad.d_b_f.length(), 3)
}

///|
test "lstm_sequence_backward: loss decreases after SGD step" {
  // Train on the identity-shift task: target[t] = xs[(t-1) mod n].
  // After K steps of SGD, the MSE loss should drop noticeably.
  // Use d_x = d_h so target dimension matches hidden state dimension.
  let param = LstmCellParam::new(4, 4, 11UL)
  let (xs, target) = lstm_identity_dataset(8, 4, 17UL)
  let h0 : Array[Float] = [0.0F, 0.0F, 0.0F, 0.0F]
  let c0 : Array[Float] = [0.0F, 0.0F, 0.0F, 0.0F]
  // Initial loss.
  let (hs0, _, _) = lstm_sequence_forward(xs, h0, c0, param)
  let init_loss = lstm_sequence_loss(hs0, target)
  // 50 SGD steps with small lr + clipping.
  let final_loss = lstm_train_n_steps(
    xs, target, h0, c0, param, 50, 0.01F, 1.0F,
  )
  if final_loss >= init_loss {
    fail("loss didn't decrease: init = \{init_loss}, final = \{final_loss}")
  }
}

///|
test "lstm_clip_grad: bounds gradient magnitudes" {
  let param = LstmCellParam::new(2, 2, 99UL)
  let grad = LstmCellGrad::zero(param)
  // Set some very large gradients.
  grad.d_w_f[0][0] = 100.0F
  grad.d_b_f[0] = -50.0F
  lstm_clip_grad(grad, 1.0F)
  if grad.d_w_f[0][0] != 1.0F {
    fail("d_w_f[0][0] = \{grad.d_w_f[0][0]}, expected 1.0")
  }
  if grad.d_b_f[0] != -1.0F {
    fail("d_b_f[0] = \{grad.d_b_f[0]}, expected -1.0")
  }
}

///|
test "lstm_identity_dataset: target[t] is lag-1 of xs (cyclic)" {
  let (xs, target) = lstm_identity_dataset(5, 2, 23UL)
  assert_eq(xs.length(), 5)
  assert_eq(target.length(), 5)
  for t in 0..<5 {
    assert_eq(xs[t].length(), 2)
    assert_eq(target[t].length(), 2)
  }
  // target[0] should be the "previous" input (which we wrap to xs[0]
  // in the cyclic implementation, or xs[0] itself).
  // For t = 1: target[1] should equal xs[0].
  for i in 0..<2 {
    if target[1][i] != xs[0][i] {
      fail("target[1][\{i}] != xs[0][\{i}]")
    }
  }
  // For t >= 2: target[t] should equal xs[t-1].
  for t in 2..<5 {
    for i in 0..<2 {
      if target[t][i] != xs[t - 1][i] {
        fail("target[\{t}][\{i}] != xs[\{t - 1}][\{i}]")
      }
    }
  }
}

///|
test "lstm_train_n_steps: returns final loss (finite)" {
  let param = LstmCellParam::new(2, 2, 31UL)
  let (xs, target) = lstm_identity_dataset(5, 2, 41UL)
  let h0 : Array[Float] = [0.0F, 0.0F]
  let c0 : Array[Float] = [0.0F, 0.0F]
  let loss = lstm_train_n_steps(xs, target, h0, c0, param, 5, 0.001F, 1.0F)
  if loss != loss {
    fail("final loss is NaN")
  }
}