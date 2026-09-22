// optimizer_rmsprop_test.mbt — RMSprop optimiser tests (v0.16.1).

///|
test "rmsprop_init / zero state, no step counter" {
  let s = rmsprop_init(3, 2)
  for i in 0..<3 {
    if s.v_w[i] != 0.0F { fail("v_w[\{i}] should be 0") }
  }
  for i in 0..<2 {
    if s.v_b[i] != 0.0F { fail("v_b[\{i}] should be 0") }
  }
}

///|
test "rmsprop_update_arrays / first step moves parameter in negative-gradient direction" {
  // RMSprop doesn't have a sign change for the first step because
  // alpha=0.9 default and grad > 0 means v stays close to g^2, and the
  // update is -lr * g / sqrt(g^2 + eps). For tiny eps and constant g,
  // this is approximately -lr * sign(g).
  let s0 = rmsprop_init(1, 1)
  let (w, b, _) = rmsprop_update_arrays(
    [1.0F], [0.5F], [0.1F], [0.05F], s0, 0.01F, 0.9F, 0.00000001F,
  )
  // gradient is positive; weight should decrease.
  if w[0] >= 1.0F { fail("w should decrease (positive gradient), got \{w[0]}") }
  if b[0] >= 0.5F { fail("b should decrease, got \{b[0]}") }
}

///|
test "rmsprop_update_arrays / alpha=0 behaves like raw gradient sign" {
  // With alpha=0: v = g^2 exactly, so update = -lr * g / sqrt(g^2 + eps).
  // For eps=1e-8 and constant gradient g=0.1:
  //   v = 0.01, sqrt(v) = 0.1, so update = -lr * 0.1 / (0.1 + 1e-8) ≈ -lr.
  // So w[0] should be approximately 1.0 - lr.
  let s0 = rmsprop_init(1, 1)
  let (w, _, _) = rmsprop_update_arrays(
    [1.0F], [0.5F], [0.1F], [0.1F], s0, 0.1F, 0.0F, 0.00000001F,
  )
  if (w[0] - 0.9F).abs() > 0.001F {
    fail("alpha=0 first-step w should be ~0.9, got \{w[0]}")
  }
}

///|
test "rmsprop_update_arrays / large gradient gets shrunk by adaptive scaling" {
  // On the second step, the velocity buffer is non-zero, so the
  // effective step magnitude adapts to gradient history. A spike in
  // |g| inflates v only partially (alpha * v_old), keeping the step
  // size bounded.
  let s0 = rmsprop_init(1, 1)
  // Step 1: gradient = 0.1.
  let (w1, _, s1) = rmsprop_update_arrays(
    [1.0F], [0.5F], [0.1F], [0.05F], s0, 0.01F, 0.9F, 0.00000001F,
  )
  // Step 2: gradient = 10 (10x spike).
  let (w2, _, _) = rmsprop_update_arrays(
    w1, [0.5F], [10.0F], [5.0F], s1, 0.01F, 0.9F, 0.00000001F,
  )
  // Step 2 update magnitude:
  //   v_new = 0.9 * v_old + 0.1 * 100 = 0.9 * 0.01 + 10 = 10.009
  //   update = 0.01 * 10 / sqrt(10.009) ≈ 0.0316
  // So w should drop by ~0.0316 (not 0.1, which a naive gradient step
  // would have given).
  let drop = w1[0] - w2[0]
  if drop > 0.1F {
    fail("RMSprop should clip large gradient step, but drop was \{drop}")
  }
  if drop < 0.001F {
    fail("step should still be non-trivial, drop=\{drop}")
  }
}

///|
test "rmsprop_update_arrays / converges on y=2x+1 over 600 steps" {
  let xs : Array[Float] = [1.0F, 2.0F, 3.0F, 4.0F]
  let ys : Array[Float] = [3.0F, 5.0F, 7.0F, 9.0F]
  let mut w = 0.0F
  let mut b = 0.0F
  let lr = 0.05F
  let mut state = rmsprop_init(1, 1)
  let n_pts = 4
  let mut initial_loss = 0.0F
  let mut final_loss = 0.0F
  for step_idx in 0..<600 {
    let mut loss = 0.0F
    let mut dw = 0.0F
    let mut db_acc = 0.0F
    for i in 0..<n_pts {
      let r = ys[i] - (w * xs[i] + b)
      loss = loss + r * r
      dw = dw - 2.0F * xs[i] * r
      db_acc = db_acc - 2.0F * r
    }
    loss = loss / Float::from_int(n_pts)
    dw = dw / Float::from_int(n_pts)
    db_acc = db_acc / Float::from_int(n_pts)
    let (w_arr, b_arr, state2) = rmsprop_update_arrays(
      [w], [b], [dw], [db_acc], state, lr, 0.9F, 0.00000001F,
    )
    w = w_arr[0]
    b = b_arr[0]
    state = state2
    if step_idx == 0 { initial_loss = loss }
    if step_idx == 599 { final_loss = loss }
  }
  if final_loss > initial_loss * 0.1F {
    fail("loss should drop by at least 10x: initial=\{initial_loss} final=\{final_loss}")
  }
  if (w - 2.0F).abs() > 0.2F { fail("w should converge near 2.0, got \{w}") }
  if (b - 1.0F).abs() > 0.2F { fail("b should converge near 1.0, got \{b}") }
}

///|
test "rmsprop_update_linear / preserves LinearParam shape" {
  let p = LinearParam::new([1.0F, 2.0F, 3.0F, 4.0F], [0.0F, 0.0F], 2, 2)
  let dw : Array[Float] = [0.1F, 0.1F, 0.1F, 0.1F]
  let db : Array[Float] = [0.05F, 0.05F]
  let state0 = rmsprop_init(4, 2)
  let (p2, _s2) = rmsprop_update_linear(p, dw, db, state0, 0.01F, 0.9F, 0.00000001F)
  if p2.in_features != 2 || p2.out_features != 2 {
    fail("LinearParam shape not preserved")
  }
  if p2.weight.length() != 4 || p2.bias.length() != 2 {
    fail("LinearParam arrays not preserved")
  }
}

///|
test "rmsprop_update_conv / preserves Conv2dParam shape" {
  let p = Conv2dParam::new(
    [0.1F, 0.2F, 0.3F, 0.4F, 0.5F, 0.6F, 0.7F, 0.8F],
    [0.0F, 0.1F],
    2, 2, 2, 2, stride=1, pad=0,
  )
  let dw : Array[Float] = [0.01F, 0.01F, 0.01F, 0.01F, 0.01F, 0.01F, 0.01F, 0.01F]
  let db : Array[Float] = [0.02F, 0.02F]
  let state0 = rmsprop_init(8, 2)
  let (p2, _s2) = rmsprop_update_conv(p, dw, db, state0, 0.01F, 0.9F, 0.00000001F)
  if p2.c_out != 2 || p2.c_in != 2 || p2.kh != 2 || p2.kw != 2 {
    fail("Conv2dParam shape not preserved")
  }
  if p2.stride != 1 || p2.pad != 0 {
    fail("stride/pad not preserved")
  }
}