// maxpool2d_backward_test.mbt 鈥?tests for maxpool2d_backward (v0.12.1).

///|
test "maxpool2d_backward / pass-through at argmax, zeros elsewhere" {
  // 2x2 maxpool of [[1,2],[3,4]] -> [4]. argmax = (1,1). d_input
  // routes gradient to (1,1) only.
  let input : Array[Float] = [1.0F, 2.0F, 3.0F, 4.0F]
  let param = MaxPool2dParam::new(2, 2, stride=2, pad=0)
  let (_, argmax) = maxpool2d_forward_with_idx(input, 1, 1, 2, 2, param)
  let d_output : Array[Float] = [5.0F]
  let d_input = maxpool2d_backward(d_output, argmax, 1, 1, 2, 2, param)
  if d_input.length() != 4 { fail("expected length 4, got \{d_input.length()}") }
  // Expected: [0, 0, 0, 5]
  let expected : Array[Float] = [0.0F, 0.0F, 0.0F, 5.0F]
  for i in 0..<4 {
    if (d_input[i] - expected[i]).abs() > 1.0e-7 {
      fail("d_input[\{i}] expected \{expected[i]}, got \{d_input[i]}")
    }
  }
}

///|
test "maxpool2d_backward / multi-channel routes gradient per channel" {
  // Each channel has a distinct max.
  // Flat layout NCHW: ch0 = idx 0..3, ch1 = idx 4..7, ch2 = idx 8..11.
  let input : Array[Float] = [
    // ch0 (2x2): max=7 at local idx 3 (flat 3)
    1.0F, 2.0F, 3.0F, 7.0F,
    // ch1 (2x2): max=-1 at local idx 2 (flat 6)
    -2.0F, -3.0F, -1.0F, -4.0F,
    // ch2 (2x2): max=100 at local idx 2 (flat 10)
    50.0F, 60.0F, 100.0F, 70.0F,
  ]
  let param = MaxPool2dParam::new(2, 2, stride=2, pad=0)
  let (_, argmax) = maxpool2d_forward_with_idx(input, 1, 3, 2, 2, param)
  let d_output : Array[Float] = [1.0F, 2.0F, 3.0F]
  let d_input = maxpool2d_backward(d_output, argmax, 1, 3, 2, 2, param)
  if d_input.length() != 12 { fail("expected length 12, got \{d_input.length()}") }
  // ch0: d at flat idx 3 = 1, others 0
  if d_input[3] != 1.0F { fail("ch0 max flat 3 should be 1.0, got \{d_input[3]}") }
  // ch1: d at flat idx 6 = 2, others 0
  if d_input[6] != 2.0F { fail("ch1 max flat 6 should be 2.0, got \{d_input[6]}") }
  // ch2: d at flat idx 10 = 3, others 0
  if d_input[10] != 3.0F { fail("ch2 max flat 10 should be 3.0, got \{d_input[10]}") }
}

///|
test "maxpool2d_backward / batch N=2" {
  // Two independent inputs.
  let input : Array[Float] = [
    // batch 0: max=4 at flat 3
    1.0F, 2.0F, 3.0F, 4.0F,
    // batch 1: max=8 at flat 7
    5.0F, 6.0F, 7.0F, 8.0F,
  ]
  let param = MaxPool2dParam::new(2, 2, stride=2, pad=0)
  let (_, argmax) = maxpool2d_forward_with_idx(input, 2, 1, 2, 2, param)
  let d_output : Array[Float] = [10.0F, 20.0F]
  let d_input = maxpool2d_backward(d_output, argmax, 2, 1, 2, 2, param)
  if d_input.length() != 8 { fail("expected length 8, got \{d_input.length()}") }
  if d_input[3] != 10.0F { fail("batch 0 max idx 3 should be 10, got \{d_input[3]}") }
  if d_input[7] != 20.0F { fail("batch 1 max idx 7 should be 20, got \{d_input[7]}") }
  if d_input[0] != 0.0F || d_input[6] != 0.0F {
    fail("non-argmax cells should be zero")
  }
}

///|
test "maxpool2d_backward / gradient check vs numerical (2x2 stride=2)" {
  // Loss = sum of maxpool output. d_input[i] should be 1 if i is argmax
  // of some output window, else 0.
  let input : Array[Float] = [1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F, 7.0F, 8.0F, 9.0F]
  let param = MaxPool2dParam::new(2, 2, stride=2, pad=0)
  let eps = 0.001F
  let fwd = fn(inp : Array[Float]) -> Float {
    let (out, _) = maxpool2d_forward_with_idx(inp, 1, 1, 3, 3, param)
    let mut s : Float = 0.0F
    for i in 0..<out.length() { s = s + out[i] }
    s
  }
  let num = numerical_gradient(input, eps, fwd)
  let (out, argmax) = maxpool2d_forward_with_idx(input, 1, 1, 3, 3, param)
  let d_output : Array[Float] = Array::make(out.length(), 1.0F)
  let analytic = maxpool2d_backward(d_output, argmax, 1, 1, 3, 3, param)
  let (max_diff, max_idx) = max_abs_diff(num, analytic)
  if max_diff > 1.0e-3 {
    fail("maxpool backward gradient mismatch: max diff \{max_diff} at \{max_idx}")
  }
}