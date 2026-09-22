// Tests for the surrogate gradient module.

///|
fn approx_eq(a : Float, b : Float, eps : Float) -> Bool {
  let d = if a > b { a - b } else { b - a }
  d <= eps
}

///|
test "fast_sigmoid_surrogate: peak at 0 = 1, symmetric, decays at infinity" {
  let beta = 1.0F
  // σ'(0; β) = 1
  assert_eq(fast_sigmoid_surrogate(0.0F, beta), 1.0F)
  // σ'(x; β) = σ'(-x; β) — check at ±1
  let g_pos = fast_sigmoid_surrogate(1.0F, beta)
  let g_neg = fast_sigmoid_surrogate(-1.0F, beta)
  if !approx_eq(g_pos, g_neg, 0.000001F) {
    fail("σ'(1) = \{g_pos} ≠ σ'(-1) = \{g_neg}")
  }
  // σ'(±1; β=1) = 1 / (1+1)² = 0.25
  if !approx_eq(g_pos, 0.25F, 0.000001F) {
    fail("σ'(1; 1) = \{g_pos}, expected 0.25")
  }
  // σ'(±10; β=1) = 1 / 11² ≈ 0.008264
  let g_far = fast_sigmoid_surrogate(10.0F, beta)
  if g_far > 0.01F {
    fail("σ'(10; 1) = \{g_far}, expected ≈ 0.0083")
  }
}

///|
test "fast_sigmoid_surrogate: β controls width (larger β → narrower)" {
  // σ'(0; β) = 1 regardless of β
  assert_eq(fast_sigmoid_surrogate(0.0F, 1.0F), 1.0F)
  assert_eq(fast_sigmoid_surrogate(0.0F, 5.0F), 1.0F)
  assert_eq(fast_sigmoid_surrogate(0.0F, 100.0F), 1.0F)
  // At x = 1/β, σ'(1/β; β) = 1 / (1 + 1)² = 0.25 — independent of β
  let g_at_inv_beta_lo = fast_sigmoid_surrogate(1.0F, 1.0F)
  let g_at_inv_beta_hi = fast_sigmoid_surrogate(0.2F, 5.0F)
  if !approx_eq(g_at_inv_beta_lo, 0.25F, 0.000001F) {
    fail("σ'(1; 1) = \{g_at_inv_beta_lo}, expected 0.25")
  }
  if !approx_eq(g_at_inv_beta_hi, 0.25F, 0.000001F) {
    fail("σ'(0.2; 5) = \{g_at_inv_beta_hi}, expected 0.25")
  }
  // At x = 2/β, σ' = 1 / 9 ≈ 0.111
  let g_at_2_beta = fast_sigmoid_surrogate(0.4F, 5.0F)
  if !approx_eq(g_at_2_beta, 0.111111F, 0.0001F) {
    fail("σ'(0.4; 5) = \{g_at_2_beta}, expected ≈ 0.111")
  }
}

///|
test "fast_sigmoid_forward: s(0)=0, monotone, saturates at ±1/β" {
  let beta = 2.0F
  // s(0; β) = 0
  assert_eq(fast_sigmoid_forward(0.0F, beta), 0.0F)
  // Symmetry: s(-x) = -s(x)
  let s_pos = fast_sigmoid_forward(1.0F, beta)
  let s_neg = fast_sigmoid_forward(-1.0F, beta)
  if !approx_eq(s_pos, -s_neg, 0.000001F) {
    fail("s(1) = \{s_pos}, s(-1) = \{s_neg}, expected opposite signs")
  }
  // s(1; 2) = 1 / (1+2) = 1/3
  if !approx_eq(s_pos, 0.333333F, 0.0001F) {
    fail("s(1; 2) = \{s_pos}, expected 0.3333")
  }
  // Saturation: s(100; 2) = 100 / 201 ≈ 0.4975 (just below 1/β = 0.5)
  let s_far = fast_sigmoid_forward(100.0F, beta)
  if !approx_eq(s_far, 0.5F, 0.005F) {
    fail("s(100; 2) = \{s_far}, expected ≈ 0.5 (saturating)")
  }
  // s(-100; 2) = -100/201 ≈ -0.4975
  let s_far_neg = fast_sigmoid_forward(-100.0F, beta)
  if !approx_eq(s_far_neg, -0.5F, 0.005F) {
    fail("s(-100; 2) = \{s_far_neg}, expected ≈ -0.5")
  }
}

///|
test "fast_sigmoid_surrogate_array: elementwise consistency with scalar" {
  let beta = 1.5F
  let u_minus_vt : Array[Float] = [-2.0F, -1.0F, -0.5F, 0.0F, 0.5F, 1.0F, 2.0F]
  let arr = fast_sigmoid_surrogate_array(u_minus_vt, beta)
  assert_eq(arr.length(), 7)
  for i in 0..<7 {
    let expected = fast_sigmoid_surrogate(u_minus_vt[i], beta)
    if !approx_eq(arr[i], expected, 0.000001F) {
      fail("arr[\{i}] = \{arr[i]}, scalar = \{expected}")
    }
  }
  // Peak at index 3 (x=0)
  let peak_idx = 3
  for j in 0..<7 {
    if j != peak_idx && arr[j] > arr[peak_idx] {
      fail("non-peak index \{j} (\{arr[j]}) > peak (\{arr[peak_idx]})")
    }
  }
}

///|
test "fast_sigmoid_forward_array: elementwise consistency with scalar" {
  let beta = 0.5F
  let u_minus_vt : Array[Float] = [-3.0F, -1.0F, -0.1F, 0.0F, 0.1F, 1.0F, 3.0F]
  let arr = fast_sigmoid_forward_array(u_minus_vt, beta)
  assert_eq(arr.length(), 7)
  for i in 0..<7 {
    let expected = fast_sigmoid_forward(u_minus_vt[i], beta)
    if !approx_eq(arr[i], expected, 0.000001F) {
      fail("arr[\{i}] = \{arr[i]}, scalar = \{expected}")
    }
  }
  // Antisymmetry: arr[6] = -arr[0], arr[5] = -arr[1], arr[4] = -arr[2]
  // (because u_minus_vt is antisymmetric and s is odd)
  let n = 7
  for i in 0..<(n / 2) {
    let mirror = n - 1 - i
    if !approx_eq(arr[i], -arr[mirror], 0.000001F) {
      fail("antisymmetry at \{i} vs \{mirror}: \{arr[i]} vs \{arr[mirror]}")
    }
  }
}

///|
test "heaviside_step: 0/1 spike, threshold respected" {
  let vt = -50.0F
  // Below threshold → 0
  assert_eq(heaviside_step(-60.0F, vt), 0.0F)
  assert_eq(heaviside_step(-50.0F, vt), 0.0F)  // strictly greater
  // Above threshold → 1
  assert_eq(heaviside_step(-49.999F, vt), 1.0F)
  assert_eq(heaviside_step(-40.0F, vt), 1.0F)
  assert_eq(heaviside_step(0.0F, vt), 1.0F)
}

///|
test "spike_surrogate: hard spike forward + soft gradient backward" {
  let vt = -50.0F
  let beta = 1.0F
  // 4 neurons: far below, just below, just above, far above threshold.
  let u : Array[Float] = [-60.0F, -50.5F, -49.5F, -40.0F]
  let r = spike_surrogate(u, vt, beta)
  assert_eq(r.spike.length(), 4)
  assert_eq(r.grad.length(), 4)
  // Hard forward:
  //   u[0]=-60 → 0, u[1]=-50.5 → 0, u[2]=-49.5 → 1, u[3]=-40 → 1
  assert_eq(r.spike[0], 0.0F)
  assert_eq(r.spike[1], 0.0F)
  assert_eq(r.spike[2], 1.0F)
  assert_eq(r.spike[3], 1.0F)
  // Surrogate backward (u - vt = -10, -0.5, +0.5, +10):
  //   σ'(0; 1) = 1, σ'(-0.5; 1) = σ'(0.5; 1) = 1/2.25 ≈ 0.4444,
  //   σ'(-10; 1) = σ'(10; 1) = 1/121 ≈ 0.00826
  if !approx_eq(r.grad[0], r.grad[3], 0.000001F) {
    fail("σ' at ±10 should be equal: \{r.grad[0]} vs \{r.grad[3]}")
  }
  if !approx_eq(r.grad[1], r.grad[2], 0.000001F) {
    fail("σ' at ±0.5 should be equal: \{r.grad[1]} vs \{r.grad[2]}")
  }
  if !approx_eq(r.grad[1], 0.444444F, 0.0001F) {
    fail("σ' at ±0.5 = \{r.grad[1]}, expected ≈ 0.4444")
  }
  // The gradient near threshold is larger than far from it
  if r.grad[1] <= r.grad[0] {
    fail("σ' near threshold (\{r.grad[1]}) should > σ' far away (\{r.grad[0]})")
  }
}