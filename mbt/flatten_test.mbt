// flatten_test.mbt 鈥?tests for flatten_forward (v0.11.1).

///|
test "flatten_forward / NCHW 1x1x2x2 -> 1x4 preserves data" {
  // NCHW [1, 1, 2, 2] = single-channel 2x2 image.
  let input : Array[Float] = [1.0F, 2.0F, 3.0F, 4.0F]
  let out = flatten_forward(input, 1, 1, 2, 2)
  if out.length() != 4 { fail("expected length 4, got \{out.length()}") }
  for i in 0..<4 {
    if (out[i] - input[i]).abs() > 1.0e-7 {
      fail("out[\{i}] expected \{input[i]}, got \{out[i]}")
    }
  }
}

///|
test "flatten_forward / multi-channel preserves interleave" {
  // NCHW [1, 2, 2, 2] = 2 channels, 2x2 each.
  // NCHW flat layout: [ch0_row0, ch0_row1, ch1_row0, ch1_row1].
  let input : Array[Float] = [
    // ch0 row 0: 1, 2; row 1: 3, 4
    1.0F, 2.0F, 3.0F, 4.0F,
    // ch1 row 0: 5, 6; row 1: 7, 8
    5.0F, 6.0F, 7.0F, 8.0F,
  ]
  let out = flatten_forward(input, 1, 2, 2, 2)
  if out.length() != 8 { fail("expected length 8, got \{out.length()}") }
  for i in 0..<8 {
    if (out[i] - input[i]).abs() > 1.0e-7 {
      fail("out[\{i}] expected \{input[i]}, got \{out[i]}")
    }
  }
}

///|
test "flatten_forward / batch N=2 with 3-channel 2x2" {
  // [2, 3, 2, 2] -> [2, 12]. Just verify lengths and that data passes
  // through unchanged.
  let n = 2
  let c = 3
  let h = 2
  let w = 2
  let n_total = n * c * h * w
  let input : Array[Float] = Array::make(n_total, 0.0F)
  for i in 0..<n_total {
    input[i] = Float::from_int(i + 1)
  }
  let out = flatten_forward(input, n, c, h, w)
  if out.length() != n * c * h * w {
    fail("expected length \{n * c * h * w}, got \{out.length()}")
  }
  for i in 0..<n_total {
    if (out[i] - input[i]).abs() > 1.0e-7 {
      fail("out[\{i}] expected \{input[i]}, got \{out[i]}")
    }
  }
}