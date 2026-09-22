// optimizer_adagrad_test.mbt — AdaGrad optimiser tests (v0.16.2).

///|
test "adagrad_init / zero state" {
  let s = adagrad_init(3, 2)
  for i in 0..<3 {
    if s.v_w[i] != 0.0F { fail("v_w[\{i}] should be 0") }
  }
  for i in 0..<2 {
    if s.v_b[i] != 0.0F { fail("v_b[\{i}] should be 0") }
  }
}

///|
test "adagrad_update_arrays / first step moves in negative-gradient direction" {
  let s0 = adagrad_init(1, 1)
  let (w, b, _) = adagrad_update_arrays(
    [1.0F], [0.5F], [0.1F], [0.05F], s0, 0.01F, 0.00000001F,
  )
  // positive gradient → weight decreases.
  if w[0] >= 1.0F { fail("w should decrease (positive gradient), got \{w[0]}") }
  if b[0] >= 0.5F { fail("b should decrease, got \{b[0]}") }
}

///|
test "adagrad_update_arrays / v accumulates g^2 monotonically (no EMA decay)" {
  // Step 1: g = 0.1, v_after = 0.01.
  // Step 2: g = 0.1, v_after = 0.02 (NOT 0.01*alpha + ...).
  // This is the key AdaGrad property: v is a sum, not an EMA.
  let s0 = adagrad_init(1, 1)
  let (w1, _b1, s1) = adagrad_update_arrays(
    [1.0F], [0.5F], [0.1F], [0.05F], s0, 0.01F, 0.00000001F,
  )
  if (s1.v_w[0] - 0.01F).abs() > 0.000001F {
    fail("v_w after step 1 should be 0.01, got \{s1.v_w[0]}")
  }
  let (w2, _b2, s2) = adagrad_update_arrays(
    w1, [0.5F], [0.1F], [0.05F], s1, 0.01F, 0.00000001F,
  )
  if (s2.v_w[0] - 0.02F).abs() > 0.000001F {
    fail("v_w after step 2 should be 0.02 (cumulative, not EMA), got \{s2.v_w[0]}")
  }
  // The second step's effective lr is smaller because v grew:
  //   step 1: lr * 0.1 / sqrt(0.01) = 0.01 * 0.1 / 0.1 = 0.01
  //   step 2: lr * 0.1 / sqrt(0.02) ≈ 0.01 * 0.1 / 0.1414 ≈ 0.00707
  let drop1 = 1.0F - w1[0]
  let drop2 = w1[0] - w2[0]
  if drop2 > drop1 {
    fail("second step should shrink (AdaGrad shrinks effective lr), drop1=\{drop1} drop2=\{drop2}")
  }
}

///|
test "adagrad_update_arrays / sparse-feature style: large initial step, smaller later" {
  // Verify the per-parameter learning rate monotonically shrinks as v
  // accumulates. With constant gradient g=1.0 the effective per-step
  // movement at step t is lr / sqrt(t).
  let s0 = adagrad_init(1, 1)
  let mut w = 1.0F
  let mut s = s0
  let lr = 0.1F
  let g = 1.0F
  let mut prev_drop = 1000000000.0F
  for step in 1..<10 {
    let (w_new, _, s_new) = adagrad_update_arrays(
      [w], [0.0F], [g], [0.0F], s, lr, 0.00000001F,
    )
    w = w_new[0]
    s = s_new
    let drop = g * lr / sqrtf(Float::from_int(step))
    let actual_drop = g * lr / (sqrtf(s_new.v_w[0]) + 0.00000001F)
    if (drop - actual_drop).abs() > 0.001F {
      fail("step \{step}: expected drop ~\{drop}, got \{actual_drop}")
    }
    // effective per-step drop should monotonically shrink.
    if actual_drop >= prev_drop {
      fail("step \{step}: drop should shrink, was \{prev_drop} now \{actual_drop}")
    }
    prev_drop = actual_drop
  }
}

///|
test "adagrad_update_arrays / converges on y=2x+1 over many steps" {
  // AdaGrad's effective lr shrinks so convergence is slower than Adam,
  // but it should still drive w and b toward their targets.
  let xs : Array[Float] = [1.0F, 2.0F, 3.0F, 4.0F]
  let ys : Array[Float] = [3.0F, 5.0F, 7.0F, 9.0F]
  let mut w = 0.0F
  let mut b = 0.0F
  let lr = 0.5F
  let mut state = adagrad_init(1, 1)
  let n_pts = 4
  for _step in 0..<1500 {
    let mut dw = 0.0F
    let mut db_acc = 0.0F
    for i in 0..<n_pts {
      let r = ys[i] - (w * xs[i] + b)
      dw = dw - 2.0F * xs[i] * r
      db_acc = db_acc - 2.0F * r
    }
    dw = dw / Float::from_int(n_pts)
    db_acc = db_acc / Float::from_int(n_pts)
    let (w_arr, b_arr, state2) = adagrad_update_arrays(
      [w], [b], [dw], [db_acc], state, lr, 0.00000001F,
    )
    w = w_arr[0]
    b = b_arr[0]
    state = state2
  }
  if (w - 2.0F).abs() > 0.3F { fail("w should converge near 2.0, got \{w}") }
  if (b - 1.0F).abs() > 0.3F { fail("b should converge near 1.0, got \{b}") }
}

///|
test "adagrad_update_linear / preserves LinearParam shape" {
  let p = LinearParam::new([1.0F, 2.0F, 3.0F, 4.0F], [0.0F, 0.0F], 2, 2)
  let dw : Array[Float] = [0.1F, 0.1F, 0.1F, 0.1F]
  let db : Array[Float] = [0.05F, 0.05F]
  let state0 = adagrad_init(4, 2)
  let (p2, _s2) = adagrad_update_linear(p, dw, db, state0, 0.01F, 0.00000001F)
  if p2.in_features != 2 || p2.out_features != 2 {
    fail("LinearParam shape not preserved")
  }
  if p2.weight.length() != 4 || p2.bias.length() != 2 {
    fail("LinearParam arrays not preserved")
  }
}

///|
test "adagrad_update_conv / preserves Conv2dParam shape" {
  let p = Conv2dParam::new(
    [0.1F, 0.2F, 0.3F, 0.4F, 0.5F, 0.6F, 0.7F, 0.8F],
    [0.0F, 0.1F],
    2, 2, 2, 2, stride=1, pad=0,
  )
  let dw : Array[Float] = [0.01F, 0.01F, 0.01F, 0.01F, 0.01F, 0.01F, 0.01F, 0.01F]
  let db : Array[Float] = [0.02F, 0.02F]
  let state0 = adagrad_init(8, 2)
  let (p2, _s2) = adagrad_update_conv(p, dw, db, state0, 0.01F, 0.00000001F)
  if p2.c_out != 2 || p2.c_in != 2 || p2.kh != 2 || p2.kw != 2 {
    fail("Conv2dParam shape not preserved")
  }
  if p2.stride != 1 || p2.pad != 0 {
    fail("stride/pad not preserved")
  }
}