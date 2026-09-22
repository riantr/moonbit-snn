// layer_norm.mbt — Layer Normalisation for NCHW tensors (v0.17.1).
//
// Reference: Ba, Kiros, Hinton, "Layer Normalization", arXiv:1607.06450
// (2016). Normalises per-sample across the feature axes (C, H, W).
//
// Forward (training, per sample n):
//   mean_n     = sum over (c, h, w) of x[n,c,h,w] / (c*h*w)
//   var_n      = sum over (c, h, w) of (x[n,c,h,w] - mean_n)^2 / (c*h*w)
//   inv_std_n  = 1 / sqrt(var_n + eps)
//   y[n,c,h,w] = gamma[c,h,w] * (x[n,c,h,w] - mean_n) * inv_std_n
//                + beta[c,h,w]
//
// Backward: closed-form per-sample using saved mean + inv_std + x_centered.
//
// Layout: NCHW row-major flat `Array[Float]`.
//   Input:  [n, c, h, w]    length = n * c * h * w
//   Output: [n, c, h, w]    length = n * c * h * w
//   Gamma:  [c * h * w]     length = c * h * w (per-feature scale)
//   Beta:   [c * h * w]     length = c * h * w (per-feature shift)
//
// No running stats — LayerNorm is deterministic (unlike BN which tracks
// running_mean / running_var for inference).

///|
/// LayerNorm parameter container. Affine params have one element per
/// feature position (length = c * h * w).
pub struct LayerNorm {
  gamma : Array[Float]
  beta : Array[Float]
  eps : Float
  c : Int
  h : Int
  w : Int
}

///|
/// Build a LayerNorm for the given feature shape (c, h, w). `gamma` is
/// initialised to all-1, `beta` to all-0.
pub fn LayerNorm::new(c : Int, h : Int, w : Int, eps? : Float = 0.00001) -> LayerNorm {
  let chw = c * h * w
  {
    gamma: Array::make(chw, 1.0F),
    beta: Array::make(chw, 0.0F),
    eps,
    c,
    h,
    w,
  }
}

///|
/// Convenience constructor with explicit gamma / beta arrays (no copy).
pub fn LayerNorm::with_gamma_beta(
  gamma : Array[Float],
  beta : Array[Float],
  c : Int,
  h : Int,
  w : Int,
  eps? : Float = 0.00001,
) -> LayerNorm {
  { gamma, beta, eps, c, h, w }
}

///|
/// Cache returned by `layer_norm_forward` and consumed by
/// `layer_norm_backward`.
pub struct LayerNormCache {
  // Per-sample statistics.
  mean : Array[Float]
  inv_std : Array[Float]
  // (n, c, h, w) — input shifted by per-sample mean.
  x_centered : Array[Float]
  // Shape for backward.
  n : Int
  c : Int
  h : Int
  w : Int
  // chw per sample (denominator for backward).
  chw : Int
}

///|
/// Forward pass.
pub fn layer_norm_forward(
  input : Array[Float],
  n : Int,
  c : Int,
  h : Int,
  w : Int,
  ln : LayerNorm,
) -> (Array[Float], LayerNormCache) {
  let chw = c * h * w
  let total = n * chw
  let output : Array[Float] = Array::make(total, 0.0F)
  let x_centered : Array[Float] = Array::make(total, 0.0F)
  let mean : Array[Float] = Array::make(n, 0.0F)
  let inv_std : Array[Float] = Array::make(n, 0.0F)
  let chw_f = Float::from_int(chw)
  for ni in 0..<n {
    // Per-sample statistics.
    let mut sum = 0.0F
    let mut sum_sq = 0.0F
    for ci in 0..<c {
      for hi in 0..<h {
        for wi in 0..<w {
          let idx = ni * chw + ci * h * w + hi * w + wi
          let v = input[idx]
          sum = sum + v
          sum_sq = sum_sq + v * v
        }
      }
    }
    let m = sum / chw_f
    let v = sum_sq / chw_f - m * m
    let inv = 1.0F / sqrtf(v + ln.eps)
    mean[ni] = m
    inv_std[ni] = inv
    // Per-element output.
    for ci in 0..<c {
      for hi in 0..<h {
        for wi in 0..<w {
          let idx = ni * chw + ci * h * w + hi * w + wi
          let xc = input[idx] - m
          x_centered[idx] = xc
          // gamma/beta are indexed by feature position only (ci*h*w + hi*w + wi).
          output[idx] = ln.gamma[ci * h * w + hi * w + wi] * xc * inv +
            ln.beta[ci * h * w + hi * w + wi]
        }
      }
    }
  }
  (output, { mean, inv_std, x_centered, n, c, h, w, chw })
}

///|
/// Backward pass. Returns (d_input, d_gamma, d_beta). d_gamma and d_beta
/// are accumulated across the batch dim (length = c * h * w each).
pub fn layer_norm_backward(
  d_output : Array[Float],
  cache : LayerNormCache,
  ln : LayerNorm,
) -> (Array[Float], Array[Float], Array[Float]) {
  let n = cache.n
  let c = cache.c
  let h = cache.h
  let w = cache.w
  let chw = cache.chw
  let total = n * chw
  let d_input : Array[Float] = Array::make(total, 0.0F)
  let d_gamma : Array[Float] = Array::make(chw, 0.0F)
  let d_beta : Array[Float] = Array::make(chw, 0.0F)
  let chw_f = Float::from_int(chw)
  let inv_chw = 1.0F / chw_f
  for ni in 0..<n {
    let inv = cache.inv_std[ni]
    // Per-sample accumulators.
    let mut sum_dxhat = 0.0F
    let mut sum_dxhat_xc = 0.0F
    // Pre-accumulate d_gamma / d_beta / scalar sums in one pass.
    for ci in 0..<c {
      for hi in 0..<h {
        for wi in 0..<w {
          let feat_off = ci * h * w + hi * w + wi
          let idx = ni * chw + feat_off
          let dy = d_output[idx]
          let dxhat = dy * ln.gamma[feat_off]
          d_gamma[feat_off] = d_gamma[feat_off] + dy * cache.x_centered[idx]
          d_beta[feat_off] = d_beta[feat_off] + dy
          sum_dxhat = sum_dxhat + dxhat
          sum_dxhat_xc = sum_dxhat_xc + dxhat * cache.x_centered[idx]
        }
      }
    }
    let d_var = -0.5F * sum_dxhat_xc * inv * inv * inv
    let d_mean = -sum_dxhat * inv
    // Per-element d_input.
    for ci in 0..<c {
      for hi in 0..<h {
        for wi in 0..<w {
          let feat_off = ci * h * w + hi * w + wi
          let idx = ni * chw + feat_off
          let dxhat = d_output[idx] * ln.gamma[feat_off]
          let xc = cache.x_centered[idx]
          d_input[idx] = dxhat * inv + d_var * 2.0F * xc * inv_chw +
            d_mean * inv_chw
        }
      }
    }
  }
  (d_input, d_gamma, d_beta)
}