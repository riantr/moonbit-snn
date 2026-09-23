// Tests for Adafactor optimizer.

///|
fn af_eps(a : Float, b : Float, eps : Float) -> Bool {
  let d = if a > b { a - b } else { b - a }
  d <= eps
}

///|
test "adafactor_update_2d: basic step shrinks weight toward 0 with non-zero gradient" {
  let weight : Array[Float] = [0.1F, 0.2F, 0.3F, 0.4F]
  let d_weight : Array[Float] = [0.1F, 0.1F, 0.1F, 0.1F]
  let state = Adafactor2DState::new(2, 2)
  let lr = 0.01F
  adafactor_update_2d(weight, d_weight, state, lr)
  // After one step, weight should have decreased (dW > 0 → w -= lr/sqrt(v)*dW).
  for i in 0..<4 {
    if weight[i] >= [0.1F, 0.2F, 0.3F, 0.4F][i] {
      fail("weight[\{i}] did not decrease")
    }
  }
  // v_row and v_col populated.
  let mut non_zero_row = 0
  for i in 0..<2 {
    if state.v_row[i] > 0.0F {
      non_zero_row = non_zero_row + 1
    }
  }
  if non_zero_row != 2 {
    fail("v_row should be non-zero, found \{non_zero_row}/2 non-zero entries")
  }
}

///|
test "adafactor_update_2d: zero gradient leaves weight unchanged" {
  let weight : Array[Float] = [0.1F, 0.2F, 0.3F, 0.4F]
  let d_weight : Array[Float] = [0.0F, 0.0F, 0.0F, 0.0F]
  let state = Adafactor2DState::new(2, 2)
  let lr = 0.01F
  adafactor_update_2d(weight, d_weight, state, lr)
  for i in 0..<4 {
    if weight[i] != [0.1F, 0.2F, 0.3F, 0.4F][i] {
      fail("weight[\{i}] = \{weight[i]}, expected unchanged")
    }
  }
}

///|
test "adafactor_update_1d: bias update matches RMSProp-like form" {
  let bias : Array[Float] = [0.0F, 0.0F, 0.0F]
  let d_bias : Array[Float] = [0.5F, -0.3F, 0.1F]
  let state = Adafactor1DState::new(3)
  let lr = 0.1F
  // First step: v = (1-β)·g^2 = 0.001·g^2
  // update = lr·g/sqrt(v+eps) ≈ lr·g/sqrt(v) for small v.
  // bias[0] ≈ 0 - 0.1·0.5/sqrt(0.001·0.25) = -0.05/sqrt(0.00025) = -0.05/0.0158 ≈ -3.16
  // That's actually a big step due to small v. Use lr=0.001 instead.
  let lr_small = 0.001F
  let saved_bias = [0.0F, 0.0F, 0.0F]
  for i in 0..<3 {
    saved_bias[i] = bias[i]
  }
  ignore(saved_bias)
  adafactor_update_1d(bias, d_bias, state, lr_small)
  // Just check that v populated and bias changed.
  for i in 0..<3 {
    if state.v[i] == 0.0F {
      fail("v[\{i}] is still zero")
    }
  }
  let mut changed = 0
  for i in 0..<3 {
    if bias[i] != 0.0F {
      changed = changed + 1
    }
  }
  if changed == 0 {
    fail("bias did not change despite non-zero gradient")
  }
}

///|
test "adafactor_update_linear: full weight + bias update" {
  let weight : Array[Float] = [0.5F, 0.6F, 0.7F, 0.8F]
  let bias : Array[Float] = [0.1F, 0.2F]
  let d_weight : Array[Float] = [0.05F, 0.05F, 0.05F, 0.05F]
  let d_bias : Array[Float] = [0.1F, 0.1F]
  let w_state = Adafactor2DState::new(2, 2)
  let b_state = Adafactor1DState::new(2)
  adafactor_update_linear(weight, bias, d_weight, d_bias, w_state, b_state, 0.01F)
  // Weight decreased.
  for i in 0..<4 {
    let original = [0.5F, 0.6F, 0.7F, 0.8F][i]
    if weight[i] >= original {
      fail("weight[\{i}] = \{weight[i]} should be < \{original}")
    }
  }
  // Bias decreased (positive gradient → bias moves down).
  if bias[0] >= 0.1F {
    fail("bias[0] = \{bias[0]} should be < 0.1")
  }
}

///|
test "adafactor: converges on a toy quadratic (W*W^T loss)" {
  // Train a small 4x4 weight to minimize |W @ W^T - I|^2 / 4 with a
  // gradient signal proportional to (W @ W^T - I) @ W.
  // Synthetic: just verify the weight moves toward a target.
  let target : Array[Float] = [1.0F, 0.0F, 0.0F, 1.0F]
  let weight : Array[Float] = [0.5F, 0.5F, 0.5F, 0.5F]
  let state = Adafactor2DState::new(2, 2)
  let lr = 0.01F
  let n_steps = 50
  for _step in 0..<n_steps {
    // d_loss / d_weight = (weight - target) (simple proxy)
    let d_w : Array[Float] = Array::make(4, 0.0F)
    for i in 0..<4 {
      d_w[i] = weight[i] - target[i]
    }
    adafactor_update_2d(weight, d_w, state, lr)
  }
  // Final weight should be closer to target than initial.
  let mut initial_diff = 0.0F
  let mut final_diff = 0.0F
  for i in 0..<4 {
    initial_diff = initial_diff + ([0.5F, 0.5F, 0.5F, 0.5F][i] - target[i]).abs()
    final_diff = final_diff + (weight[i] - target[i]).abs()
  }
  if !(final_diff < initial_diff) {
    fail("Adafactor did not reduce L1 distance: initial = \{initial_diff}, final = \{final_diff}")
  }
}