// Tests for dropout.

///|
fn drop_eps(a : Float, b : Float, eps : Float) -> Bool {
  let d = if a > b { a - b } else { b - a }
  d <= eps
}

///|
test "Dropout::new: defaults + out-of-range aborts" {
  let d = Dropout::new()
  assert_eq(d.p, 0.5F)
  assert_eq(d.training, true)
  let d_off = Dropout::new(p=0.0F)
  assert_eq(d_off.p, 0.0F)
}

///|
test "dropout_forward: inference mode is identity" {
  let d = Dropout::new(p=0.5F)
  d.training = false
  let input : Array[Float] = [0.1F, 0.2F, 0.3F, 0.4F, 0.5F]
  let rng = Xoshiro::from_state(1UL, 2UL, 3UL, 4UL)
  let (out, mask) = dropout_forward(input, d, rng)
  for i in 0..<input.length() {
    if !drop_eps(out[i], input[i], 0.0001F) {
      fail("inference out[\{i}] = \{out[i]}, expected identity")
    }
    if mask[i] != 1.0F {
      fail("inference mask[\{i}] = \{mask[i]}, expected 1")
    }
  }
}

///|
test "dropout_forward: training mode zeros ~p fraction + scales by 1/(1-p)" {
  let d = Dropout::new(p=0.5F)
  let n = 1000
  let input : Array[Float] = Array::make(n, 1.0F)
  let rng = Xoshiro::from_state(42UL, 0UL, 0UL, 0UL)
  let (out, mask) = dropout_forward(input, d, rng)
  // Count kept positions (mask == 1)
  let mut kept = 0
  for i in 0..<n {
    if mask[i] == 1.0F {
      kept = kept + 1
    }
  }
  // Expected ~500 ±50 (binomial at p=0.5, n=1000)
  if kept < 450 || kept > 550 {
    fail("kept count = \{kept}/1000, expected ~500")
  }
  // Surviving values should be scaled by 1/(1-0.5) = 2.0
  for i in 0..<n {
    if mask[i] == 1.0F {
      if !drop_eps(out[i], 2.0F, 0.0001F) {
        fail("kept out[\{i}] = \{out[i]}, expected 2.0")
      }
    } else {
      if out[i] != 0.0F {
        fail("dropped out[\{i}] = \{out[i]}, expected 0")
      }
    }
  }
}

///|
test "dropout_forward: deterministic same-seed → same mask" {
  let d = Dropout::new(p=0.3F)
  let input : Array[Float] = Array::make(50, 0.7F)
  let rng_a = Xoshiro::from_state(7UL, 0UL, 0UL, 0UL)
  let rng_b = Xoshiro::from_state(7UL, 0UL, 0UL, 0UL)
  let (_out_a, mask_a) = dropout_forward(input, d, rng_a)
  let (_out_b, mask_b) = dropout_forward(input, d, rng_b)
  for i in 0..<mask_a.length() {
    if mask_a[i] != mask_b[i] {
      fail("mask[\{i}]: \{mask_a[i]} vs \{mask_b[i]}")
    }
  }
}

///|
test "dropout_forward: p=0 → identity (no-op)" {
  let d = Dropout::new(p=0.0F)
  let input : Array[Float] = [0.1F, 0.2F, 0.3F, 0.4F]
  let rng = Xoshiro::default()
  let (out, mask) = dropout_forward(input, d, rng)
  for i in 0..<input.length() {
    if !drop_eps(out[i], input[i], 0.0001F) {
      fail("p=0 out[\{i}] = \{out[i]}")
    }
    if mask[i] != 1.0F {
      fail("p=0 mask[\{i}] = \{mask[i]}")
    }
  }
}

///|
test "dropout_forward: p=1 → all zeros (full dropout)" {
  let d = Dropout::new(p=1.0F)
  let input : Array[Float] = [0.1F, 0.2F, 0.3F, 0.4F]
  let rng = Xoshiro::default()
  let (out, mask) = dropout_forward(input, d, rng)
  for i in 0..<input.length() {
    if out[i] != 0.0F {
      fail("p=1 out[\{i}] = \{out[i]}, expected 0")
    }
    if mask[i] != 0.0F {
      fail("p=1 mask[\{i}] = \{mask[i]}, expected 0")
    }
  }
}

///|
test "dropout_backward: matches forward (mask * scale) + zero d_output" {
  let d = Dropout::new(p=0.5F)
  let input : Array[Float] = Array::make(8, 0.7F)
  let rng = Xoshiro::from_state(11UL, 0UL, 0UL, 0UL)
  let (_out, mask) = dropout_forward(input, d, rng)
  let d_output : Array[Float] = [0.1F, 0.2F, 0.3F, 0.4F,
                                  0.5F, 0.6F, 0.7F, 0.8F]
  let d_input = dropout_backward(d_output, mask, d)
  // For each kept position: d_input = d_output * 2.0 (inverted scale).
  // For each dropped position: d_input = 0.
  for i in 0..<8 {
    if mask[i] == 1.0F {
      let expected = d_output[i] * 2.0F
      if !drop_eps(d_input[i], expected, 0.0001F) {
        fail("kept d_input[\{i}] = \{d_input[i]}, expected \{expected}")
      }
    } else {
      if d_input[i] != 0.0F {
        fail("dropped d_input[\{i}] = \{d_input[i]}, expected 0")
      }
    }
  }
  // Zero d_output → zero d_input
  let zero : Array[Float] = Array::make(8, 0.0F)
  let d_zero = dropout_backward(zero, mask, d)
  for i in 0..<8 {
    if d_zero[i] != 0.0F {
      fail("zero d_output [\{i}] = \{d_zero[i]}")
    }
  }
}

///|
test "dropout_backward: inference mode is identity" {
  let d = Dropout::new(p=0.5F)
  d.training = false
  let mask : Array[Float] = [0.0F, 0.0F, 0.0F, 0.0F]  // doesn't matter
  let d_output : Array[Float] = [0.1F, 0.2F, 0.3F, 0.4F]
  let d_input = dropout_backward(d_output, mask, d)
  for i in 0..<4 {
    if !drop_eps(d_input[i], d_output[i], 0.0001F) {
      fail("inference d_input[\{i}] = \{d_input[i]}")
    }
  }
}