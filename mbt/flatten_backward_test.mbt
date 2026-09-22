// flatten_backward_test.mbt 鈥?tests for flatten_backward (v0.12.0).

///|
test "flatten_backward / identity copy preserves values" {
  let d_output : Array[Float] = [1.0F, 2.0F, 3.0F, 4.0F]
  let d_input = flatten_backward(d_output, 1, 1, 2, 2)
  if d_input.length() != 4 { fail("expected length 4, got \{d_input.length()}") }
  for i in 0..<4 {
    if (d_input[i] - d_output[i]).abs() > 1.0e-7 {
      fail("d_input[\{i}] expected \{d_output[i]}, got \{d_input[i]}")
    }
  }
}

///|
test "flatten_backward / does not alias d_output" {
  let d_output : Array[Float] = [10.0F, 20.0F, 30.0F]
  let original = d_output.copy()
  let d_input = flatten_backward(d_output, 1, 1, 1, 3)
  d_input[0] = -1.0F
  for i in 0..<3 {
    if (d_output[i] - original[i]).abs() > 1.0e-7 {
      fail("flatten_backward should not alias d_output[\{i}]")
    }
  }
}

///|
test "flatten_backward / chain test: input -> flatten -> sum -> backward" {
  // Forward: out = sum(flatten(input)).
  // Since flatten is identity data-wise, d_input[i] = 1 for all i.
  let input : Array[Float] = [1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F]
  let flat = flatten_forward(input, 1, 2, 1, 3)
  let loss : Float = {
    let mut s : Float = 0.0F
    for i in 0..<flat.length() {
      s = s + flat[i]
    }
    s
  }
  if (loss - 21.0F).abs() > 1.0e-5 {
    fail("forward loss expected 21, got \{loss}")
  }
  let d_output : Array[Float] = Array::make(flat.length(), 1.0F)
  let d_input = flatten_backward(d_output, 1, 2, 1, 3)
  for i in 0..<6 {
    if (d_input[i] - 1.0F).abs() > 1.0e-7 {
      fail("d_input[\{i}] expected 1.0, got \{d_input[i]}")
    }
  }
}