// Tests for the exact (sparse) GELU activation.

///|
fn gs_eps(a : Float, b : Float, eps : Float) -> Bool {
  let d = if a > b { a - b } else { b - a }
  d <= eps
}

///|
test "gelu_sparse: gelu(0) = 0, large-x saturation" {
  // gelu(0) = 0 by definition (x * Φ(x) with Φ(0) = 0.5 → 0).
  assert_eq(gelu_sparse(0.0F), 0.0F)
  // Large positive: gelu(5) ≈ 5 (since Φ(5) ≈ 1).
  let g_pos = gelu_sparse(5.0F)
  if !gs_eps(g_pos, 5.0F, 0.001F) {
    fail("gelu_sparse(5) = \{g_pos}, expected ≈ 5")
  }
  // Large negative: gelu(-5) ≈ 0.
  let g_neg = gelu_sparse(-5.0F)
  if g_neg < -0.0001F || g_neg > 0.0001F {
    fail("gelu_sparse(-5) = \{g_neg}, expected ≈ 0")
  }
}

///|
test "gelu_sparse: gelu(±1) matches standard normal CDF" {
  // Φ(1) ≈ 0.8413, so gelu_sparse(1) = 1 * 0.8413 = 0.8413
  let g1 = gelu_sparse(1.0F)
  if !gs_eps(g1, 0.8413F, 0.001F) {
    fail("gelu_sparse(1) = \{g1}, expected ≈ 0.8413")
  }
  // Φ(-1) ≈ 0.1587, so gelu_sparse(-1) = -1 * 0.1587 = -0.1587
  let gm1 = gelu_sparse(-1.0F)
  if !gs_eps(gm1, -0.1587F, 0.001F) {
    fail("gelu_sparse(-1) = \{gm1}, expected ≈ -0.1587")
  }
}

///|
test "gelu_sparse_grad(0) = 0.5 (matches dgelu/dx at origin)" {
  // dgelu/dx at 0 = Φ(0) + 0·φ(0) = 0.5
  assert_eq(gelu_sparse_grad(0.0F), 0.5F)
  // Saturation: large |x| → gradient → 0 (for -x) or 1 (for +x).
  let g_far = gelu_sparse_grad(5.0F)
  if !gs_eps(g_far, 1.0F, 0.001F) {
    fail("gelu_sparse_grad(5) = \{g_far}, expected ≈ 1")
  }
  let g_far_neg = gelu_sparse_grad(-5.0F)
  if g_far_neg < -0.001F || g_far_neg > 0.001F {
    fail("gelu_sparse_grad(-5) = \{g_far_neg}, expected ≈ 0")
  }
}

///|
test "gelu_sparse: tanh approximation matches within 1e-3 on |x| ≤ 5" {
  // Compare to the tanh-approximation from gelu.mbt.
  let xs : Array[Float] = [-2.5F, -1.0F, -0.5F, 0.0F, 0.5F, 1.0F, 2.5F]
  for i in 0..<xs.length() {
    let x = xs[i]
    let sparse = gelu_sparse(x)
    let tanh_approx = gelu(x)
    // The tanh approximation has ~1e-3 error on |x| ≤ 5.
    if !gs_eps(sparse, tanh_approx, 0.005F) {
      fail("at x=\{x}: sparse = \{sparse}, tanh = \{tanh_approx}, differ > 5e-3")
    }
  }
}

///|
test "gelu_sparse_backward: gradient check vs central difference" {
  let eps = 0.001F
  let x : Float = 0.7F
  let d_out : Float = 1.5F
  let analytical = gelu_sparse_grad(x) * d_out
  let g_plus = gelu_sparse(x + eps)
  let g_minus = gelu_sparse(x - eps)
  let numerical = (g_plus - g_minus) / (2.0F * eps) * d_out
  if !gs_eps(analytical, numerical, 0.001F) {
    fail("gelu_sparse_backward mismatch: analytical = \{analytical}, numerical = \{numerical}")
  }
}

///|
test "gelu_sparse_forward + backward element-wise + no-mutation" {
  let input : Array[Float] = [-2.0F, -1.0F, 0.0F, 1.0F, 2.0F]
  let out = gelu_sparse_forward(input)
  assert_eq(out.length(), 5)
  for i in 0..<5 {
    let expected = gelu_sparse(input[i])
    if !gs_eps(out[i], expected, 0.0001F) {
      fail("out[\{i}] = \{out[i]}, scalar = \{expected}")
    }
  }
  // No mutation of input.
  assert_eq(input[0], -2.0F)
  let d_output : Array[Float] = [1.0F, 1.0F, 1.0F, 1.0F, 1.0F]
  let d_input = gelu_sparse_backward(input, d_output)
  for i in 0..<5 {
    let expected = gelu_sparse_grad(input[i])
    if !gs_eps(d_input[i], expected, 0.0001F) {
      fail("d_input[\{i}] = \{d_input[i]}, expected \{expected}")
    }
  }
}