// optimizer_sgd_test.mbt — SGD + SGD-with-momentum tests (v0.15.0).

///|
test "sgd_update_arrays / step is weight -= lr * grad" {
  let w : Array[Float] = [1.0F, 2.0F, 3.0F]
  let b : Array[Float] = [0.5F, 1.5F]
  let dw : Array[Float] = [0.1F, 0.2F, 0.3F]
  let db : Array[Float] = [0.05F, 0.15F]
  let (w2, b2) = sgd_update_arrays(w, b, dw, db, 0.1F)
  if (w2[0] - 0.99F).abs() > 0.000001F { fail("w[0] expected 0.99, got \{w2[0]}") }
  if (w2[1] - 1.98F).abs() > 0.000001F { fail("w[1] expected 1.98") }
  if (w2[2] - 2.97F).abs() > 0.000001F { fail("w[2] expected 2.97") }
  if (b2[0] - 0.495F).abs() > 0.000001F { fail("b[0] expected 0.495") }
  if (b2[1] - 1.485F).abs() > 0.000001F { fail("b[1] expected 1.485") }
}

///|
test "sgd_update_arrays / zero gradient leaves parameters unchanged" {
  let w : Array[Float] = [1.0F, -2.0F, 0.5F]
  let b : Array[Float] = [3.0F]
  let dw : Array[Float] = [0.0F, 0.0F, 0.0F]
  let db : Array[Float] = [0.0F]
  let (w2, b2) = sgd_update_arrays(w, b, dw, db, 0.5F)
  for i in 0..<w.length() {
    if w2[i] != w[i] { fail("w[\{i}] should be unchanged at \{w[i]}, got \{w2[i]}") }
  }
  if b2[0] != b[0] { fail("b[0] should be unchanged") }
}

///|
test "sgd_update_arrays / does not alias the input" {
  let w : Array[Float] = [1.0F, 2.0F]
  let b : Array[Float] = [3.0F]
  let dw : Array[Float] = [0.5F, 0.5F]
  let db : Array[Float] = [0.1F]
  let (w2, _b2) = sgd_update_arrays(w, b, dw, db, 1.0F)
  // Mutate the result; original must not change.
  w2[0] = 99.0F
  if w[0] != 1.0F { fail("original w[0] must not be aliased, got \{w[0]}") }
}

///|
test "sgd_update_linear / updates fields in a fresh struct" {
  let p = LinearParam::new([1.0F, 2.0F], [0.5F], 2, 1)
  let dw : Array[Float] = [0.1F, 0.1F]
  let db : Array[Float] = [0.2F]
  let p2 = sgd_update_linear(p, dw, db, 0.5F)
  // Original untouched.
  if p.weight[0] != 1.0F { fail("original weight should be 1.0") }
  // New struct has updated values.
  if (p2.weight[0] - 0.95F).abs() > 0.000001F {
    fail("p2.weight[0] expected 0.95, got \{p2.weight[0]}")
  }
  if p2.in_features != 2 || p2.out_features != 1 {
    fail("shape should be preserved")
  }
  if (p2.bias[0] - 0.4F).abs() > 0.000001F {
    fail("p2.bias[0] expected 0.4, got \{p2.bias[0]}")
  }
}

///|
test "sgd_update_conv / updates a Conv2dParam with stride/pad preserved" {
  let p = Conv2dParam::new(
    [0.1F, 0.2F, 0.3F, 0.4F],
    [0.5F, 0.6F],
    2, 1, 2, 2, stride=1, pad=0,
  )
  let dw : Array[Float] = [0.01F, 0.01F, 0.01F, 0.01F]
  let db : Array[Float] = [0.02F, 0.02F]
  let p2 = sgd_update_conv(p, dw, db, 0.1F)
  if p2.c_out != 2 || p2.c_in != 1 || p2.kh != 2 || p2.kw != 2 {
    fail("shape not preserved: (\{p2.c_out},\{p2.c_in},\{p2.kh},\{p2.kw})")
  }
  if p2.stride != 1 || p2.pad != 0 {
    fail("stride/pad not preserved: stride=\{p2.stride}, pad=\{p2.pad}")
  }
  if (p2.weight[0] - 0.099F).abs() > 0.000001F {
    fail("p2.weight[0] expected 0.099, got \{p2.weight[0]}")
  }
}

///|
test "sgd_momentum_update_arrays / first step equals SGD (zero velocity)" {
  // PyTorch convention: v <- momentum * v + g; w <- w - lr * v.
  // First call: v = momentum * 0 + g = g, so w_new = w - lr * g.
  let w : Array[Float] = [1.0F, 2.0F]
  let b : Array[Float] = [0.5F]
  let dw : Array[Float] = [0.4F, 0.4F]
  let db : Array[Float] = [0.2F]
  let state = sgd_momentum_init(2, 1)
  let (w2, b2, _s2) = sgd_momentum_update_arrays(w, b, dw, db, state, 0.5F, 0.9F)
  if (w2[0] - 0.8F).abs() > 0.000001F { fail("w[0] expected 0.8, got \{w2[0]}") }
  if (w2[1] - 1.8F).abs() > 0.000001F { fail("w[1] expected 1.8") }
  if (b2[0] - 0.4F).abs() > 0.000001F { fail("b[0] expected 0.4") }
}

///|
test "sgd_momentum_update_arrays / second step accumulates velocity" {
  // After first step: v = g1 (from initial).
  // Second step with the same gradient g:
  //   v = momentum * g1 + g2 = (momentum + 1) * g
  //   w_new = w_after_first - lr * v
  let w : Array[Float] = [1.0F]
  let b : Array[Float] = [0.5F]
  let dw : Array[Float] = [0.1F]
  let db : Array[Float] = [0.05F]
  let state0 = sgd_momentum_init(1, 1)
  // Step 1: w becomes 1.0 - 0.5 * 0.1 = 0.95, v = 0.1.
  let (w1, b1, s1) = sgd_momentum_update_arrays(w, b, dw, db, state0, 0.5F, 0.9F)
  // Step 2: v = 0.9 * 0.1 + 0.1 = 0.19, w = 0.95 - 0.5 * 0.19 = 0.855.
  let (w2, _b2, _s2) = sgd_momentum_update_arrays(w1, b1, dw, db, s1, 0.5F, 0.9F)
  if (w2[0] - 0.855F).abs() > 0.000001F {
    fail("w[0] expected 0.855, got \{w2[0]}")
  }
}

///|
test "sgd_momentum_update_arrays / zero momentum = vanilla SGD" {
  let w : Array[Float] = [1.0F, 2.0F]
  let b : Array[Float] = [0.5F]
  let dw : Array[Float] = [0.1F, 0.2F]
  let db : Array[Float] = [0.3F]
  let state = sgd_momentum_init(2, 1)
  let (w2, b2, _) = sgd_momentum_update_arrays(w, b, dw, db, state, 0.1F, 0.0F)
  // With momentum=0: v = g, w_new = w - lr * g.
  if (w2[0] - 0.99F).abs() > 0.000001F { fail("w[0] expected 0.99") }
  if (w2[1] - 1.98F).abs() > 0.000001F { fail("w[1] expected 1.98") }
  if (b2[0] - 0.47F).abs() > 0.000001F { fail("b[0] expected 0.47") }
}

///|
test "sgd_momentum_update_linear / preserves LinearParam shape" {
  let p = LinearParam::new([1.0F, 2.0F, 3.0F, 4.0F], [0.0F, 0.0F], 2, 2)
  let dw : Array[Float] = [0.1F, 0.1F, 0.1F, 0.1F]
  let db : Array[Float] = [0.1F, 0.1F]
  let state = sgd_momentum_init(4, 2)
  let (p2, _s2) = sgd_momentum_update_linear(p, dw, db, state, 0.5F, 0.5F)
  if p2.in_features != 2 || p2.out_features != 2 {
    fail("shape should be preserved")
  }
  // First step: v = momentum*0 + 0.1 = 0.1, w_new = 1.0 - 0.5*0.1 = 0.95.
  if (p2.weight[0] - 0.95F).abs() > 0.000001F {
    fail("p2.weight[0] expected 0.95, got \{p2.weight[0]}")
  }
}

///|
test "sgd_loss-decreases / linear regression with constant gradient converges" {
  // Mini training loop: y = 2*x + 1, fit y = w*x + b via SGD.
  // Initial w=0, b=0; lr=0.1; 10 steps.
  // Each step uses the analytical gradient for MSE: dw = -2/N * sum x*(y - w*x - b), db = -2/N * sum (y - w*x - b).
  let n = 4
  let xs : Array[Float] = [1.0F, 2.0F, 3.0F, 4.0F]
  let ys : Array[Float] = [3.0F, 5.0F, 7.0F, 9.0F] // y = 2x + 1
  let mut w = 0.0F
  let mut b = 0.0F
  let lr = 0.05F
  let mut prev_loss = 1000000000.0F
  for _step in 0..<200 {
    // Forward + MSE loss + analytical gradients.
    let mut loss = 0.0F
    let mut dw = 0.0F
    let mut db_acc = 0.0F
    for i in 0..<n {
      let yh = w * xs[i] + b
      let r = ys[i] - yh
      loss = loss + r * r
      dw = dw - 2.0F * xs[i] * r
      db_acc = db_acc - 2.0F * r
    }
    loss = loss / Float::from_int(n)
    dw = dw / Float::from_int(n)
    db_acc = db_acc / Float::from_int(n)
    // Vanilla SGD step.
    let (w_arr, b_arr) = sgd_update_arrays([w], [b], [dw], [db_acc], lr)
    w = w_arr[0]
    b = b_arr[0]
    // Loss must decrease monotonically (MSE is convex, lr small enough).
    if loss > prev_loss + 0.000001F {
      fail("loss should be non-increasing, was \{prev_loss} now \{loss}")
    }
    prev_loss = loss
  }
  // After 200 steps, w should be ~2.0 and b ~1.0.
  if (w - 2.0F).abs() > 0.05F { fail("w should converge to 2.0, got \{w}") }
  if (b - 1.0F).abs() > 0.05F { fail("b should converge to 1.0, got \{b}") }
}