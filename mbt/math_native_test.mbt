// Tests for the native math FFI bindings (expf, tanhf, sigmoid_f32).
//
// These are bit-exact checks against the C library functions
// `expf` / `tanhf`, which Julia also uses under the hood for
// Float32 transcendentals.

///|
test "expf(0) = 1" {
  // Julia: expf(0.0f0) = 1.0f0
  assert_eq(expf(0.0F), 1.0F)
}

///|
test "expf(1) is bit-exact" {
  // Julia: expf(1.0f0) = 2.7182817459106445f0
  // (last bit of Float32 e^1)
  let got = expf(1.0F)
  // 2.7182817459106445 rounds to 2.7182817f0 in standard Float32 print
  // but the bit pattern is the same.
  assert_eq(got, 2.7182817F)
}

///|
test "expf is monotonic" {
  let a = expf(-2.0F)
  let b = expf(0.0F)
  let c = expf(2.0F)
  if !(a < b && b < c) {
    fail("expf not monotonic: \{a}, \{b}, \{c}")
  }
}

///|
test "sigmoid_f32(0) = 0.5" {
  // Julia: 1 / (1 + expf(0.0f0)) = 0.5
  assert_eq(sigmoid_f32(0.0F), 0.5F)
}

///|
test "sigmoid_f32 is in (0, 1)" {
  for v in [-3.0F, -1.0F, 0.0F, 1.0F, 3.0F] {
    let s = sigmoid_f32(v)
    if s <= 0.0F || s >= 1.0F {
      fail("sigmoid(\{v}) = \{s} not in (0, 1)")
    }
  }
}
