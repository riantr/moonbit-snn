// conv2d_backward_test.mbt 鈥?tests for conv2d_backward (v0.12.1).

///|
test "conv2d_backward / d_bias = sum of d_output across batch and spatial" {
  // N=1, C_in=1, H=W=2, kh=kw=1, stride=1, pad=0, C_out=2 -> output 2x2 per ch.
  //   ho = wo = 2, so d_output has length n * c_out * ho * wo = 8.
  let input : Array[Float] = [1.0F, 2.0F, 3.0F, 4.0F]
  let weight : Array[Float] = [0.5F, 0.5F]  // [c_out=2, c_in=1, kh=1, kw=1]
  let bias : Array[Float] = [0.0F, 0.0F]
  let param = Conv2dParam::new(weight, bias, 2, 1, 1, 1, stride=1, pad=0)
  // d_output layout: [c_out=0, ho=0..1, wo=0..1] + [c_out=1, ho=0..1, wo=0..1]
  let d_output : Array[Float] = [1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F, 7.0F, 8.0F]
  let (_, _, d_bias) = conv2d_backward(input, d_output, 1, 1, 2, 2, param)
  // c_out=0: sum of d_output[0..3] = 1+2+3+4 = 10
  // c_out=1: sum of d_output[4..7] = 5+6+7+8 = 26
  if (d_bias[0] - 10.0F).abs() > 1.0e-5 { fail("d_bias[0] expected 10, got \{d_bias[0]}") }
  if (d_bias[1] - 26.0F).abs() > 1.0e-5 { fail("d_bias[1] expected 26, got \{d_bias[1]}") }
}

///|
test "conv2d_backward / 1x1 conv: d_weight = d_output * input (per-channel outer product)" {
  // N=1, C_in=2, H=W=2, kh=kw=1, stride=1, pad=0, C_out=1 -> output 4 cells.
  // d_weight[0, ci, 0, 0] = sum_{ho, wo} d_output[ho, wo] * input[ci, ho, wo]
  let input : Array[Float] = [
    // ch0
    1.0F, 2.0F, 3.0F, 4.0F,
    // ch1
    10.0F, 20.0F, 30.0F, 40.0F,
  ]
  let weight : Array[Float] = [0.5F, 0.5F]  // c_out=1, c_in=2
  let bias : Array[Float] = [0.0F]
  let param = Conv2dParam::new(weight, bias, 1, 2, 1, 1, stride=1, pad=0)
  let d_output : Array[Float] = [1.0F, 1.0F, 1.0F, 1.0F]
  let (_, d_weight, _) = conv2d_backward(input, d_output, 1, 2, 2, 2, param)
  // d_weight[0, 0] = sum d_output * input[ch0] = 1+2+3+4 = 10
  // d_weight[0, 1] = sum d_output * input[ch1] = 10+20+30+40 = 100
  if (d_weight[0] - 10.0F).abs() > 1.0e-5 { fail("d_weight[0,0] expected 10, got \{d_weight[0]}") }
  if (d_weight[1] - 100.0F).abs() > 1.0e-5 { fail("d_weight[0,1] expected 100, got \{d_weight[1]}") }
}

///|
test "conv2d_backward / 1x1 conv: d_input = sum_o W[o, i] * d_output" {
  // N=1, C_in=2, H=W=2, C_out=3.
  // d_input[n, ci, ih, iw] = sum_co W[co, ci, 0, 0] * d_output[n, co, ih, iw]
  let input : Array[Float] = [
    1.0F, 2.0F, 3.0F, 4.0F,
    10.0F, 20.0F, 30.0F, 40.0F,
  ]
  let weight : Array[Float] = [
    // c_out=0, c_in=0..1
    0.5F, 0.5F,
    // c_out=1, c_in=0..1
    1.0F, 2.0F,
    // c_out=2, c_in=0..1
    3.0F, 4.0F,
  ]
  let bias : Array[Float] = [0.0F, 0.0F, 0.0F]
  let param = Conv2dParam::new(weight, bias, 3, 2, 1, 1, stride=1, pad=0)
  let d_output : Array[Float] = [
    // c_out=0: 4 cells = 1 each
    1.0F, 1.0F, 1.0F, 1.0F,
    // c_out=1: 4 cells = 1 each
    1.0F, 1.0F, 1.0F, 1.0F,
    // c_out=2: 4 cells = 1 each
    1.0F, 1.0F, 1.0F, 1.0F,
  ]
  let (d_input, _, _) = conv2d_backward(input, d_output, 1, 2, 2, 2, param)
  // For each spatial position (ih, iw), d_input[ch0] = W[0,0] + W[1,0] + W[2,0]
  //   = 0.5 + 1.0 + 3.0 = 4.5
  // d_input[ch1] = W[0,1] + W[1,1] + W[2,1] = 0.5 + 2.0 + 4.0 = 6.5
  for i in 0..<4 {
    if (d_input[i] - 4.5F).abs() > 1.0e-5 {
      fail("d_input[ch0, \{i}] expected 4.5, got \{d_input[i]}")
    }
    if (d_input[4 + i] - 6.5F).abs() > 1.0e-5 {
      fail("d_input[ch1, \{i}] expected 6.5, got \{d_input[4 + i]}")
    }
  }
}

///|
test "conv2d_backward / gradient check d_input vs numerical" {
  // Loss = sum(conv2d_forward(input, ..., param)). For a single in_channel,
  // single out_channel, single batch, simple 1x1 conv: d_input[i] = W * 1 = W.
  // Use a slightly bigger example to test 3x3 kernel + same pad.
  let input : Array[Float] = [
    0.1F, 0.2F, 0.3F, 0.4F,
    0.5F, 0.6F, 0.7F, 0.8F,
    0.9F, 1.0F, 1.1F, 1.2F,
    1.3F, 1.4F, 1.5F, 1.6F,
  ]
  let weight : Array[Float] = [
    // 3x3 all ones
    1.0F, 1.0F, 1.0F,
    1.0F, 1.0F, 1.0F,
    1.0F, 1.0F, 1.0F,
  ]
  let bias : Array[Float] = [0.0F]
  let param = Conv2dParam::new(weight, bias, 1, 1, 3, 3, stride=1, pad=1)
  let eps = 0.001F
  let fwd = fn(inp : Array[Float]) -> Float {
    let out = conv2d_forward(inp, 1, 1, 4, 4, param)
    let mut s : Float = 0.0F
    for i in 0..<out.length() { s = s + out[i] }
    s
  }
  let num = numerical_gradient(input, eps, fwd)
  let ho = (4 + 2 - 3) / 1 + 1  // 4
  let wo = (4 + 2 - 3) / 1 + 1  // 4
  let d_output : Array[Float] = Array::make(ho * wo, 1.0F)
  let (analytic, _, _) = conv2d_backward(input, d_output, 1, 1, 4, 4, param)
  let (max_diff, max_idx) = max_abs_diff(num, analytic)
  // Float32 central-difference has O(eps^2 * curvature) error; for a
  // 9-sum conv with all-1 kernel the tolerance is ~1e-2 at eps=1e-3.
  if max_diff > 1.0e-2 {
    fail("conv2d d_input gradient mismatch: max diff \{max_diff} at \{max_idx} (num=\{num[max_idx]} vs analytic=\{analytic[max_idx]})")
  }
}

///|
test "conv2d_backward / gradient check d_weight vs numerical" {
  // d_weight[co, ci, kh, kw] = sum_{n, ho, wo} d_output[n, co, ho, wo] * input[n, ci, ih, iw]
  // We verify by perturbing each weight and checking d_loss/d_W.
  let n = 1
  let c_in = 1
  let c_out = 1
  let h = 3
  let w = 3
  let kh = 2
  let kw = 2
  let input : Array[Float] = [1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F, 7.0F, 8.0F, 9.0F]
  let weight : Array[Float] = [1.0F, 1.0F, 1.0F, 1.0F]
  let bias : Array[Float] = [0.0F]
  let param = Conv2dParam::new(weight, bias, c_out, c_in, kh, kw, stride=1, pad=0)
  let ho = (h + 0 - kh) / 1 + 1  // 2
  let wo = (w + 0 - kw) / 1 + 1  // 2
  let d_output : Array[Float] = Array::make(n * c_out * ho * wo, 1.0F)
  // Numerical d_weight.
  let eps = 0.001F
  let original_w = weight.copy()
  let num_d_weight : Array[Float] = Array::make(c_out * c_in * kh * kw, 0.0F)
  for k in 0..<(c_out * c_in * kh * kw) {
    weight[k] = original_w[k] + eps
    let y_p = conv2d_forward(input, n, c_in, h, w, param)
    let mut loss_p : Float = 0.0F
    for i in 0..<y_p.length() { loss_p = loss_p + y_p[i] }
    weight[k] = original_w[k] - eps
    let y_m = conv2d_forward(input, n, c_in, h, w, param)
    let mut loss_m : Float = 0.0F
    for i in 0..<y_m.length() { loss_m = loss_m + y_m[i] }
    weight[k] = original_w[k]
    num_d_weight[k] = (loss_p - loss_m) / (2.0F * eps)
  }
  let (_, analytic_d_weight, _) = conv2d_backward(input, d_output, n, c_in, h, w, param)
  let (max_diff, max_idx) = max_abs_diff(num_d_weight, analytic_d_weight)
  if max_diff > 1.0e-2 {
    fail("conv2d d_weight gradient mismatch: max diff \{max_diff} at \{max_idx}")
  }
}

///|
test "conv2d_backward / gradient check d_bias vs numerical" {
  let input : Array[Float] = [1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F, 7.0F, 8.0F, 9.0F]
  // weight must have c_out * c_in * kh * kw = 2 * 1 * 3 * 3 = 18 elements.
  let weight : Array[Float] = Array::make(2 * 1 * 3 * 3, 1.0F)
  let bias : Array[Float] = [0.0F, 0.0F]
  let param = Conv2dParam::new(weight, bias, 2, 1, 3, 3, stride=1, pad=0)
  let ho = 1
  let wo = 1
  let d_output : Array[Float] = [1.0F, 2.0F]
  let (_, _, d_bias) = conv2d_backward(input, d_output, 1, 1, 3, 3, param)
  // d_bias[0] = sum d_output[0..0] = 1
  // d_bias[1] = sum d_output[1..1] = 2
  if (d_bias[0] - 1.0F).abs() > 1.0e-5 { fail("d_bias[0] expected 1, got \{d_bias[0]}") }
  if (d_bias[1] - 2.0F).abs() > 1.0e-5 { fail("d_bias[1] expected 2, got \{d_bias[1]}") }
}