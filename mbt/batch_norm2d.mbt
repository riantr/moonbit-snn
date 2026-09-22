// batch_norm2d.mbt — 2D Batch Normalisation for NCHW tensors (v0.17.0).
//
// Reference: Ioffe & Szegedy, "Batch Normalization: Accelerating Deep
// Network Training by Reducing Internal Covariate Shift", ICML 2015.
//
// Forward (training mode, per channel c):
//   mean_c     = sum over (n,h,w) of x[n,c,h,w] / (n*h*w)
//   var_c      = sum over (n,h,w) of (x[n,c,h,w] - mean_c)^2 / (n*h*w)
//   inv_std_c  = 1 / sqrt(var_c + eps)
//   y[n,c,h,w] = gamma[c] * (x[n,c,h,w] - mean_c) * inv_std_c + beta[c]
//
// Backward: returns d_input / d_gamma / d_beta given d_output. Uses the
// standard "save mean + inv_std + x_centered" cache to avoid recomputation.
//
// Layout: NCHW row-major flat `Array[Float]`.
//   Input:    [n, c, h, w]    length = n * c  * h  * w
//   Output:   [n, c, h, w]    length = n * c  * h  * w
//   Gamma:    [c]             length = c
//   Beta:     [c]             length = c
//   Running mean / var: [c]
//
// Conventions match the project's bit-exact Float32 contract. We do not
// use Bessel's correction (population variance, divisor = N*H*W) since
// that matches PyTorch's `track_running_stats=False` training behaviour.

///|
/// BatchNorm2d parameter container.
pub struct BatchNorm2d {
  // Learnable affine parameters.
  gamma : Array[Float]
  beta : Array[Float]
  // Running statistics (for inference); updated with exponential moving
  // average during training. Empty if running stats are not tracked.
  mut running_mean : Array[Float]
  mut running_var : Array[Float]
  // BN hyper-parameters.
  momentum : Float  // EMA coefficient for running stats; 0 disables updates.
  eps : Float
  // Training mode flag — when false, forward uses running_mean / running_var
  // instead of batch statistics.
  mut training : Bool
}

///|
/// Build a BatchNorm2d for `c` channels. `gamma` is initialised to all-1,
/// `beta` to all-0; running stats to all-0. `momentum` defaults to 0.1,
/// `eps` to 1e-5.
pub fn BatchNorm2d::new(c : Int, momentum? : Float = 0.1, eps? : Float = 0.00001) -> BatchNorm2d {
  {
    gamma: Array::make(c, 1.0F),
    beta: Array::make(c, 0.0F),
    running_mean: Array::make(c, 0.0F),
    running_var: Array::make(c, 0.0F),
    momentum,
    eps,
    training: true,
  }
}

///|
/// Convenience constructor taking pre-built gamma / beta arrays (does NOT
/// copy them).
pub fn BatchNorm2d::with_gamma_beta(
  gamma : Array[Float],
  beta : Array[Float],
  momentum? : Float = 0.1,
  eps? : Float = 0.00001,
) -> BatchNorm2d {
  let c = gamma.length()
  {
    gamma,
    beta,
    running_mean: Array::make(c, 0.0F),
    running_var: Array::make(c, 0.0F),
    momentum,
    eps,
    training: true,
  }
}

///|
/// Set training / inference mode.
pub fn BatchNorm2d::set_training(self : BatchNorm2d, training : Bool) -> Unit {
  self.training = training
}

///|
/// Cache returned by `batch_norm2d_forward` and consumed by
/// `batch_norm2d_backward`. Holds everything the backward pass needs
/// without recomputing the forward.
pub struct BatchNormCache {
  // Per-channel statistics computed from the current batch.
  mean : Array[Float]
  inv_std : Array[Float]
  // (n, c, h, w) — input shifted by the per-channel mean.
  x_centered : Array[Float]
  // n * h * w per channel; cached to avoid re-multiplying in backward.
  nhw : Int
  // Shape, for backward.
  n : Int
  c : Int
  h : Int
  w : Int
}

///|
/// Forward pass. `bn` may be in training mode (uses batch stats) or
/// inference mode (uses running_mean / running_var). When `training=true`
/// and `bn.momentum > 0`, the running stats are updated in-place.
pub fn batch_norm2d_forward(
  input : Array[Float],
  n : Int,
  c : Int,
  h : Int,
  w : Int,
  bn : BatchNorm2d,
) -> (Array[Float], BatchNormCache) {
  let nhw = n * h * w
  let chw = c * h * w
  let total = n * chw
  // Allocate output and cache buffers up front.
  let output : Array[Float] = Array::make(total, 0.0F)
  let x_centered : Array[Float] = Array::make(total, 0.0F)
  let mean : Array[Float] = Array::make(c, 0.0F)
  let inv_std : Array[Float] = Array::make(c, 0.0F)
  // Pick which mean / var to use based on training mode.
  let use_running = not(bn.training)
  let use_mean : Array[Float] = if use_running { bn.running_mean } else { mean }
  let use_var : Array[Float] = if use_running {
    bn.running_var
  } else {
    // We'll fill `mean` and compute `inv_std` from `var` below.
    mean // placeholder; inv_std computed separately
  }
  // ---- Per-channel batch statistics (always computed for the cache) ----
  // We always compute batch mean / var so the cache is complete (in
  // inference mode we still want the cache to record what we used).
  for ci in 0..<c {
    // Compute mean over (n, h, w) for this channel.
    let mut sum = 0.0F
    let mut sum_sq = 0.0F
    for ni in 0..<n {
      for hi in 0..<h {
        for wi in 0..<w {
          let idx = ni * chw + ci * h * w + hi * w + wi
          let v = input[idx]
          sum = sum + v
          sum_sq = sum_sq + v * v
        }
      }
    }
    let m = sum / Float::from_int(nhw)
    // Population variance: E[x^2] - (E[x])^2.
    let v = sum_sq / Float::from_int(nhw) - m * m
    mean[ci] = m
    inv_std[ci] = 1.0F / sqrtf(v + bn.eps)
    // Update running stats in-place (if training).
    if bn.training {
      // running_mean <- (1 - momentum) * running_mean + momentum * mean
      bn.running_mean[ci] = (1.0F - bn.momentum) * bn.running_mean[ci] +
        bn.momentum * m
      // running_var <- (1 - momentum) * running_var + momentum * var
      // (PyTorch tracks biased variance in running_var.)
      bn.running_var[ci] = (1.0F - bn.momentum) * bn.running_var[ci] +
        bn.momentum * v
    }
    // Write per-element output + x_centered.
    let ms = use_mean[ci]
    let inv = if use_running {
      1.0F / sqrtf(use_var[ci] + bn.eps)
    } else {
      inv_std[ci]
    }
    for ni in 0..<n {
      for hi in 0..<h {
        for wi in 0..<w {
          let idx = ni * chw + ci * h * w + hi * w + wi
          let xc = input[idx] - ms
          x_centered[idx] = xc
          output[idx] = bn.gamma[ci] * xc * inv + bn.beta[ci]
        }
      }
    }
  }
  // Silence the unused `use_var` warning by referencing it.
  let _ = use_var
  (output, { mean: use_mean, inv_std, x_centered, nhw, n, c, h, w })
}

///|
/// Backward pass. Given `d_output` (shape `[n, c, h, w]`) and the cache
/// from forward, returns `(d_input, d_gamma, d_beta)`. All shapes match
/// the forward.
pub fn batch_norm2d_backward(
  d_output : Array[Float],
  cache : BatchNormCache,
  bn : BatchNorm2d,
) -> (Array[Float], Array[Float], Array[Float]) {
  let n = cache.n
  let c = cache.c
  let h = cache.h
  let w = cache.w
  let nhw = cache.nhw
  let chw = c * h * w
  let total = n * chw
  let d_input : Array[Float] = Array::make(total, 0.0F)
  let d_gamma : Array[Float] = Array::make(c, 0.0F)
  let d_beta : Array[Float] = Array::make(c, 0.0F)
  let nhw_f = Float::from_int(nhw)
  for ci in 0..<c {
    let inv = cache.inv_std[ci]
    // d_beta and d_gamma accumulate over (n, h, w).
    let mut d_b = 0.0F
    let mut d_g = 0.0F
    // Pre-accumulate the two scalar quantities needed for d_input:
    //   sum_dxhat  = sum_{n,h,w} d_y * gamma
    //   sum_dxhat_xc = sum_{n,h,w} d_y * gamma * x_centered
    // (For d_var and d_mean in the closed-form BN backward.)
    let mut sum_dxhat = 0.0F
    let mut sum_dxhat_xc = 0.0F
    for ni in 0..<n {
      for hi in 0..<h {
        for wi in 0..<w {
          let idx = ni * chw + ci * h * w + hi * w + wi
          let dy = d_output[idx]
          let dxhat = dy * bn.gamma[ci]
          d_b = d_b + dy
          d_g = d_g + dy * cache.x_centered[idx]
          sum_dxhat = sum_dxhat + dxhat
          sum_dxhat_xc = sum_dxhat_xc + dxhat * cache.x_centered[idx]
        }
      }
    }
    d_beta[ci] = d_b
    d_gamma[ci] = d_g
    // Closed-form backward (PyTorch / original paper formulation):
    //   d_var = -(1/2) * sum(dxhat * xc) * inv_std^3
    //   d_mean = -sum(dxhat) * inv_std + d_var * -2 * sum(xc) / N
    //   d_input = dxhat * inv_std + d_var * 2 * xc / N + d_mean / N
    let d_var = -0.5F * sum_dxhat_xc * inv * inv * inv
    let d_mean = -sum_dxhat * inv
    let inv_nhw = 1.0F / nhw_f
    for ni in 0..<n {
      for hi in 0..<h {
        for wi in 0..<w {
          let idx = ni * chw + ci * h * w + hi * w + wi
          let dxhat = d_output[idx] * bn.gamma[ci]
          let xc = cache.x_centered[idx]
          d_input[idx] = dxhat * inv + d_var * 2.0F * xc * inv_nhw +
            d_mean * inv_nhw
        }
      }
    }
  }
  (d_input, d_gamma, d_beta)
}