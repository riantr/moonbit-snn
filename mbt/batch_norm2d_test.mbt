// batch_norm2d_test.mbt — BatchNorm2d forward + backward tests (v0.17.0).

///|
test "batch_norm2d_forward / output shape equals input shape" {
  let bn = BatchNorm2d::new(3)
  let input : Array[Float] = Array::make(2 * 3 * 4 * 5, 0.5F)
  let (out, _) = batch_norm2d_forward(input, 2, 3, 4, 5, bn)
  if out.length() != input.length() {
    fail("output length should equal input length, got \{out.length()}")
  }
}

///|
test "batch_norm2d_forward / per-channel mean ~ 0 and std ~ 1 after normalisation" {
  // Build an input with different per-channel statistics:
  //   channel 0: mean=10, std=2 (after subtracting mean should be 0)
  //   channel 1: mean=0,  std=1
  //   channel 2: mean=-5, std=0.5
  let bn = BatchNorm2d::new(3) // gamma=1, beta=0
  let n = 4
  let c = 3
  let h = 2
  let w = 2
  let input : Array[Float] = Array::make(n * c * h * w, 0.0F)
  for ni in 0..<n {
    for hi in 0..<h {
      for wi in 0..<w {
        let idx00 = ni * c * h * w + 0 * h * w + hi * w + wi
        let idx01 = ni * c * h * w + 1 * h * w + hi * w + wi
        let idx02 = ni * c * h * w + 2 * h * w + hi * w + wi
        // Channel 0: 10 ± small jitter.
        input[idx00] = 10.0F + Float::from_int(ni + hi + wi) * 0.1F
        // Channel 1: 0 ± small jitter.
        input[idx01] = Float::from_int(ni + hi + wi) * 0.1F
        // Channel 2: -5 ± small jitter.
        input[idx02] = -5.0F + Float::from_int(ni + hi + wi) * 0.1F
      }
    }
  }
  let (out, _) = batch_norm2d_forward(input, n, c, h, w, bn)
  // Compute per-channel mean / std of output; should be ~0 and ~1.
  for ci in 0..<c {
    let mut sum = 0.0F
    let mut sum_sq = 0.0F
    let mut count = 0
    for ni in 0..<n {
      for hi in 0..<h {
        for wi in 0..<w {
          let idx = ni * c * h * w + ci * h * w + hi * w + wi
          let v = out[idx]
          sum = sum + v
          sum_sq = sum_sq + v * v
          count = count + 1
        }
      }
    }
    let m = sum / Float::from_int(count)
    let v = sum_sq / Float::from_int(count) - m * m
    let s = sqrtf(v)
    if m.abs() > 0.01F {
      fail("channel \{ci} mean should be ~0, got \{m}")
    }
    if (s - 1.0F).abs() > 0.05F {
      fail("channel \{ci} std should be ~1, got \{s}")
    }
  }
}

///|
test "batch_norm2d_forward / gamma and beta are applied correctly" {
  // With gamma=2, beta=3: y = 2 * (x-mean)*inv_std + 3.
  // For a zero-mean input batch, mean=0 and inv_std~1, so y ~ 2*x + 3.
  let bn = BatchNorm2d::with_gamma_beta([2.0F], [3.0F])
  let input : Array[Float] = [1.0F, -1.0F, 1.0F, -1.0F]
  let (out, _) = batch_norm2d_forward(input, 4, 1, 1, 1, bn)
  // Per-channel mean over {1, -1, 1, -1} = 0; std = sqrt(1) = 1.
  // y[n] = 2 * (x[n] - 0) / 1 + 3 = 2*x[n] + 3.
  // x[0]=1 -> y=5, x[1]=-1 -> y=1, x[2]=1 -> y=5, x[3]=-1 -> y=1.
  let expected : Array[Float] = [5.0F, 1.0F, 5.0F, 1.0F]
  for i in 0..<4 {
    if (out[i] - expected[i]).abs() > 0.01F {
      fail("y[\{i}] expected \{expected[i]}, got \{out[i]}")
    }
  }
}

///|
test "batch_norm2d_forward / running stats change during training" {
  // After one forward pass with momentum=0.1, the running stats should
  // match (1 - momentum) * 0 + momentum * batch_stats.
  let bn = BatchNorm2d::new(1, momentum=0.1)
  let input : Array[Float] = [10.0F, 12.0F, 14.0F, 16.0F]
  let _ = batch_norm2d_forward(input, 4, 1, 1, 1, bn)
  // Batch mean = 13, var = ((10-13)^2 + (12-13)^2 + (14-13)^2 + (16-13)^2) / 4
  //            = (9 + 1 + 1 + 9) / 4 = 5.
  // PyTorch convention: running = (1 - momentum) * running + momentum * batch.
  // First update with momentum=0.1: running_mean = 0.1 * 13 = 1.3.
  if (bn.running_mean[0] - 1.3F).abs() > 0.0001F {
    fail("running_mean should be 1.3 (= 0.1 * 13), got \{bn.running_mean[0]}")
  }
  if (bn.running_var[0] - 0.5F).abs() > 0.0001F {
    fail("running_var should be 0.5 (= 0.1 * 5), got \{bn.running_var[0]}")
  }
}

///|
test "batch_norm2d_backward / d_gamma and d_beta shape" {
  let bn = BatchNorm2d::new(3)
  let input : Array[Float] = Array::make(2 * 3 * 2 * 2, 0.5F)
  let (out, cache) = batch_norm2d_forward(input, 2, 3, 2, 2, bn)
  let d_output : Array[Float] = Array::make(out.length(), 1.0F)
  let (_d_input, d_gamma, d_beta) = batch_norm2d_backward(d_output, cache, bn)
  if d_gamma.length() != 3 { fail("d_gamma length should be 3") }
  if d_beta.length() != 3 { fail("d_beta length should be 3") }
}

///|
test "batch_norm2d_backward / d_input shape equals input shape" {
  let bn = BatchNorm2d::new(3)
  let input : Array[Float] = Array::make(2 * 3 * 2 * 2, 0.7F)
  let (out, cache) = batch_norm2d_forward(input, 2, 3, 2, 2, bn)
  let d_output : Array[Float] = Array::make(out.length(), 1.0F)
  let (d_input, _, _) = batch_norm2d_backward(d_output, cache, bn)
  if d_input.length() != input.length() {
    fail("d_input length should be \{input.length()}, got \{d_input.length()}")
  }
}

///|
test "batch_norm2d_backward / zero d_output gives zero gradients" {
  let bn = BatchNorm2d::new(2)
  let input : Array[Float] = Array::make(2 * 2 * 2 * 2, 0.5F)
  let (_, cache) = batch_norm2d_forward(input, 2, 2, 2, 2, bn)
  let d_output : Array[Float] = Array::make(input.length(), 0.0F)
  let (d_input, d_gamma, d_beta) = batch_norm2d_backward(d_output, cache, bn)
  for i in 0..<d_input.length() {
    if d_input[i] != 0.0F { fail("d_input[\{i}] should be 0") }
  }
  for i in 0..<d_gamma.length() {
    if d_gamma[i] != 0.0F { fail("d_gamma[\{i}] should be 0") }
    if d_beta[i] != 0.0F { fail("d_beta[\{i}] should be 0") }
  }
}

///|
test "batch_norm2d_backward / d_input gradient check vs numerical" {
  // Single channel, simple input. Use finite-difference central
  // differences (eps=1e-3, tol=1e-2 due to Float32 noise).
  let bn = BatchNorm2d::new(1)
  let n = 4
  let c = 1
  let h = 1
  let w = 1
  let input : Array[Float] = [1.0F, 2.0F, 3.0F, 4.0F]
  let (out, cache) = batch_norm2d_forward(input, n, c, h, w, bn)
  // Loss = sum(out).  d_loss / d_out = 1 for every element.
  let d_output : Array[Float] = Array::make(out.length(), 1.0F)
  let (d_input, _d_gamma, _d_beta) = batch_norm2d_backward(d_output, cache, bn)
  // Numerical gradient: d/dx[i] of sum(out) ≈ (loss(x+eps) - loss(x-eps)) / 2eps.
  let eps = 0.001F
  for i in 0..<input.length() {
    // Build input with x[i] perturbed by +/- eps.
    let input_plus : Array[Float] = Array::make(input.length(), 0.0F)
    let input_minus : Array[Float] = Array::make(input.length(), 0.0F)
    for k in 0..<input.length() {
      input_plus[k] = input[k]
      input_minus[k] = input[k]
    }
    input_plus[i] = input[i] + eps
    input_minus[i] = input[i] - eps
    let (out_plus, _) = batch_norm2d_forward(input_plus, n, c, h, w, bn)
    let (out_minus, _) = batch_norm2d_forward(input_minus, n, c, h, w, bn)
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