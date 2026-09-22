// optimizer_adamw_test.mbt — AdamW optimiser tests (v0.16.0).

///|
test "adamw_init / zero state, step=0" {
  let s = adamw_init(3, 2)
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
test "adamw_update_arrays / weight_decay=0 matches Adam direction" {
  // First step with weight_decay=0, same gradient as Adam test:
  // y = 2x + 1, init w=0, b=0. Adam and AdamW should produce the
  // same direction (no decoupled decay).
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
  let state0 = adamw_init(1, 1)
  let (w_arr, b_arr, _) = adamw_update_arrays(
    [0.0F], [0.0F], [dw], [db_acc], state0,
    0.1F, 0.9F, 0.999F, 0.00000001F, 0.0F,
  )
  // Same direction as Adam (positive w movement).
  if w_arr[0] <= 0.0F { fail("w should move positive, got \{w_arr[0]}") }
  if b_arr[0] <= 0.0F { fail("b should move positive, got \{b_arr[0]}") }
}

///|
test "adamw_update_arrays / weight_decay shrinks large weights more than small ones" {
  // Two-element weight vector w=[10, 0.1] with weight_decay=0.1.
  // After one step the *larger* weight should shrink by more in absolute
  // terms (the decoupled decay term `lr * weight_decay * w` is linear
  // in `w`). Use zero gradient so the only effect is weight decay.
  let state0 = adamw_init(2, 1)
  let (w, _b, _) = adamw_update_arrays(
    [10.0F, 0.1F], [0.5F], [0.0F, 0.0F], [0.0F],
    state0, 0.1F, 0.9F, 0.999F, 0.00000001F, 0.1F,
  )
  // With zero gradient and step 1: m_hat = 0, v_hat = 0, the update is
  // `-lr * (0 + wd * w) = -lr * wd * w`.
  // w[0] = 10 - 0.1*0.1*10 = 10 - 0.1 = 9.9  (shrink 0.1)
  // w[1] = 0.1 - 0.1*0.1*0.1 = 0.1 - 0.001 = 0.099  (shrink 0.001)
  if (w[0] - 9.9F).abs() > 0.01F {
    fail("w[0] expected ~9.9, got \{w[0]}")
  }
  if (w[1] - 0.099F).abs() > 0.001F {
    fail("w[1] expected ~0.099, got \{w[1]}")
  }
  // The absolute shrink ratio must favour the larger weight.
  let shrink_big = 10.0F - w[0]
  let shrink_small = 0.1F - w[1]
  if shrink_big < shrink_small {
    fail("larger weight should shrink more absolutely: \{shrink_big} vs \{shrink_small}")
  }
}

///|
test "adamw_update_arrays / step counter increments via returned state" {
  let s0 = adamw_init(1, 1)
  let (_, _, s1) = adamw_update_arrays(
    [0.0F], [0.0F], [1.0F], [0.5F], s0,
    0.01F, 0.9F, 0.999F, 0.00000001F, 0.0F,
  )
  let (_, _, s2) = adamw_update_arrays(
    [0.0F], [0.0F], [1.0F], [0.5F], s1,
    0.01F, 0.9F, 0.999F, 0.00000001F, 0.0F,
  )
  let (_, _, s3) = adamw_update_arrays(
    [0.0F], [0.0F], [1.0F], [0.5F], s2,
    0.01F, 0.9F, 0.999F, 0.00000001F, 0.0F,
  )
  if s1.step != 1 { fail("s1.step expected 1, got \{s1.step}") }
  if s2.step != 2 { fail("s2.step expected 2, got \{s2.step}") }
  if s3.step != 3 { fail("s3.step expected 3, got \{s3.step}") }
}

///|
test "adamw_update_arrays / loss converges on y=2x+1 with weight_decay" {
  // AdamW on linear regression with mild weight decay. The parameters
  // should still converge to (w=2, b=1) — the decay is small enough
  // not to dominate the gradient.
  let xs : Array[Float] = [1.0F, 2.0F, 3.0F, 4.0F]
  let ys : Array[Float] = [3.0F, 5.0F, 7.0F, 9.0F]
  let mut w = 0.0F
  let mut b = 0.0F
  let lr = 0.1F
  let wd = 0.001F
  let mut state = adamw_init(1, 1)
  let n_pts = 4
  for _step in 0..<600 {
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
    let (w_arr, b_arr, state2) = adamw_update_arrays(
      [w], [b], [dw], [db_acc], state,
      lr, 0.9F, 0.999F, 0.00000001F, wd,
    )
    w = w_arr[0]
    b = b_arr[0]
    state = state2
  }
  if (w - 2.0F).abs() > 0.2F { fail("w should converge near 2.0, got \{w}") }
  if (b - 1.0F).abs() > 0.2F { fail("b should converge near 1.0, got \{b}") }
}

///|
test "adamw_update_linear / preserves LinearParam shape" {
  let p = LinearParam::new([1.0F, 2.0F, 3.0F, 4.0F], [0.0F, 0.0F], 2, 2)
  let dw : Array[Float] = [0.1F, 0.1F, 0.1F, 0.1F]
  let db : Array[Float] = [0.05F, 0.05F]
  let state0 = adamw_init(4, 2)
  let (p2, _s2) = adamw_update_linear(
    p, dw, db, state0, 0.01F, 0.9F, 0.999F, 0.00000001F, 0.01F,
  )
  if p2.in_features != 2 || p2.out_features != 2 {
    fail("LinearParam shape not preserved")
  }
  if p2.weight.length() != 4 || p2.bias.length() != 2 {
    fail("LinearParam arrays not preserved")
  }
}

///|
test "adamw_update_conv / preserves Conv2dParam shape" {
  let p = Conv2dParam::new(
    [0.1F, 0.2F, 0.3F, 0.4F, 0.5F, 0.6F, 0.7F, 0.8F],
    [0.0F, 0.1F],
    2, 2, 2, 2, stride=1, pad=0,
  )
  let dw : Array[Float] = [0.01F, 0.01F, 0.01F, 0.01F, 0.01F, 0.01F, 0.01F, 0.01F]
  let db : Array[Float] = [0.02F, 0.02F]
  let state0 = adamw_init(8, 2)
  let (p2, _s2) = adamw_update_conv(
    p, dw, db, state0, 0.01F, 0.9F, 0.999F, 0.00000001F, 0.01F,
  )
  if p2.c_out != 2 || p2.c_in != 2 || p2.kh != 2 || p2.kw != 2 {
    fail("Conv2dParam shape not preserved")
  }
  if p2.stride != 1 || p2.pad != 0 {
    fail("stride/pad not preserved")
  }
}