// Tests for GELU activation (forward + backward).

///|
fn gelu_eps(a : Float, b : Float, eps : Float) -> Bool {
  let d = if a > b { a - b } else { b - a }
  d <= eps
}

///|
test "gelu(0) = 0, gelu(+x) ≈ x for large x, gelu(-x) ≈ 0 for large -x" {
  // At x=0: inner = 0, tanh(0) = 0, y = 0.5·0·1 = 0
  assert_eq(gelu(0.0F), 0.0F)
  // Large positive: GELU(5) ≈ 5
  let g_pos = gelu(5.0F)
  if !gelu_eps(g_pos, 5.0F, 0.001F) {
    fail("gelu(5) = \{g_pos}, expected ≈ 5")
  }
  // Large negative: GELU(-5) ≈ 0
  let g_neg = gelu(-5.0F)
  if g_neg < -0.0001F || g_neg > 0.0001F {
    fail("gelu(-5) = \{g_neg}, expected ≈ 0")
  }
  // Antisymmetry check: GELU(-x) ≈ -GELU(x) is NOT exact because erf
  // isn't antisymmetric, but for large x it holds approximately.
  // For moderate x, verify GELU(-1) < 0 < GELU(1).
  if !(gelu(-1.0F) < 0.0F && gelu(1.0F) > 0.0F) {
    fail("gelu(1) and gelu(-1) should bracket 0")
  }
}

///|
test "gelu(1) and gelu(-1) match approximate normal CDF" {
  // Exact GELU(1) = Φ(1) ≈ 0.8413
  // Tanh-approx is within ~0.0002 of the exact value.
  let g1 = gelu(1.0F)
  if !gelu_eps(g1, 0.8413F, 0.001F) {
    fail("gelu(1) = \{g1}, expected ≈ 0.8413")
  }
  // GELU(-1) = -Φ(-1) ≈ -0.1587
  let gm1 = gelu(-1.0F)
  if !gelu_eps(gm1, -0.1587F, 0.001F) {
    fail("gelu(-1) = \{gm1}, expected ≈ -0.1587")
  }
}

///|
test "gelu_grad(0) = 0.5 (matches dgelu/dx at origin)" {
  // At x=0: dgelu/dx = 0.5·(1 + tanh(0)) + 0 = 0.5·1 = 0.5
  assert_eq(gelu_grad(0.0F), 0.5F)
  // Note: gelu_grad is NOT symmetric in general because the constant
  // term 0.5·(1 + tanh(inner)) breaks evenness. At x=±1:
  //   gelu_grad(+1) > gelu_grad(-1)  (positive side has steeper slope).
  if gelu_grad(1.0F) <= gelu_grad(-1.0F) {
    fail("gelu_grad(1) = \{gelu_grad(1.0F)} should > gelu_grad(-1) = \{gelu_grad(-1.0F)}")
  }
  // Saturation: large positive x → gradient → 1 (GELU becomes identity)
  let g_far = gelu_grad(5.0F)
  if !gelu_eps(g_far, 1.0F, 0.001F) {
    fail("gelu_grad(5) = \{g_far}, expected ≈ 1")
  }
  // Large negative x → gradient → 0 (GELU becomes constant 0)
  let g_far_neg = gelu_grad(-5.0F)
  if g_far_neg < -0.001F || g_far_neg > 0.001F {
    fail("gelu_grad(-5) = \{g_far_neg}, expected ≈ 0")
  }
}

///|
test "gelu_forward element-wise + no-mutation" {
  let input : Array[Float] = [-2.0F, -1.0F, 0.0F, 1.0F, 2.0F]
  let out = gelu_forward(input)
  assert_eq(out.length(), 5)
  // Element-wise consistency with scalar
  for i in 0..<5 {
    let expected = gelu(input[i])
    if !gelu_eps(out[i], expected, 0.0001F) {
      fail("out[\{i}] = \{out[i]}, scalar = \{expected}")
    }
  }
  // No aliasing — input unchanged
  assert_eq(input[0], -2.0F)
  assert_eq(input[4], 2.0F)
}

///|
test "gelu_backward gradient check vs numerical (central difference)" {
  let eps = 0.001F
  let x : Float = 0.7F
  let d_out : Float = 1.5F
  // Analytical gradient (per-element)
  let analytical = gelu_grad(x) * d_out
  // Numerical gradient via central difference on scalar gelu
  let g_plus = gelu(x + eps)
  let g_minus = gelu(x - eps)
  let numerical = (g_plus - g_minus) / (2.0F * eps) * d_out
  // Float32 tolerance ~1e-3 for sums of 3 products
  if !gelu_eps(analytical, numerical, 0.001F) {
    fail("gelu_backward mismatch: analytical = \{analytical}, numerical = \{numerical}")
  }
}

///|
test "gelu_backward element-wise + zero no-op" {
  let input : Array[Float] = [-2.0F, -0.5F, 0.0F, 0.5F, 2.0F]
  let d_output : Array[Float] = [1.0F, 1.0F, 1.0F, 1.0F, 1.0F]
  let d_input = gelu_backward(input, d_output)
  assert_eq(d_input.length(), 5)
  for i in 0..<5 {
    let expected = gelu_grad(input[i])
    if !gelu_eps(d_input[i], expected, 0.0001F) {
      fail("d_input[\{i}] = \{d_input[i]}, expected \{expected}")
    }
  }
  // Zero d_output → zero d_input
  let zero : Array[Float] = Array::make(5, 0.0F)
  let d_zero = gelu_backward(input, zero)
  for i in 0..<5 {
    if d_zero[i] != 0.0F {
      fail("zero d_output at \{i}: \{d_zero[i]} != 0")
    }
  }
}