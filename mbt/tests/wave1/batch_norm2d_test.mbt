// batch_norm2d_test.mbt — BatchNorm2d forward + backward tests (v0.17.0).

///|
test "batch_norm2d_forward / output shape equals input shape" {
  let bn = @snn_mbt.BatchNorm2d::new(3)
  let input : Array[Float] = Array::make(2 * 3 * 4 * 5, 0.5F)
  let (out, _) = @snn_mbt.batch_norm2d_forward(input, 2, 3, 4, 5, bn)
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
  let bn = @snn_mbt.BatchNorm2d::new(3) // gamma=1, beta=0
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
  let (out, _) = @snn_mbt.batch_norm2d_forward(input, n, c, h, w, bn)
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
    let s = @snn_mbt.sqrtf(v)
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
  let bn = @snn_mbt.BatchNorm2d::with_gamma_beta([2.0F], [3.0F])
  let input : Array[Float] = [1.0F, -1.0F, 1.0F, -1.0F]
  let (out, _) = @snn_mbt.batch_norm2d_forward(input, 4, 1, 1, 1, bn)
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
  let bn = @snn_mbt.BatchNorm2d::new(1, momentum=0.1)
  let input : Array[Float] = [10.0F, 12.0F, 14.0F, 16.0F]
  let _ = @snn_mbt.batch_norm2d_forward(input, 4, 1, 1, 1, bn)
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
  let bn = @snn_mbt.BatchNorm2d::new(3)
  let input : Array[Float] = Array::make(2 * 3 * 2 * 2, 0.5F)
  let (out, cache) = @snn_mbt.batch_norm2d_forward(input, 2, 3, 2, 2, bn)
  let d_output : Array[Float] = Array::make(out.length(), 1.0F)
  let (_d_input, d_gamma, d_beta) = @snn_mbt.batch_norm2d_backward(d_output, cache, bn)
  if d_gamma.length() != 3 { fail("d_gamma length should be 3") }
  if d_beta.length() != 3 { fail("d_beta length should be 3") }
}

///|
test "batch_norm2d_backward / d_input shape equals input shape" {
  let bn = @snn_mbt.BatchNorm2d::new(3)
  let input : Array[Float] = Array::make(2 * 3 * 2 * 2, 0.7F)
  let (out, cache) = @snn_mbt.batch_norm2d_forward(input, 2, 3, 2, 2, bn)
  let d_output : Array[Float] = Array::make(out.length(), 1.0F)
  let (d_input, _, _) = @snn_mbt.batch_norm2d_backward(d_output, cache, bn)
  if d_input.length() != input.length() {
    fail("d_input length should be \{input.length()}, got \{d_input.length()}")
  }
}

///|
test "batch_norm2d_backward / zero d_output gives zero gradients" {
  let bn = @snn_mbt.BatchNorm2d::new(2)
  let input : Array[Float] = Array::make(2 * 2 * 2 * 2, 0.5F)
  let (_, cache) = @snn_mbt.batch_norm2d_forward(input, 2, 2, 2, 2, bn)
  let d_output : Array[Float] = Array::make(input.length(), 0.0F)
  let (d_input, d_gamma, d_beta) = @snn_mbt.batch_norm2d_backward(d_output, cache, bn)
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
  let bn = @snn_mbt.BatchNorm2d::new(1)
  let n = 4
  let c = 1
  let h = 1
  let w = 1
  let input : Array[Float] = [1.0F, 2.0F, 3.0F, 4.0F]
  let (out, cache) = @snn_mbt.batch_norm2d_forward(input, n, c, h, w, bn)
  // Loss = sum(out).  d_loss / d_out = 1 for every element.
  let d_output : Array[Float] = Array::make(out.length(), 1.0F)
  let (d_input, _d_gamma, _d_beta) = @snn_mbt.batch_norm2d_backward(d_output, cache, bn)
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
    let (out_plus, _) = @snn_mbt.batch_norm2d_forward(input_plus, n, c, h, w, bn)
    let (out_minus, _) = @snn_mbt.batch_norm2d_forward(input_minus, n, c, h, w, bn)
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

///|
/// Objective for the affine-parameter gates: the BN output weighted by a fixed
/// vector, so every channel gets a non-zero, non-uniform upstream gradient.
fn bn_obj(
  input : Array[Float],
  n : Int,
  c : Int,
  h : Int,
  w : Int,
  bn : @snn_mbt.BatchNorm2d,
  wt : Array[Float],
) -> Float {
  let (out, _cache) = @snn_mbt.batch_norm2d_forward(input, n, c, h, w, bn)
  let mut s = 0.0F
  for i in 0..<out.length() {
    s = s + out[i] * wt[i]
  }
  s
}

///|
/// Fixture whose channels have deliberately WILDLY different scales, so
/// `inv_std = 1/sqrt(var + eps)` differs by ~100x across channels.
///
/// THIS IS THE POINT OF THE TEST. `d_gamma[c] = sum(d_y * xhat)` and
/// `xhat = x_centered * inv_std`; the `* inv_std` factor was missing from
/// `batch_norm2d_backward` for its whole life, and every pre-existing gate
/// missed it because they checked d_gamma's SHAPE and never its value, and the
/// d_input gate uses a single channel. On a fixture whose variance is near 1,
/// `inv_std ~ 1` and the bug is arithmetically invisible. The three channels
/// here have scales 1.0, 0.05 and 20.0, so no single omitted factor can hide.
/// The TARNet model-level sweep is what finally caught it, on `bn_seg`.
fn bn_scaled_fixture() -> (@snn_mbt.BatchNorm2d, Array[Float], Array[Float], Int, Int, Int, Int) {
  let n = 4
  let c = 3
  let h = 2
  let w = 2
  let scales = [1.0F, 0.05F, 20.0F]
  let input : Array[Float] = Array::make(n * c * h * w, 0.0F)
  for ni in 0..<n {
    for ci in 0..<c {
      let hw = h * w
      for k in 0..<hw {
        let idx = ni * c * h * w + ci * h * w + k
        // a varying pattern, not a constant: a constant channel has xhat == 0
        // and would measure nothing
        input[idx] = scales[ci] *
          (1.0F + 0.37F * Float::from_int(k) + 0.11F * Float::from_int(ni))
      }
    }
  }
  let wt : Array[Float] = Array::make(n * c * h * w, 0.0F)
  for i in 0..<wt.length() {
    wt[i] = 1.0F + 0.23F * Float::from_int(i % 7)
  }
  (@snn_mbt.BatchNorm2d::new(c), input, wt, n, c, h, w)
}

///|
/// Ratio of the largest to the smallest per-channel standard deviation in the
/// fixture. This is the knob that decides whether the d_gamma gate can see a
/// missing `inv_std`: the factor is 1/sqrt(var + eps), so channels whose
/// spreads differ by R have inv_stds differing by up to R.
fn bn_channel_scale_ratio(input : Array[Float], n : Int, c : Int, hw : Int) -> Float {
  let mut lo = 1.0e30F
  let mut hi = 0.0F
  for ci in 0..<c {
    let mut s = 0.0F
    let mut s2 = 0.0F
    let mut cnt = 0.0F
    for ni in 0..<n {
      for k in 0..<hw {
        let v = input[ni * c * hw + ci * hw + k]
        s = s + v
        s2 = s2 + v * v
        cnt = cnt + 1.0F
      }
    }
    let mean = s / cnt
    let variance = s2 / cnt - mean * mean
    let sd = if variance > 0.0F { variance.sqrt() } else { 0.0F }
    if sd < lo {
      lo = sd
    }
    if sd > hi {
      hi = sd
    }
  }
  if lo > 0.0F { hi / lo } else { 0.0F }
}

///|
test "batch_norm2d_backward / d_gamma matches numerical on wildly scaled channels" {
  let (bn0, input, wt, n, c, h, w) = bn_scaled_fixture()
  // Non-vacuity: if every channel had variance near 1, inv_std ~ 1 and a
  // missing inv_std factor in d_gamma would be arithmetically invisible --
  // which is exactly how the bug survived every earlier gate.
  let ratio = bn_channel_scale_ratio(input, n, c, h * w)
  if ratio < 20.0F {
    fail("fixture channels differ in scale by only \{ratio}x -- d_gamma's inv_std factor would be invisible here")
  }
  let (_out, cache) = @snn_mbt.batch_norm2d_forward(input, n, c, h, w, bn0)
  let wt2 : Array[Float] = wt
  let (_di, d_gamma, _db) = @snn_mbt.batch_norm2d_backward(wt2, cache, bn0)
  let g0 = bn0.gamma.copy()
  let f0 = bn_obj(input, n, c, h, w, bn0, wt2)
  let eps = 0.002F
  for k in 0..<c {
    let up = g0.copy()
    let dn = g0.copy()
    up[k] = g0[k] + eps
    dn[k] = g0[k] - eps
    let fu = bn_obj(
      input, n, c, h, w,
      @snn_mbt.BatchNorm2d::with_gamma_beta(up, bn0.beta), wt2,
    )
    let fm = bn_obj(
      input, n, c, h, w,
      @snn_mbt.BatchNorm2d::with_gamma_beta(dn, bn0.beta), wt2,
    )
    let fd = (fu - fm) / (2.0F * eps)
    if (d_gamma[k] - fd).abs() > 0.01F {
      fail("d_gamma[\{k}] analytical \{d_gamma[k]} vs numerical \{fd}")
    }
  }
  if f0 == 0.0F {
    fail("objective is identically zero -- the gate measured nothing")
  }
}

///|
test "batch_norm2d_backward / d_beta matches numerical" {
  let (bn0, input, wt, n, c, h, w) = bn_scaled_fixture()
  let (_out, cache) = @snn_mbt.batch_norm2d_forward(input, n, c, h, w, bn0)
  let (_di, _dg, d_beta) = @snn_mbt.batch_norm2d_backward(wt, cache, bn0)
  let b0 = bn0.beta.copy()
  let eps = 0.002F
  for k in 0..<c {
    let up = b0.copy()
    let dn = b0.copy()
    up[k] = b0[k] + eps
    dn[k] = b0[k] - eps
    let fu = bn_obj(
      input, n, c, h, w,
      @snn_mbt.BatchNorm2d::with_gamma_beta(bn0.gamma, up), wt,
    )
    let fm = bn_obj(
      input, n, c, h, w,
      @snn_mbt.BatchNorm2d::with_gamma_beta(bn0.gamma, dn), wt,
    )
    let fd = (fu - fm) / (2.0F * eps)
    if (d_beta[k] - fd).abs() > 0.01F {
      fail("d_beta[\{k}] analytical \{d_beta[k]} vs numerical \{fd}")
    }
  }
}