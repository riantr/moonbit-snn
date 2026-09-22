// relu_backward_test.mbt 鈥?tests for relu_backward (v0.12.0).

///|
test "relu_backward / pass-through where input > 0" {
  // input[i] > 0 -> d_input[i] = d_output[i].
  let input : Array[Float] = [1.0F, 2.0F, 3.0F, 0.5F]
  let d_output : Array[Float] = [0.1F, 0.2F, 0.3F, 0.4F]
  let d_input = relu_backward(input, d_output)
  for i in 0..<4 {
    if (d_input[i] - d_output[i]).abs() > 1.0e-7 {
      fail("d_input[\{i}] expected \{d_output[i]}, got \{d_input[i]}")
    }
  }
}

///|
test "relu_backward / zero where input <= 0" {
  // input[i] <= 0 -> d_input[i] = 0 regardless of d_output.
  let input : Array[Float] = [-2.0F, -1.0F, 0.0F, -0.5F]
  let d_output : Array[Float] = [99.0F, -99.0F, 100.0F, 50.0F]
  let d_input = relu_backward(input, d_output)
  for i in 0..<4 {
    if d_input[i] != 0.0F {
      fail("d_input[\{i}] expected 0, got \{d_input[i]}")
    }
  }
}

///|
test "relu_backward / does not mutate d_output" {
  // Sanity: the returned d_input is a fresh copy.
  let input : Array[Float] = [1.0F, -1.0F, 2.0F]
  let d_output : Array[Float] = [10.0F, 20.0F, 30.0F]
  let original = d_output.copy()
  let d_input = relu_backward(input, d_output)
  // Mutate d_input and verify d_output is unchanged.
  d_input[0] = -42.0F
  for i in 0..<3 {
    if (d_output[i] - original[i]).abs() > 1.0e-7 {
      fail("relu_backward should not alias d_output[\{i}]; was \{original[i]}, now \{d_output[i]}")
    }
  }
}

///|
test "relu_backward / gradient check vs numerical (strictly-positive/negative input)" {
  // Loss = sum(output) = sum(max(x, 0)). Analytical d_input = 1 if x>0 else 0.
  // Note: x=0 is non-differentiable (subgradient undefined); we keep
  // the input strictly away from 0 so the central-difference numerical
  // gradient matches the analytic gradient.
  let input : Array[Float] = [-1.0F, 0.5F, -2.0F, 3.0F, -0.7F, 0.1F]
  let eps = 0.001F
  // Forward as a closure that mutates input in place.
  let fwd = fn(inp : Array[Float]) -> Float {
    let mut s : Float = 0.0F
    for i in 0..<inp.length() {
      let v = inp[i]
      if v > 0.0F {
        s = s + v
      }
    }
    s
  }
  let num = numerical_gradient(input, eps, fwd)
  let d_output : Array[Float] = Array::make(input.length(), 1.0F)
  let analytic = relu_backward(input, d_output)
  let (max_diff, max_idx) = max_abs_diff(num, analytic)
  if max_diff > 1.0e-3 {
    fail("relu backward gradient mismatch: max diff \{max_diff} at \{max_idx} (num=\{num[max_idx]} vs analytic=\{analytic[max_idx]})")
  }
}