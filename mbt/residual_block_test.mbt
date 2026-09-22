// residual_block_test.mbt — Residual block tests (v0.20.2).

///|
test "residual_block / identity forward output shape = input shape" {
  let b = ResidualBlock::identity(8, 42UL)
  let n = 2
  let input : Array[Float] = Array::make(n * 8 * 4 * 4, 0.5F)
  let (out, _) = residual_block_forward(b, input, n, 8, 4, 4)
  if out.length() != input.length() {
    fail("output length should match input length, got \{out.length()}")
  }
}

///|
test "residual_block / identity zero d_output gives zero gradients" {
  let b = ResidualBlock::identity(4, 42UL)
  let n = 1
  let input : Array[Float] = Array::make(n * 4 * 4 * 4, 0.5F)
  let (_out, cache) = residual_block_forward(b, input, n, 4, 4, 4)
  let d_output : Array[Float] = Array::make(input.length(), 0.0F)
  let (d_input, grads) = residual_block_backward(b, cache, d_output)
  for i in 0..<d_input.length() {
    if d_input[i] != 0.0F { fail("d_input[\{i}] should be 0") }
  }
  if grads.conv1_d_weight.length() != 4 * 4 * 3 * 3 { fail("conv1_d_weight length mismatch") }
  for i in 0..<grads.conv1_d_weight.length() {
    if grads.conv1_d_weight[i] != 0.0F { fail("conv1_d_weight[\{i}] should be 0") }
  }
  for i in 0..<grads.bn1_d_gamma.length() {
    if grads.bn1_d_gamma[i] != 0.0F { fail("bn1_d_gamma[\{i}] should be 0") }
  }
}

///|
test "residual_block / projection forward halves spatial when stride=2" {
  let b = ResidualBlock::projection(8, 16, 2, 42UL)
  let n = 1
  let input : Array[Float] = Array::make(n * 8 * 4 * 4, 0.5F)
  let (out, _) = residual_block_forward(b, input, n, 8, 4, 4)
  // 4x4 -> with stride 2 + pad 1, output h = (4+2-3)/2+1 = 2.
  // c_out = 16. Output length = 1*16*2*2 = 64.
  if out.length() != 64 {
    fail("projection output should be 64, got \{out.length()}")
  }
}

///|
test "residual_block / projection backward d_input shape matches input" {
  let b = ResidualBlock::projection(4, 8, 2, 42UL)
  let n = 2
  let input : Array[Float] = Array::make(n * 4 * 4 * 4, 0.5F)
  let (out, cache) = residual_block_forward(b, input, n, 4, 4, 4)
  let d_output : Array[Float] = Array::make(out.length(), 1.0F)
  let (d_input, grads) = residual_block_backward(b, cache, d_output)
  if d_input.length() != input.length() {
    fail("d_input length should match input length")
  }
  // Projection grads should be populated.
  match grads.shortcut_conv_d_weight {
    Some(_) => ()
    None => fail("shortcut_conv_d_weight should be Some for projection block")
  }
  match grads.shortcut_bn_d_gamma {
    Some(_) => ()
    None => fail("shortcut_bn_d_gamma should be Some for projection block")
  }
}

///|
test "residual_block / identity numerical gradient check on conv1 weights" {
  // Central-difference check that the analytic conv1_d_weight
  // matches the numerical gradient for one specific weight element.
  let b0 = ResidualBlock::identity(2, 1234UL)
  let n = 1
  let input : Array[Float] = [0.5F, 0.3F, -0.2F, 0.8F, 0.1F, -0.4F, 0.6F, 0.2F]
  let (_out, cache) = residual_block_forward(b0, input, n, 2, 2, 2)
  // Pick one weight (say index 0 of conv1) and perturb.
  let eps = 0.001F
  // Reference forward at w[0] + eps.
  let b_plus = ResidualBlock::identity(2, 1234UL)
  // Copy weights then perturb index 0 of conv1.
  for i in 0..<b_plus.conv1.weight.length() {
    b_plus.conv1.weight[i] = b0.conv1.weight[i]
  }
  b_plus.conv1.weight[0] = b0.conv1.weight[0] + eps
  let (out_plus, _) = residual_block_forward(b_plus, input, n, 2, 2, 2)
  // Reference forward at w[0] - eps.
  let b_minus = ResidualBlock::identity(2, 1234UL)
  for i in 0..<b_minus.conv1.weight.length() {
    b_minus.conv1.weight[i] = b0.conv1.weight[i]
  }
  b_minus.conv1.weight[0] = b0.conv1.weight[0] - eps
  let (out_minus, _) = residual_block_forward(b_minus, input, n, 2, 2, 2)
  let mut sum_plus = 0.0F
  let mut sum_minus = 0.0F
  for k in 0..<out_plus.length() {
    sum_plus = sum_plus + out_plus[k]
    sum_minus = sum_minus + out_minus[k]
  }
  let num_grad = (sum_plus - sum_minus) / (2.0F * eps)
  let d_output : Array[Float] = Array::make(_out.length(), 1.0F)
  let (_d_input, grads) = residual_block_backward(b0, cache, d_output)
  let ana_grad = grads.conv1_d_weight[0]
  if (ana_grad - num_grad).abs() > 0.01F {
    fail("conv1_d_weight[0] analytic \{ana_grad} vs numerical \{num_grad}")
  }
}