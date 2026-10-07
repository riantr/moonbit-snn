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
/// `pub(all)` rather than `pub` so a caller OUTSIDE this package can build an
/// instance with chosen gamma/beta. With plain `pub` the fields are private
/// across packages, which made it impossible to write a gradient gate for
/// `layer_norm_backward`'s d_gamma / d_beta -- the gate could only exercise
/// d_input, and the two affine slots went unverified until the block's
/// per-parameter sweep hit one of them (26 of 85 block parameters wrong, worst
/// in ln1.gamma, fd = -0.369 against analytic -0.0138).
pub(all) struct LayerNorm {
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
          // output = gamma * xc * inv + beta, so d(output)/d(gamma) is
          // xc * inv -- the inv_std factor is part of the derivative and was
          // missing here. A gate that only checked d_input could not see it:
          // d_input does not depend on this line.
          d_gamma[feat_off] = d_gamma[feat_off] +
            dy * cache.x_centered[idx] * inv
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

// ---------------------------------------------------------------------------
// Global layer norm
// ---------------------------------------------------------------------------
//
// WHY THIS EXISTS, and why it is not `LayerNorm` with different numbers:
// asteroid's `GlobLN` -- the `gLN` that the TARNet reference selects via
// `norms.get("gLN")` -- has PER-CHANNEL affine parameters of shape (C,),
// broadcast over time, while this package's `LayerNorm` allocates one
// gamma/beta per ELEMENT of the (c, h, w) group. Both normalise over the same
// axis, so the two agree numerically at gamma = 1, beta = 0; they differ in
// capacity, and the difference is not cosmetic:
//
//   - parameter count: GlobLN has C, LayerNorm has C * h * w. At the default
//     t_frames = 301 that is 301x more parameters in every one of the block's
//     four norms, none of them in the reference.
//   - LENGTH: GlobLN has no length-dependent parameter, so the reference runs
//     utterances of any frame count through one model. LayerNorm(c, 1, t)
//     cannot -- the model becomes hard-wired to the t_frames it was built with,
//     which defeats the point of a model whose input is variable-length
//     speech.
//
// Reference (asteroid/masknn/norms.py, verified against the library's own
// source, not from memory):
//
//     class _LayerNorm(nn.Module):
//         def __init__(self, channel_size):
//             self.gamma = nn.Parameter(torch.ones(channel_size))
//             self.beta = nn.Parameter(torch.zeros(channel_size))
//         def apply_gain_and_bias(self, normed_x):   # [batch, channel, *]
//             return (self.gamma * normed_x.transpose(1, -1) + self.beta).transpose(1, -1)
//
//     class GlobLN(_LayerNorm):
//         def forward(self, x, EPS=1e-8):
//             dims = list(range(1, len(x.shape)))      # every dim except batch
//             mean = x.mean(dim=dims, keepdim=True)
//             var = torch.pow(x - mean, 2).mean(dim=dims, keepdim=True)
//             return self.apply_gain_and_bias((x - mean) / (var + EPS).sqrt())
//
// Note EPS = 1e-8 here, against 1e-5 in this package's `LayerNorm`; that is the
// reference's value for the gLN path and is not arbitrary.

///|
/// Global layer norm with a PER-CHANNEL affine of length `c`, applied to a
/// `[n, c, t]` tensor. Deliberately not `LayerNorm`: see the section comment.
pub(all) struct GlobalLayerNorm {
  gamma : Array[Float]
  beta : Array[Float]
  eps : Float
  c : Int
}

///|
pub fn GlobalLayerNorm::new(c : Int, eps? : Float = 0.00000001) -> GlobalLayerNorm {
  { gamma: Array::make(c, 1.0F), beta: Array::make(c, 0.0F), eps, c }
}

///|
/// Forward cache. `x_centered` is (x - mean) and `inv_std` is
/// 1 / sqrt(var + eps) with ONE entry per sample, because the group is the
/// whole (c, t) plane of that sample.
pub(all) struct GlnCache {
  x_centered : Array[Float]
  inv_std : Array[Float]
  n : Int
  c : Int
  t : Int
  ct : Int
}

///|
/// Forward pass. `x` is `[n, c, t]`, returns `[n, c, t]` and the cache.
pub fn gln_forward(
  x : Array[Float],
  n : Int,
  c : Int,
  t : Int,
  gln : GlobalLayerNorm,
) -> (Array[Float], GlnCache) {
  let ct = c * t
  let out : Array[Float] = Array::make(n * ct, 0.0F)
  let x_centered : Array[Float] = Array::make(n * ct, 0.0F)
  let inv_std : Array[Float] = Array::make(n, 0.0F)
  for b in 0..<n {
    let base = b * ct
    let mut s = 0.0F
    for i in 0..<ct {
      s = s + x[base + i]
    }
    let mean = s / Float::from_int(ct)
    let mut v = 0.0F
    for i in 0..<ct {
      let d = x[base + i] - mean
      x_centered[base + i] = d
      v = v + d * d
    }
    // biased variance, matching torch.pow(x - mean, 2).mean(...)
    let variance = v / Float::from_int(ct)
    let inv = 1.0F / sqrtf(variance + gln.eps)
    inv_std[b] = inv
    for ch in 0..<c {
      let g = gln.gamma[ch]
      let be = gln.beta[ch]
      let off = ch * t
      for ti in 0..<t {
        let i = base + off + ti
        out[i] = x_centered[i] * inv * g + be
      }
    }
  }
  (out, { x_centered, inv_std, n, c, t, ct })
}

///|
/// Backward pass: `(d_input, d_gamma, d_beta)`, both affine gradients being of
/// length `c` -- they sum over time, because gamma and beta are per channel.
///
/// The input gradient is the closed form for a group-normalised affine:
///
///     d_xhat = d_y * gamma
///     d_x    = inv * ( d_xhat - mean(d_xhat) - xhat * mean(d_xhat * xhat) )
///
/// with every mean taken over the (c, t) plane of one sample. Writing it this
/// way rather than by accumulating per-element contributions is what keeps the
/// two "mean" terms from being recomputed inside the inner loop.
pub fn gln_backward(
  d_output : Array[Float],
  cache : GlnCache,
  gln : GlobalLayerNorm,
) -> (Array[Float], Array[Float], Array[Float]) {
  let n = cache.n
  let c = cache.c
  let t = cache.t
  let ct = cache.ct
  let d_input : Array[Float] = Array::make(n * ct, 0.0F)
  let d_gamma : Array[Float] = Array::make(c, 0.0F)
  let d_beta : Array[Float] = Array::make(c, 0.0F)
  let inv_ct = 1.0F / Float::from_int(ct)
  for b in 0..<n {
    let base = b * ct
    let inv = cache.inv_std[b]
    // first pass: d_xhat and its two moments
    let mut sum_d = 0.0F
    let mut sum_dxc = 0.0F
    for ch in 0..<c {
      let g = gln.gamma[ch]
      let off = ch * t
      for ti in 0..<t {
        let i = base + off + ti
        let dy = d_output[i]
        let xhat = cache.x_centered[i] * inv
        d_beta[ch] = d_beta[ch] + dy
        d_gamma[ch] = d_gamma[ch] + dy * xhat
        let dxhat = dy * g
        sum_d = sum_d + dxhat
        sum_dxc = sum_dxc + dxhat * xhat
      }
    }
    let mean_d = sum_d * inv_ct
    let mean_dxc = sum_dxc * inv_ct
    // second pass: assemble d_x
    for i in 0..<ct {
      let idx = base + i
      let xhat = cache.x_centered[idx] * inv
      d_input[idx] = inv * (d_output[idx] * gln.gamma[i / t] - mean_d - xhat * mean_dxc)
    }
  }
  (d_input, d_gamma, d_beta)
}
