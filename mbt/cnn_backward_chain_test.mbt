// cnn_backward_chain_test.mbt 鈥?end-to-end mini-CNN chain backward +
// gradient check (v0.12.2).
//
// Chain: Conv2d -> ReLU -> MaxPool2d -> Flatten -> Linear.
// Loss = sum(output).
//
// We backprop by hand:
//   d_out_linear = ones(n * out_features)
//   d_flat       = linear_backward_input(...)  // (computed internally)
//   d_pool       = flatten_backward(...)
//   d_relu       = maxpool2d_backward(...)
//   d_conv       = relu_backward(input_to_relu, d_pool)
//   d_input      = conv2d_backward_input(...)
//   d_weight     = conv2d_backward_weight(...)
//   d_bias       = conv2d_backward_bias(...)
//
// Then verify d_input matches a numerical gradient computed via
// perturbing each input element by ±eps.

///|
test "cnn_backward_chain / gradient check d_input end-to-end" {
  // N=1, C_in=1, H=W=4.
  // Conv 3x3 stride=1 pad=1 (same), C_out=2 -> (1, 2, 4, 4).
  // ReLU.
  // MaxPool 2x2 stride=2 -> (1, 2, 2, 2).
  // Flatten -> (1, 8).
  // Linear (in=8, out=3) -> (1, 3).
  let input : Array[Float] = [
    0.1F, 0.2F, 0.3F, 0.4F,
    0.5F, 0.6F, 0.7F, 0.8F,
    0.9F, 1.0F, 1.1F, 1.2F,
    1.3F, 1.4F, 1.5F, 1.6F,
  ]
  // 3x3 conv weight, c_in=1, c_out=2 -> 2*1*3*3 = 18.
  let conv_weight : Array[Float] = Array::make(18, 0.5F)
  let conv_bias : Array[Float] = [0.0F, 0.0F]
  let conv_param = Conv2dParam::new(
    conv_weight, conv_bias, 2, 1, 3, 3, stride=1, pad=1,
  )
  let linear_weight : Array[Float] = Array::make(3 * 8, 0.1F)
  let linear_bias : Array[Float] = [0.0F, 0.0F, 0.0F]
  let linear_param = LinearParam::new(linear_weight, linear_bias, 8, 3)

  // ---- Forward pass ----
  let conv_out = conv2d_forward(input, 1, 1, 4, 4, conv_param)
  let relu_out = relu_forward(conv_out)
  let pool_param = MaxPool2dParam::new(2, 2, stride=2, pad=0)
  let (pool_out, argmax) = maxpool2d_forward_with_idx(
    relu_out, 1, 2, 4, 4, pool_param,
  )
  let flat_out = flatten_forward(pool_out, 1, 2, 2, 2)

  // ---- Backward pass ----
  // d_loss / d_linear_out = ones.
  let n_out = 3
  let d_linear_out : Array[Float] = Array::make(n_out, 1.0F)
  let (d_flat, d_lin_weight, d_lin_bias) = linear_backward(
    flat_out, d_linear_out, 1, linear_param,
  )
  let _ = d_lin_weight
  let _ = d_lin_bias
  // d_flat -> d_pool (reshape identity, but flatten_backward handles it).
  let d_pool = flatten_backward(d_flat, 1, 2, 2, 2)
  // d_pool -> d_relu via maxpool backward.
  let d_relu = maxpool2d_backward(d_pool, argmax, 1, 2, 4, 4, pool_param)
  // d_relu -> d_conv via relu backward (need pre-activation conv_out).
  let d_conv = relu_backward(conv_out, d_relu)
  // d_conv -> d_input via conv backward (only need d_input).
  let (d_input, _, _) = conv2d_backward(input, d_conv, 1, 1, 4, 4, conv_param)
  // (Note: conv_out had bias added; relu_forward was applied AFTER that.
  // The d_conv here is gradient w.r.t. conv pre-activation output, which
  // is what conv2d_backward expects.)

  // ---- Numerical gradient check ----
  let eps = 0.001F
  let fwd = fn(inp : Array[Float]) -> Float {
    let c_out2 = conv2d_forward(inp, 1, 1, 4, 4, conv_param)
    let r_out = relu_forward(c_out2)
    let (p_out, _) = maxpool2d_forward_with_idx(r_out, 1, 2, 4, 4, pool_param)
    let f_out = flatten_forward(p_out, 1, 2, 2, 2)
    let l_out = linear_forward(f_out, 1, linear_param)
    let mut s : Float = 0.0F
    for i in 0..<l_out.length() { s = s + l_out[i] }
    s
  }
  let num = numerical_gradient(input, eps, fwd)
  let (max_diff, max_idx) = max_abs_diff(num, d_input)
  if max_diff > 1.0e-2 {
    fail("cnn chain d_input mismatch: max diff \{max_diff} at \{max_idx} (num=\{num[max_idx]} vs analytic=\{d_input[max_idx]})")
  }
}

///|
test "cnn_backward_chain / bit-exact: gradient values match across re-runs" {
  // Two parallel runs of the same chain -> same gradient.
  let input : Array[Float] = [
    0.5F, -0.2F, 0.1F, 0.7F,
    -0.3F, 0.9F, -0.4F, 0.6F,
    0.2F, -0.1F, 0.8F, -0.5F,
    0.4F, 0.3F, -0.6F, 0.1F,
  ]
  let conv_weight : Array[Float] = [
    0.1F, 0.2F, 0.3F,
    0.4F, 0.5F, 0.6F,
    0.7F, 0.8F, 0.9F,
    -0.1F, -0.2F, -0.3F,
    -0.4F, -0.5F, -0.6F,
    -0.7F, -0.8F, -0.9F,
  ]
  let conv_bias : Array[Float] = [0.0F, 0.0F]
  let conv_param = Conv2dParam::new(
    conv_weight, conv_bias, 2, 1, 3, 3, stride=1, pad=0,
  )
  let linear_weight : Array[Float] = [
    // in_features=2 (from flatten (1,2,1,1)), out_features=3.
    0.1F, 0.2F,
    0.8F, 0.7F,
    0.2F, 0.4F,
  ]
  let linear_bias : Array[Float] = [0.0F, 0.0F, 0.0F]
  let linear_param = LinearParam::new(linear_weight, linear_bias, 2, 3)

  // Run chain twice; verify d_input is bit-exact.
  let run_once = fn() -> Array[Float] {
    let conv_out = conv2d_forward(input, 1, 1, 4, 4, conv_param)
    let relu_out = relu_forward(conv_out)
    let pool_param = MaxPool2dParam::new(2, 2, stride=2, pad=0)
    let (pool_out, argmax) = maxpool2d_forward_with_idx(
      relu_out, 1, 2, 2, 2, pool_param,
    )
    let flat_out = flatten_forward(pool_out, 1, 2, 1, 1)
    let d_linear_out : Array[Float] = Array::make(3, 1.0F)
    let (d_flat, _, _) = linear_backward(flat_out, d_linear_out, 1, linear_param)
    let d_pool = flatten_backward(d_flat, 1, 2, 1, 1)
    let d_relu = maxpool2d_backward(d_pool, argmax, 1, 2, 2, 2, pool_param)
    let d_conv = relu_backward(conv_out, d_relu)
    let (d_input, _, _) = conv2d_backward(input, d_conv, 1, 1, 4, 4, conv_param)
    d_input
  }
  let a = run_once()
  let b = run_once()
  if a.length() != b.length() {
    fail("length mismatch: a=\{a.length()} b=\{b.length()}")
  }
  for i in 0..<a.length() {
    if a[i] != b[i] {
      fail("d_input[\{i}] not bit-exact: a=\{a[i]} b=\{b[i]}")
    }
  }
}

///|
test "cnn_backward_chain / gradient check d_weight (conv weights) end-to-end" {
  // Same chain but verify d_weight matches numerical gradient.
  let n : Int = 1
  let c_in : Int = 1
  let c_out : Int = 1
  let h : Int = 3
  let w : Int = 3
  let kh : Int = 2
  let kw : Int = 2
  let input : Array[Float] = [1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F, 7.0F, 8.0F, 9.0F]
  let conv_weight : Array[Float] = [1.0F, 1.0F, 1.0F, 1.0F]
  let conv_bias : Array[Float] = [0.0F]
  let conv_param = Conv2dParam::new(
    conv_weight, conv_bias, c_out, c_in, kh, kw, stride=1, pad=0,
  )
  let linear_param = LinearParam::new(
    Array::make(1 * 1, 1.0F), [0.0F], 1, 1,
  )
  // Run the chain end-to-end.
  let chain = fn() -> Float {
    let c_out2 = conv2d_forward(input, n, c_in, h, w, conv_param)
    let r_out = relu_forward(c_out2)
    // After conv 2x2 valid on 3x3 -> 2x2 output.
    let pool_param = MaxPool2dParam::new(2, 2, stride=2, pad=0)
    let (p_out, _) = maxpool2d_forward_with_idx(r_out, 1, 1, 2, 2, pool_param)
    let f_out = flatten_forward(p_out, 1, 1, 1, 1)
    let l_out = linear_forward(f_out, 1, linear_param)
    l_out[0]
  }
  let _ = chain()
  let eps = 0.001F
  let original_w = conv_weight.copy()
  let num_d_weight : Array[Float] = Array::make(c_out * c_in * kh * kw, 0.0F)
  for k in 0..<(c_out * c_in * kh * kw) {
    conv_weight[k] = original_w[k] + eps
    let loss_p = chain()
    conv_weight[k] = original_w[k] - eps
    let loss_m = chain()
    conv_weight[k] = original_w[k]
    num_d_weight[k] = (loss_p - loss_m) / (2.0F * eps)
  }
  // Analytical d_weight: via full backward chain.
  let conv_out = conv2d_forward(input, n, c_in, h, w, conv_param)
  let relu_out = relu_forward(conv_out)
  let pool_param = MaxPool2dParam::new(2, 2, stride=2, pad=0)
  let (pool_out, argmax) = maxpool2d_forward_with_idx(
    relu_out, 1, 1, 2, 2, pool_param,
  )
  let flat_out = flatten_forward(pool_out, 1, 1, 1, 1)
  let d_linear_out : Array[Float] = [1.0F]
  let (d_flat, _, _) = linear_backward(flat_out, d_linear_out, 1, linear_param)
  let d_pool = flatten_backward(d_flat, 1, 1, 1, 1)
  let d_relu = maxpool2d_backward(d_pool, argmax, 1, 1, 2, 2, pool_param)
  let d_conv = relu_backward(conv_out, d_relu)
  let (_, analytic_d_weight, _) = conv2d_backward(
    input, d_conv, n, c_in, h, w, conv_param,
  )
  let (max_diff, max_idx) = max_abs_diff(num_d_weight, analytic_d_weight)
  if max_diff > 1.0e-2 {
    fail("cnn chain d_weight mismatch: max diff \{max_diff} at \{max_idx}")
  }
}