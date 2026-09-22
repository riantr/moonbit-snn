// linear_backward_test.mbt 鈥?tests for linear_backward (v0.12.0).

///|
test "linear_backward / single-batch known values" {
  // N=1, in=3, out=2. y = W @ x + b.
  // W = [[1, 2, 3], [4, 5, 6]], x = [10, 20, 30], b = [100, 200].
  //   y = [240, 520]
  // d_y = [1, 1] -> d_x[i] = sum_o W[o, i] * d_y[o] (N=1):
  //   d_x[0] = W[0,0]*1 + W[1,0]*1 = 1 + 4 = 5
  //   d_x[1] = W[0,1]*1 + W[1,1]*1 = 2 + 5 = 7
  //   d_x[2] = W[0,2]*1 + W[1,2]*1 = 3 + 6 = 9
  // d_W[o, i] = d_y[o] * x[i]:
  //   d_W[0] = [1*10, 1*20, 1*30] = [10, 20, 30]
  //   d_W[1] = [1*10, 1*20, 1*30] = [10, 20, 30]
  // d_b = [1, 1].
  let weight : Array[Float] = [1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F]
  let bias : Array[Float] = [100.0F, 200.0F]
  let param = LinearParam::new(weight, bias, 3, 2)
  let input : Array[Float] = [10.0F, 20.0F, 30.0F]
  let d_output : Array[Float] = [1.0F, 1.0F]
  let (d_input, d_weight, d_bias) = linear_backward(input, d_output, 1, param)
  // d_input
  let expected_d_input : Array[Float] = [5.0F, 7.0F, 9.0F]
  for i in 0..<3 {
    if (d_input[i] - expected_d_input[i]).abs() > 1.0e-5 {
      fail("d_input[\{i}] expected \{expected_d_input[i]}, got \{d_input[i]}")
    }
  }
  // d_weight
  let expected_d_weight : Array[Float] = [10.0F, 20.0F, 30.0F, 10.0F, 20.0F, 30.0F]
  for i in 0..<6 {
    if (d_weight[i] - expected_d_weight[i]).abs() > 1.0e-5 {
      fail("d_weight[\{i}] expected \{expected_d_weight[i]}, got \{d_weight[i]}")
    }
  }
  // d_bias
  let expected_d_bias : Array[Float] = [1.0F, 1.0F]
  for i in 0..<2 {
    if (d_bias[i] - expected_d_bias[i]).abs() > 1.0e-5 {
      fail("d_bias[\{i}] expected \{expected_d_bias[i]}, got \{d_bias[i]}")
    }
  }
}

///|
test "linear_backward / batch N=2 d_input sums across batch for d_weight" {
  // N=2, in=2, out=1.
  //   W = [[1, 0]], b = [0].
  //   x[0] = [1, 2], x[1] = [3, 4]
  //   y[0] = 1*1 + 0*2 = 1; y[1] = 1*3 + 0*4 = 3.
  //   d_y = [2, 5] (one per batch).
  // d_W[0, 0] = d_y[0] * x[0, 0] + d_y[1] * x[1, 0] = 2*1 + 5*3 = 17.
  // d_W[0, 1] = d_y[0] * x[0, 1] + d_y[1] * x[1, 1] = 2*2 + 5*4 = 24.
  // d_b[0] = 2 + 5 = 7.
  // d_x[0, 0] = W[0, 0] * d_y[0] = 1 * 2 = 2.
  // d_x[0, 1] = W[0, 1] * d_y[0] = 0 * 2 = 0.
  // d_x[1, 0] = W[0, 0] * d_y[1] = 1 * 5 = 5.
  // d_x[1, 1] = W[0, 1] * d_y[1] = 0 * 5 = 0.
  let weight : Array[Float] = [1.0F, 0.0F]
  let bias : Array[Float] = [0.0F]
  let param = LinearParam::new(weight, bias, 2, 1)
  let input : Array[Float] = [1.0F, 2.0F, 3.0F, 4.0F]
  let d_output : Array[Float] = [2.0F, 5.0F]
  let (d_input, d_weight, d_bias) = linear_backward(input, d_output, 2, param)
  if (d_input[0] - 2.0F).abs() > 1.0e-5 { fail("d_input[0] expected 2, got \{d_input[0]}") }
  if (d_input[1] - 0.0F).abs() > 1.0e-5 { fail("d_input[1] expected 0, got \{d_input[1]}") }
  if (d_input[2] - 5.0F).abs() > 1.0e-5 { fail("d_input[2] expected 5, got \{d_input[2]}") }
  if (d_input[3] - 0.0F).abs() > 1.0e-5 { fail("d_input[3] expected 0, got \{d_input[3]}") }
  if (d_weight[0] - 17.0F).abs() > 1.0e-5 { fail("d_weight[0] expected 17, got \{d_weight[0]}") }
  if (d_weight[1] - 24.0F).abs() > 1.0e-5 { fail("d_weight[1] expected 24, got \{d_weight[1]}") }
  if (d_bias[0] - 7.0F).abs() > 1.0e-5 { fail("d_bias[0] expected 7, got \{d_bias[0]}") }
}

///|
test "linear_backward / gradient check vs numerical (single-batch)" {
  // Loss = sum(linear_forward(input, W, b)). For each component of
  // input, the analytical d_input[i] = sum_o W[o, i] * 1 = row-sum.
  let in_f = 3
  let out_f = 2
  let weight : Array[Float] = [0.5F, -1.0F, 2.0F, 1.0F, 1.0F, 0.0F]
  let bias : Array[Float] = [0.0F, 0.0F]
  let param = LinearParam::new(weight, bias, in_f, out_f)
  let input : Array[Float] = [0.3F, -0.7F, 1.2F]
  let eps = 0.001F
  let fwd = fn(inp : Array[Float]) -> Float {
    let y = linear_forward(inp, 1, param)
    let mut s : Float = 0.0F
    for i in 0..<y.length() {
      s = s + y[i]
    }
    s
  }
  let num = numerical_gradient(input, eps, fwd)
  let d_output : Array[Float] = Array::make(out_f, 1.0F)
  let (analytic, _, _) = linear_backward(input, d_output, 1, param)
  let (max_diff, max_idx) = max_abs_diff(num, analytic)
  if max_diff > 1.0e-3 {
    fail("linear backward d_input mismatch: max diff \{max_diff} at \{max_idx}")
  }
}

///|
test "linear_backward / gradient check vs numerical (d_weight)" {
  // Loss = sum(linear_forward(input, W, b)). For d_weight[o, i] the
  // analytical value is x[i] * 1 (per batch). Verify against numerical.
  let in_f = 2
  let out_f = 3
  let weight : Array[Float] = [0.2F, 0.3F, -0.5F, 0.1F, 0.0F, 0.7F]
  let bias : Array[Float] = [0.0F, 0.0F, 0.0F]
  let param = LinearParam::new(weight, bias, in_f, out_f)
  let input : Array[Float] = [0.5F, -0.4F]
  // Numerical d_weight: perturb each weight by eps.
  let eps = 0.001F
  let num_d_weight : Array[Float] = Array::make(out_f * in_f, 0.0F)
  let original_w = weight.copy()
  for k in 0..<(out_f * in_f) {
    weight[k] = original_w[k] + eps
    let y_p = linear_forward(input, 1, param)
    let mut loss_p : Float = 0.0F
    for i in 0..<y_p.length() { loss_p = loss_p + y_p[i] }
    weight[k] = original_w[k] - eps
    let y_m = linear_forward(input, 1, param)
    let mut loss_m : Float = 0.0F
    for i in 0..<y_m.length() { loss_m = loss_m + y_m[i] }
    weight[k] = original_w[k]
    num_d_weight[k] = (loss_p - loss_m) / (2.0F * eps)
  }
  let d_output : Array[Float] = Array::make(out_f, 1.0F)
  let (_, analytic_d_weight, _) = linear_backward(input, d_output, 1, param)
  let (max_diff, max_idx) = max_abs_diff(num_d_weight, analytic_d_weight)
  if max_diff > 1.0e-3 {
    fail("linear backward d_weight mismatch: max diff \{max_diff} at \{max_idx}")
  }
}