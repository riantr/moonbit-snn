// layer_norm_test.mbt — LayerNorm forward + backward tests (v0.17.1).

///|
test "layer_norm_forward / output shape equals input shape" {
  let ln = LayerNorm::new(3, 4, 5)
  let input : Array[Float] = Array::make(2 * 3 * 4 * 5, 0.5F)
  let (out, _) = layer_norm_forward(input, 2, 3, 4, 5, ln)
  if out.length() != input.length() {
    fail("output length should equal input length, got \{out.length()}")
  }
}

///|
test "layer_norm_forward / per-sample mean ~ 0 and std ~ 1" {
  // Each sample has a distinct mean and std; after LN they should all
  // be normalised to mean=0, std=1 independently.
  let ln = LayerNorm::new(1, 1, 4) // 4 features per sample
  let n = 3
  let c = 1
  let h = 1
  let w = 4
  // Sample 0: mean = 10, std = 2.
  // Sample 1: mean = 0, std = 1.
  // Sample 2: mean = -5, std = 0.5.
  let input : Array[Float] = [
    8.0F, 10.0F, 10.0F, 12.0F, // mean=10, std=sqrt(2)
    -1.0F, 0.0F, 0.0F, 1.0F,   // mean=0, std=sqrt(0.5)
    -5.5F, -5.0F, -5.0F, -4.5F, // mean=-5, std=sqrt(0.0625)=0.25
  ]
  let (out, _) = layer_norm_forward(input, n, c, h, w, ln)
  for ni in 0..<n {
    let mut sum = 0.0F
    let mut sum_sq = 0.0F
    for k in 0..<4 {
      let idx = ni * 4 + k
      sum = sum + out[idx]
      sum_sq = sum_sq + out[idx] * out[idx]
    }
    let m = sum / 4.0F
    let v = sum_sq / 4.0F - m * m
    let s = sqrtf(v)
    if m.abs() > 0.01F {
      fail("sample \{ni} mean should be ~0, got \{m}")
    }
    if (s - 1.0F).abs() > 0.05F {
      fail("sample \{ni} std should be ~1, got \{s}")
    }
  }
}

///|
test "layer_norm_forward / different samples normalised independently" {
  // Two samples with opposite scales; LN should normalise each one,
  // not jointly across the batch.
  let ln = LayerNorm::new(1, 1, 2)
  let input : Array[Float] = [100.0F, 200.0F, -100.0F, -200.0F]
  let (out, _) = layer_norm_forward(input, 2, 1, 1, 2, ln)
  // Sample 0: mean=150, std=sqrt((50)^2 + (50)^2)/2 = 50.
  //   y[0] = (100 - 150) / 50 = -1
  //   y[1] = (200 - 150) / 50 =  1
  // Sample 1: mean=-150, std=50.
  //   y[2] = (-100 - (-150)) / 50 =  1
  //   y[3] = (-200 - (-150)) / 50 = -1
  if (out[0] - -1.0F).abs() > 0.001F { fail("out[0] expected -1, got \{out[0]}") }
  if (out[1] - 1.0F).abs() > 0.001F { fail("out[1] expected 1, got \{out[1]}") }
  if (out[2] - 1.0F).abs() > 0.001F { fail("out[2] expected 1, got \{out[2]}") }
  if (out[3] - -1.0F).abs() > 0.001F { fail("out[3] expected -1, got \{out[3]}") }
}

///|
test "layer_norm_forward / gamma and beta are applied per feature" {
  // gamma = [2, 0.5], beta = [3, -1].
  // For x = [1, -1] (already mean=0, std=1): y = [2*1+3, 0.5*(-1)+(-1)] = [5, -1.5].
  let ln = LayerNorm::with_gamma_beta([2.0F, 0.5F], [3.0F, -1.0F], 1, 1, 2)
  let input : Array[Float] = [1.0F, -1.0F]
  let (out, _) = layer_norm_forward(input, 1, 1, 1, 2, ln)
  if (out[0] - 5.0F).abs() > 0.001F { fail("out[0] expected 5, got \{out[0]}") }
  if (out[1] - -1.5F).abs() > 0.001F { fail("out[1] expected -1.5, got \{out[1]}") }
}

///|
test "layer_norm_backward / d_gamma and d_beta shape = c*h*w" {
  let ln = LayerNorm::new(3, 2, 2)
  let input : Array[Float] = Array::make(2 * 3 * 2 * 2, 0.5F)
  let (out, cache) = layer_norm_forward(input, 2, 3, 2, 2, ln)
  let d_output : Array[Float] = Array::make(out.length(), 1.0F)
  let (_d_input, d_gamma, d_beta) = layer_norm_backward(d_output, cache, ln)
  if d_gamma.length() != 12 { fail("d_gamma length should be 12, got \{d_gamma.length()}") }
  if d_beta.length() != 12 { fail("d_beta length should be 12, got \{d_beta.length()}") }
}

///|
test "layer_norm_backward / d_input shape equals input shape" {
  let ln = LayerNorm::new(3, 2, 2)
  let input : Array[Float] = Array::make(2 * 3 * 2 * 2, 0.5F)
  let (out, cache) = layer_norm_forward(input, 2, 3, 2, 2, ln)
  let d_output : Array[Float] = Array::make(out.length(), 1.0F)
  let (d_input, _, _) = layer_norm_backward(d_output, cache, ln)
  if d_input.length() != input.length() {
    fail("d_input length should be \{input.length()}, got \{d_input.length()}")
  }
}

///|
test "layer_norm_backward / zero d_output gives zero gradients" {
  let ln = LayerNorm::new(2, 1, 2)
  let input : Array[Float] = Array::make(3 * 2 * 1 * 2, 0.5F)
  let (_, cache) = layer_norm_forward(input, 3, 2, 1, 2, ln)
  let d_output : Array[Float] = Array::make(input.length(), 0.0F)
  let (d_input, d_gamma, d_beta) = layer_norm_backward(d_output, cache, ln)
  for i in 0..<d_input.length() {
    if d_input[i] != 0.0F { fail("d_input[\{i}] should be 0") }
  }
  for i in 0..<d_gamma.length() {
    if d_gamma[i] != 0.0F { fail("d_gamma[\{i}] should be 0") }
    if d_beta[i] != 0.0F { fail("d_beta[\{i}] should be 0") }
  }
}

///|
test "layer_norm_backward / d_input gradient check vs numerical" {
  // 2 samples × 1 channel × 1 H × 3 features = 6 elements.
  let ln = LayerNorm::new(1, 1, 3)
  let n = 2
  let c = 1
  let h = 1
  let w = 3
  let input : Array[Float] = [1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F]
  let (out, cache) = layer_norm_forward(input, n, c, h, w, ln)
  let d_output : Array[Float] = Array::make(out.length(), 1.0F)
  let (d_input, _d_gamma, _d_beta) = layer_norm_backward(d_output, cache, ln)
  // Numerical gradient.
  let eps = 0.001F
  for i in 0..<input.length() {
    let input_plus : Array[Float] = Array::make(input.length(), 0.0F)
    let input_minus : Array[Float] = Array::make(input.length(), 0.0F)
    for k in 0..<input.length() {
      input_plus[k] = input[k]
      input_minus[k] = input[k]
    }
    input_plus[i] = input[i] + eps
    input_minus[i] = input[i] - eps
    let (out_plus, _) = layer_norm_forward(input_plus, n, c, h, w, ln)
    let (out_minus, _) = layer_norm_forward(input_minus, n, c, h, w, ln)
    let mut sum_plus = 0.0F
    let mut sum_minus = 0.0F
    for k in 0..<out_plus.length() {
      sum_plus = sum_plus + out_plus[k]
      sum_minus = sum_minus + out_minus[k]
    }
    let num_grad = (sum_plus - sum_minus) / (2.0F * eps)
    if (d_input[i] - num_grad).abs() > 0.01F {
      fail("d_input[\{i}] analytical \{d_input[i]} vs numerical \{num_grad}")
    }
  }
}