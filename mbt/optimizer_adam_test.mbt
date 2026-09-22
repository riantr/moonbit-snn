// optimizer_adam_test.mbt — Adam optimiser tests (v0.15.1).

///|
test "adam_init / zero state, step=0" {
  let s = adam_init(3, 2)
  for i in 0..<3 {
    if s.m_w[i] != 0.0F { fail("m_w[\{i}] should be 0") }
    if s.v_w[i] != 0.0F { fail("v_w[\{i}] should be 0") }
  }
  for i in 0..<2 {
    if s.m_b[i] != 0.0F { fail("m_b[\{i}] should be 0") }
    if s.v_b[i] != 0.0F { fail("v_b[\{i}] should be 0") }
  }
  if s.step != 0 { fail("initial step should be 0, got \{s.step}") }
}

///|
test "adam_next_step / returns state.step + 1 without mutation" {
  let s = adam_init(1, 1)
  let t1 = adam_next_step(s)
  // `state.step` is read-only; passing `s` again gives the same +1.
  let t2 = adam_next_step(s)
  if t1 != 1 { fail("first next_step should be 1, got \{t1}") }
  if t2 != 1 { fail("adam_next_step does not mutate state, t2 should still be 1, got \{t2}") }
  // Original state.step must not be mutated.
  if s.step != 0 { fail("state.step should remain 0, got \{s.step}") }
}

///|
test "adam_update_arrays / first step reduces loss for linear regression" {
  // y = 2x + 1, init w=0 b=0, single Adam step at lr=0.1.
  // Gradient at (w=0, b=0): dw = -2/N * sum x*(y - 0 - 0) = -2/N * sum x*y
  //                        db = -2/N * sum y
  // For x=[1,2,3,4], y=[3,5,7,9]: dw = -2/4 * (1*3+2*5+3*7+4*9) = -2/4 * 80 = -40.
  //                                db = -2/4 * (3+5+7+9) = -12.
  let xs : Array[Float] = [1.0F, 2.0F, 3.0F, 4.0F]
  let ys : Array[Float] = [3.0F, 5.0F, 7.0F, 9.0F]
  let mut dw = 0.0F
  let mut db_acc = 0.0F
  for i in 0..<4 {
    dw = dw - 2.0F * xs[i] * ys[i]
    db_acc = db_acc - 2.0F * ys[i]
  }
  dw = dw / 4.0F
  db_acc = db_acc / 4.0F
  let state0 = adam_init(1, 1)
  let (w_arr, b_arr, state1) = adam_update_arrays(
    [0.0F], [0.0F], [dw], [db_acc], state0,
    0.1F, 0.9F, 0.999F, 0.00000001F,
  )
  // After 1 step w should move in the *positive* direction (gradient is
  // negative; Adam step is param - lr * m_hat / (sqrt(v_hat) + eps)).
  if w_arr[0] <= 0.0F { fail("w should move positive, got \{w_arr[0]}") }
  if b_arr[0] <= 0.0F { fail("b should move positive, got \{b_arr[0]}") }
  if state1.step != 1 { fail("step should be 1, got \{state1.step}") }
}

///|
test "adam_update_arrays / loss decreases over 600 Adam steps on linear regression" {
  // Fit y = 2x + 1 with Adam (lr=0.1, beta1=0.9, beta2=0.999).
  // Adam's adaptive moments mean the loss curve is NOT strictly monotone
  // — it can oscillate locally — but the long-term trend is downward and
  // the parameters converge to (w=2, b=1).
  let xs : Array[Float] = [1.0F, 2.0F, 3.0F, 4.0F]
  let ys : Array[Float] = [3.0F, 5.0F, 7.0F, 9.0F]
  let mut w = 0.0F
  let mut b = 0.0F
  let lr = 0.1F
  let mut state = adam_init(1, 1)
  let mut initial_loss = 0.0F
  let mut final_loss = 0.0F
  let n_pts = 4
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
    let (w_arr, b_arr, state2) = adam_update_arrays(
      [w], [b], [dw], [db_acc], state,
      lr, 0.9F, 0.999F, 0.00000001F,
    )
    w = w_arr[0]
    b = b_arr[0]
    state = state2
    if step_idx == 0 { initial_loss = loss }
    if step_idx == 599 { final_loss = loss }
  }
  // Final loss must be MUCH smaller than initial (10x reduction).
  if final_loss > initial_loss * 0.1F {
    fail("loss should drop by at least 10x: initial=\{initial_loss} final=\{final_loss}")
  }
  if (w - 2.0F).abs() > 0.15F { fail("w should converge to ~2.0, got \{w}") }
  if (b - 1.0F).abs() > 0.15F { fail("b should converge to ~1.0, got \{b}") }
}

///|
test "adam_update_arrays / step counter increments monotonically" {
  let state0 = adam_init(1, 1)
  let (_, _, s1) = adam_update_arrays(
    [0.0F], [0.0F], [1.0F], [0.5F], state0, 0.01F, 0.9F, 0.999F, 0.00000001F,
  )
  let (_, _, s2) = adam_update_arrays(
    [0.0F], [0.0F], [1.0F], [0.5F], s1, 0.01F, 0.9F, 0.999F, 0.00000001F,
  )
  let (_, _, s3) = adam_update_arrays(
    [0.0F], [0.0F], [1.0F], [0.5F], s2, 0.01F, 0.9F, 0.999F, 0.00000001F,
  )
  if s1.step != 1 { fail("step 1, got \{s1.step}") }
  if s2.step != 2 { fail("step 2, got \{s2.step}") }
  if s3.step != 3 { fail("step 3, got \{s3.step}") }
}

///|
test "adam_update_arrays / zero gradient yields zero first moment" {
  // m = beta1 * 0 + (1 - beta1) * 0 = 0; v likewise; step is zero.
  // Step is param - lr * 0 / (sqrt(0) + eps) = param - 0 = param.
  let state0 = adam_init(2, 1)
  let (w, b, s1) = adam_update_arrays(
    [1.0F, -2.0F], [0.5F], [0.0F, 0.0F], [0.0F],
    state0, 0.1F, 0.9F, 0.999F, 0.00000001F,
  )
  // bias-correction denominators bc1=1-beta1^t=0.1, bc2=1-beta2^t=0.001.
  // v_hat=0/bc2=0, sqrt(0)+eps=eps, m_hat=0/bc1=0.
  // update = 1.0 - 0.1 * 0/eps = 1.0.
  if (w[0] - 1.0F).abs() > 0.000001F {
    fail("w[0] should be unchanged at 1.0, got \{w[0]}")
  }
  if (w[1] - -2.0F).abs() > 0.000001F {
    fail("w[1] should be unchanged at -2.0, got \{w[1]}")
  }
  if (b[0] - 0.5F).abs() > 0.000001F { fail("b[0] should be unchanged at 0.5") }
  if s1.step != 1 { fail("step should be 1") }
}

///|
test "adam_update_linear / preserves LinearParam shape" {
  let p = LinearParam::new([1.0F, 2.0F, 3.0F, 4.0F], [0.0F, 0.0F], 2, 2)
  let dw : Array[Float] = [0.1F, 0.1F, 0.1F, 0.1F]
  let db : Array[Float] = [0.05F, 0.05F]
  let state0 = adam_init(4, 2)
  let (p2, _s2) = adam_update_linear(
    p, dw, db, state0, 0.01F, 0.9F, 0.999F, 0.00000001F,
  )
  if p2.in_features != 2 || p2.out_features != 2 {
    fail("LinearParam shape not preserved")
  }
  if p2.weight.length() != 4 || p2.bias.length() != 2 {
    fail("LinearParam arrays not preserved")
  }
}

///|
test "adam_update_conv / preserves Conv2dParam shape" {
  let p = Conv2dParam::new(
    [0.1F, 0.2F, 0.3F, 0.4F, 0.5F, 0.6F, 0.7F, 0.8F],
    [0.0F, 0.1F],
    2, 2, 2, 2, stride=1, pad=0,
  )
  let dw : Array[Float] = [0.01F, 0.01F, 0.01F, 0.01F, 0.01F, 0.01F, 0.01F, 0.01F]
  let db : Array[Float] = [0.02F, 0.02F]
  let state0 = adam_init(8, 2)
  let (p2, _s2) = adam_update_conv(
    p, dw, db, state0, 0.01F, 0.9F, 0.999F, 0.00000001F,
  )
  if p2.c_out != 2 || p2.c_in != 2 || p2.kh != 2 || p2.kw != 2 {
    fail("Conv2dParam shape not preserved")
  }
  if p2.stride != 1 || p2.pad != 0 {
    fail("stride/pad not preserved")
  }
}