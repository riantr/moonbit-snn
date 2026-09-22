// relu_test.mbt 鈥?tests for relu_forward (v0.11.1).

///|
test "relu_forward / mixed positive/negative values" {
  let input : Array[Float] = [-2.0F, -1.0F, 0.0F, 1.0F, 2.0F, 3.5F, -0.5F]
  let out = relu_forward(input)
  if out.length() != 7 { fail("expected length 7, got \{out.length()}") }
  let expected : Array[Float] = [0.0F, 0.0F, 0.0F, 1.0F, 2.0F, 3.5F, 0.0F]
  for i in 0..<7 {
    if (out[i] - expected[i]).abs() > 1.0e-6 {
      fail("out[\{i}] expected \{expected[i]}, got \{out[i]}")
    }
  }
}

///|
test "relu_forward / all-positive unchanged" {
  let input : Array[Float] = [0.1F, 1.0F, 100.0F, 1.0e-6F]
  let out = relu_forward(input)
  for i in 0..<4 {
    if (out[i] - input[i]).abs() > 1.0e-6 {
      fail("out[\{i}] expected \{input[i]}, got \{out[i]}")
    }
  }
}

///|
test "relu_forward / all-negative becomes zero" {
  let input : Array[Float] = [-0.0001F, -1.0F, -1000.0F, -3.4e38F]
  let out = relu_forward(input)
  for i in 0..<4 {
    if out[i] != 0.0F {
      fail("out[\{i}] expected 0.0, got \{out[i]}")
    }
  }
}

///|
test "relu_forward / does not mutate input" {
  let input : Array[Float] = [1.0F, -2.0F, 3.0F]
  let original = input.copy()
  let _ = relu_forward(input)
  for i in 0..<3 {
    if (input[i] - original[i]).abs() > 1.0e-7 {
      fail("relu_forward should not mutate input[\{i}]; was \{original[i]}, now \{input[i]}")
    }
  }
}