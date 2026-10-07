// tarnet_se.mbt -- the two squeeze-and-excitation variants in TARNet.
//
// Reference: arXiv:2605.07735 (ICME 2026); authors' implementation
// `tarnet/models/tarnet.py`, classes `SEModule` and
// `ChannelTimeSenseSELayer`.
//
// Both compute ONE gate per channel and broadcast it over time
// (`x * w.view(B, C, 1)`), which is what makes them channel attention rather
// than temporal attention. The difference is where the temporal context
// enters: `SeModule` pools first and never convolves in time at all, while
// `ChannelTimeSenseSELayer` runs three depthwise convolutions of kernel 3, 5
// and 9 and pools each, so its gate depends on short-, mid- and long-context
// statistics. That three-scale gate is the paper's actual novelty; the plain
// SE is the post-MFA recalibration.

///|
/// Parameters of the post-MFA squeeze-and-excitation module.
pub(all) struct SeParam {
  w1 : Array[Float]
  b1 : Array[Float]
  w2 : Array[Float]
  b2 : Array[Float]
  channels : Int
  bottleneck : Int
}

///|
pub fn SeParam::new(channels : Int, bottleneck : Int, seed : UInt64) -> SeParam {
  let rng = Xoshiro::new(seed)
  {
    w1: tn_randn(bottleneck * channels, 0.05F, rng),
    b1: Array::make(bottleneck, 0.0F),
    w2: tn_randn(channels * bottleneck, 0.05F, rng),
    b2: Array::make(channels, 0.0F),
    channels,
    bottleneck,
  }
}

///|
/// Squeeze-and-excitation forward. `x` is `[n, c, t]`.
pub fn se_forward(x : Array[Float], n : Int, p : SeParam) -> Array[Float] {
  let c = p.channels
  let t = x.length() / (n * c)
  // global average pool over time
  let u : Array[Float] = Array::make(n * c, 0.0F)
  let inv_t = 1.0F / Float::from_int(t)
  for b in 0..<n {
    for ch in 0..<c {
      let base = (b * c + ch) * t
      let mut s = 0.0F
      for ti in 0..<t {
        s = s + x[base + ti]
      }
      u[b * c + ch] = s * inv_t
    }
  }
  let gate = tn_se_gate(u, n, p)
  let out : Array[Float] = Array::make(n * c * t, 0.0F)
  for b in 0..<n {
    for ch in 0..<c {
      let a = gate[b * c + ch]
      let base = (b * c + ch) * t
      for ti in 0..<t {
        out[base + ti] = x[base + ti] * a
      }
    }
  }
  out
}

///|
/// The pooled vector -> gate part, shared by the forward and the backward so
/// the two cannot drift on where the sigmoid sits.
fn tn_se_gate(u : Array[Float], n : Int, p : SeParam) -> Array[Float] {
  let c = p.channels
  let bk = p.bottleneck
  let h : Array[Float] = Array::make(n * bk, 0.0F)
  for b in 0..<n {
    for j in 0..<bk {
      let mut acc = p.b1[j]
      for ch in 0..<c {
        acc = acc + p.w1[j * c + ch] * u[b * c + ch]
      }
      let z = acc
      h[b * bk + j] = if z > 0.0F { z } else { 0.0F }
    }
  }
  let gate : Array[Float] = Array::make(n * c, 0.0F)
  for b in 0..<n {
    for ch in 0..<c {
      let mut acc = p.b2[ch]
      for j in 0..<bk {
        acc = acc + p.w2[ch * bk + j] * h[b * bk + j]
      }
      gate[b * c + ch] = tn_sigmoid(acc)
    }
  }
  gate
}

///|
/// Element-wise logistic, local because three modules need it.
pub fn tn_sigmoid(z : Float) -> Float {
  1.0F / (1.0F + expf(-z))
}

///|
/// Parameters of the multi-scale channel-time SE gate.
pub(all) struct CtseParam {
  small : Conv1DDepthwise
  mid : Conv1DDepthwise
  large : Conv1DDepthwise
  fw : Array[Float]
  fb : Float
  w1 : Array[Float]
  b1 : Array[Float]
  w2 : Array[Float]
  b2 : Array[Float]
  channels : Int
  c_red : Int
}

///|
/// The paper's default: reduction ratio 2, kernel sizes (3, 5, 9), no
/// subband grouping.
pub fn CtseParam::new(channels : Int, seed : UInt64) -> CtseParam {
  let rng = Xoshiro::new(seed)
  let c_red = channels / 2
  let cr = if c_red < 1 { 1 } else { c_red }
  {
    small: tn_depthwise(channels, 3, 0.03F, rng),
    mid: tn_depthwise(channels, 5, 0.03F, rng),
    large: tn_depthwise(channels, 9, 0.03F, rng),
    fw: Array::make(3, 0.3F),
    fb: 0.0F,
    w1: tn_randn(cr * channels, 0.05F, rng),
    b1: Array::make(cr, 0.0F),
    w2: tn_randn(channels * cr, 0.05F, rng),
    b2: Array::make(channels, 0.0F),
    channels,
    c_red: cr,
  }
}

///|
/// Depthwise conv with the paper's weights scaled small and bias zero.
fn tn_depthwise(
  channels : Int,
  k : Int,
  std : Float,
  rng : Xoshiro,
) -> Conv1DDepthwise {
  let p = Conv1DDepthwise::new(channels, k, 1, k / 2)
  let w = tn_randn(channels * k, std, rng)
  let mut i = 0
  while i < channels * k {
    p.weight[i] = w[i]
    i = i + 1
  }
  p
}

///|
/// Caches for the three scale paths: each is pooled over time, so the pooled
/// values are needed to differentiate the ReLU and the depthwise conv.
pub(all) struct CtseCache {
  x : Array[Float]
  pooled : Array[Float]
  relu_mask : Array[Bool]
  squeezed : Array[Float]
  h : Array[Float]
  gate : Array[Float]
}

///|
/// Channel-time SE forward. `x` is `[n, c, t]`.
pub fn ctse_forward(
  x : Array[Float],
  n : Int,
  p : CtseParam,
) -> (Array[Float], CtseCache) {
  let c = p.channels
  let t = x.length() / (n * c)
  let inv_t = 1.0F / Float::from_int(t)
  // three depthwise paths, each pooled then ReLU'd
  let cnvs = [
    conv1d_depthwise_forward(x, n, p.small),
    conv1d_depthwise_forward(x, n, p.mid),
    conv1d_depthwise_forward(x, n, p.large),
  ]
  let pooled : Array[Float] = Array::make(3 * n * c, 0.0F)
  let mask : Array[Bool] = Array::make(3 * n * c, false)
  let mut k = 0
  while k < 3 {
    let src = cnvs[k]
    let dst = k * n * c
    for b in 0..<n {
      for ch in 0..<c {
        let base = (b * c + ch) * t
        let mut s = 0.0F
        for ti in 0..<t {
          s = s + src[base + ti]
        }
        s = s * inv_t
        pooled[dst + b * c + ch] = if s > 0.0F { s } else { 0.0F }
        mask[dst + b * c + ch] = s > 0.0F
      }
    }
    k = k + 1
  }
  // Linear(3,1) over the three scales, shared across channels
  let squeezed : Array[Float] = Array::make(n * c, 0.0F)
  for b in 0..<n {
    for ch in 0..<c {
      let a = pooled[0 * n * c + b * c + ch]
      let m = pooled[1 * n * c + b * c + ch]
      let l = pooled[2 * n * c + b * c + ch]
      squeezed[b * c + ch] = p.fw[0] * a + p.fw[1] * m + p.fw[2] * l + p.fb
    }
  }
  let cr = p.c_red
  let h : Array[Float] = Array::make(n * cr, 0.0F)
  for b in 0..<n {
    for j in 0..<cr {
      let mut acc = p.b1[j]
      for ch in 0..<c {
        acc = acc + p.w1[j * c + ch] * squeezed[b * c + ch]
      }
      h[b * cr + j] = if acc > 0.0F { acc } else { 0.0F }
    }
  }
  let gate : Array[Float] = Array::make(n * c, 0.0F)
  for b in 0..<n {
    for ch in 0..<c {
      let mut acc = p.b2[ch]
      for j in 0..<cr {
        acc = acc + p.w2[ch * cr + j] * h[b * cr + j]
      }
      gate[b * c + ch] = tn_sigmoid(acc)
    }
  }
  let out : Array[Float] = Array::make(n * c * t, 0.0F)
  for b in 0..<n {
    for ch in 0..<c {
      let a = gate[b * c + ch]
      let base = (b * c + ch) * t
      for ti in 0..<t {
        out[base + ti] = x[base + ti] * a
      }
    }
  }
  (out, { x, pooled, relu_mask: mask, squeezed, h, gate })
}

///|
/// Gradients of a channel-time SE gate: `(d_x, dw_small, db_small,
/// dw_mid, db_mid, dw_large, db_large, dfw, dfb, dw1, db1, dw2, db2)`.
/// Tuple-typed unwieldy on purpose would be worse; this is the full set.
pub(all) struct CtseGrad {
  d_x : Array[Float]
  d_small : Array[Float]
  db_small : Array[Float]
  d_mid : Array[Float]
  db_mid : Array[Float]
  d_large : Array[Float]
  db_large : Array[Float]
  d_fw : Array[Float]
  d_fb : Float
  d_w1 : Array[Float]
  d_b1 : Array[Float]
  d_w2 : Array[Float]
  d_b2 : Array[Float]
}

///|
/// Channel-time SE backward.
pub fn ctse_backward(
  d_out : Array[Float],
  n : Int,
  p : CtseParam,
  cache : CtseCache,
) -> CtseGrad {
  let c = p.channels
  let t = cache.x.length() / (n * c)
  let inv_t = 1.0F / Float::from_int(t)
  let cr = p.c_red
  let d_x : Array[Float] = Array::make(n * c * t, 0.0F)
  let d_gate : Array[Float] = Array::make(n * c, 0.0F)
  // d_x gets the direct path; d_gate is the gate's own upstream
  for b in 0..<n {
    for ch in 0..<c {
      let a = cache.gate[b * c + ch]
      let base = (b * c + ch) * t
      let mut g = 0.0F
      for ti in 0..<t {
        let d = d_out[base + ti]
        d_x[base + ti] = d * a
        g = g + d * cache.x[base + ti]
      }
      d_gate[b * c + ch] = g
    }
  }
  // sigmoid -> fc2 -> ReLU -> fc1 -> squeezed
  let d_squeezed : Array[Float] = Array::make(n * c, 0.0F)
  let d_w2 : Array[Float] = Array::make(c * cr, 0.0F)
  let d_b2 : Array[Float] = Array::make(c, 0.0F)
  let d_h : Array[Float] = Array::make(n * cr, 0.0F)
  for b in 0..<n {
    for ch in 0..<c {
      let a = cache.gate[b * c + ch]
      let dz = d_gate[b * c + ch] * a * (1.0F - a)
      d_b2[ch] = d_b2[ch] + dz
      for j in 0..<cr {
        d_w2[ch * cr + j] = d_w2[ch * cr + j] + dz * cache.h[b * cr + j]
        d_h[b * cr + j] = d_h[b * cr + j] + dz * p.w2[ch * cr + j]
      }
    }
  }
  let d_w1 : Array[Float] = Array::make(cr * c, 0.0F)
  let d_b1 : Array[Float] = Array::make(cr, 0.0F)
  for b in 0..<n {
    for j in 0..<cr {
      let hv = cache.h[b * cr + j]
      let dz = if hv > 0.0F { d_h[b * cr + j] } else { 0.0F }
      d_b1[j] = d_b1[j] + dz
      for ch in 0..<c {
        d_w1[j * c + ch] = d_w1[j * c + ch] + dz * cache.squeezed[b * c + ch]
        d_squeezed[b * c + ch] = d_squeezed[b * c + ch] + dz * p.w1[j * c + ch]
      }
    }
  }
  // Linear(3,1) back into the three pooled paths
  let d_fw : Array[Float] = Array::make(3, 0.0F)
  let mut d_fb = 0.0F
  let d_pooled : Array[Float] = Array::make(3 * n * c, 0.0F)
  for b in 0..<n {
    for ch in 0..<c {
      let d = d_squeezed[b * c + ch]
      d_fb = d_fb + d
      let mut k = 0
      while k < 3 {
        d_pooled[k * n * c + b * c + ch] = d * p.fw[k]
        d_fw[k] = d_fw[k] + d * cache.pooled[k * n * c + b * c + ch]
        k = k + 1
      }
    }
  }
  // ReLU mask, then mean-over-time, then the three depthwise backwards
  let cnvs = [
    conv1d_depthwise_forward(cache.x, n, p.small),
    conv1d_depthwise_forward(cache.x, n, p.mid),
    conv1d_depthwise_forward(cache.x, n, p.large),
  ]
  let params = [p.small, p.mid, p.large]
  let d_w = [Array::make(c * 3, 0.0F), Array::make(c * 5, 0.0F), Array::make(c * 9, 0.0F)]
  let d_b = [Array::make(c, 0.0F), Array::make(c, 0.0F), Array::make(c, 0.0F)]
  let mut k = 0
  while k < 3 {
    let dp = Array::make(n * c * t, 0.0F)
    for b in 0..<n {
      for ch in 0..<c {
        let base = (b * c + ch) * t
        let g = d_pooled[k * n * c + b * c + ch]
        let active = if cache.relu_mask[k * n * c + b * c + ch] {
          g
        } else {
          0.0F
        }
        let per = active * inv_t
        for ti in 0..<t {
          dp[base + ti] = per
        }
      }
    }
    let (dx, dw, db) = conv1d_depthwise_backward(cache.x, dp, n, params[k])
    let mut i = 0
    while i < (n * c * t) {
      d_x[i] = d_x[i] + dx[i]
      i = i + 1
    }
    d_w[k] = dw
    d_b[k] = db
    k = k + 1
  }
  let _ = cnvs
  {
    d_x,
    d_small: d_w[0],
    db_small: d_b[0],
    d_mid: d_w[1],
    db_mid: d_b[1],
    d_large: d_w[2],
    db_large: d_b[2],
    d_fw,
    d_fb,
    d_w1,
    d_b1,
    d_w2,
    d_b2,
  }
}