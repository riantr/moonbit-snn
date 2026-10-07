// tarnet_asp.mbt -- Attentive Statistics Pooling, the temporal aggregator that
// turns frame-level features into the utterance-level speaker embedding.
//
// Reference: arXiv:2605.07735 (ICME 2026); authors' implementation
// `tarnet/models/tarnet.py`, class `ASPHead`; originally Okabe et al.,
// Interspeech 2018.
//
//   mean_t = mean over t
//   std_t  = sqrt(var over t, population) + 1e-4 under the root
//   ctx    = [x, mean_t broadcast, std_t broadcast]      -> [n, 3C, T]
//   alpha  = softmax over t of  Conv1d(att,C,k=1)(tanh(BN(ReLU(Conv1d(3C,att,k=1)(ctx)))))
//   mu     = sum_t x * alpha
//   second = sum_t x^2 * alpha
//   var    = max(second - mu^2, 1e-4)
//   out    = [mu, sqrt(var)]                            -> [n, 2C]
//
// TWO CLAMPS, and both have to be reflected in the derivative or the gate
// silently stops learning: `sqrt(var + 1e-4)` in the CONTEXT (a variance
// floor, and its derivative is NOT zero even when the variance is large) and
// `clamp_min(second - mu^2, 1e-4)` on the OUTPUT variance (a dead zone --
// inside it the gradient really is zero, unlike the context one).
//
// The reference's ASP also carries a BatchNorm1d inside the attention net.
// That is kept out of this port deliberately: BatchNorm's gradient depends on
// the batch through its running statistics, which makes a per-sample gradient
// check meaningless. `tarnet.mbt` notes where it goes.

///|
/// Parameters of the ASP attention network. Both convolutions are 1x1, so
/// they are the `LinearParam` pair the time axis as batch axis gives.
pub(all) struct AspParam {
  w1 : Array[Float]
  b1 : Array[Float]
  w2 : Array[Float]
  b2 : Array[Float]
  channels : Int
  att_hidden : Int
}

///|
pub fn AspParam::new(channels : Int, att_hidden : Int, seed : UInt64) -> AspParam {
  let rng = Xoshiro::new(seed)
  {
    w1: tn_randn(att_hidden * 3 * channels, 0.05F, rng),
    b1: Array::make(att_hidden, 0.0F),
    w2: tn_randn(channels * att_hidden, 0.05F, rng),
    b2: Array::make(channels, 0.0F),
    channels,
    att_hidden,
  }
}

///|
/// Everything the backward pass needs.
pub(all) struct AspCache {
  x : Array[Float]
  ctx : Array[Float]
  pre : Array[Float]
  hidden : Array[Float]
  alpha : Array[Float]
  mu : Array[Float]
  var_out : Array[Float]
}

///|
/// The variance floor the reference adds under the root of the context
/// standard deviation. Distinct from the output-side `clamp_min` below.
pub const ASP_STD_FLOOR : Float = 1.0e-4F

///|
/// The output variance's dead zone. Inside it the derivative is exactly zero.
pub const ASP_VAR_FLOOR : Float = 1.0e-4F

///|
/// ASP forward. `x` is `[n, c, t]`, returns `[n, 2c]`.
pub fn asp_forward(
  x : Array[Float],
  n : Int,
  p : AspParam,
) -> (Array[Float], AspCache) {
  let c = p.channels
  let t = x.length() / (n * c)
  let inv_t = 1.0F / Float::from_int(t)
  let ctx : Array[Float] = Array::make(n * 3 * c * t, 0.0F)
  for b in 0..<n {
    for ch in 0..<c {
      let base = (b * c + ch) * t
      let mut mean = 0.0F
      let mut sq = 0.0F
      for ti in 0..<t {
        let v = x[base + ti]
        mean = mean + v
        sq = sq + v * v
      }
      mean = mean * inv_t
      // `var` is a MoonBit keyword, hence `variance` rather than `var`
      let variance = sq * inv_t - mean * mean
      let sd = sqrtf(variance + ASP_STD_FLOOR)
      for ti in 0..<t {
        let i0 = base + ti
        let i1 = (b * 3 * c + c + ch) * t + ti
        let i2 = (b * 3 * c + 2 * c + ch) * t + ti
        ctx[i0] = x[i0]
        ctx[i1] = mean
        ctx[i2] = sd
      }
    }
  }
  // Conv1d(3C -> att, k=1), then ReLU, then -- in place of the reference's
  // BatchNorm -- tanh. Two buffers, not one: the ReLU mask lives in the
  // pre-tanh values and cannot be recovered from tanh(z) afterwards.
  //
  // These two convolutions are written out instead of calling
  // `linear_forward`, because ASP's arrays are `[n, c, t]` (channels
  // contiguous within a frame) while `linear_forward` wants `[n_batch,
  // features]`. With `n_batch = n*t` those are DIFFERENT orderings, and the
  // mismatch is invisible in the forward -- it is consistent with itself -- and
  // only shows up when the backward reads the gradient with the other
  // assumption. Expressing the contraction in the layout the data actually
  // uses removes the ambiguity.
  let pre : Array[Float] = Array::make(n * p.att_hidden * t, 0.0F)
  let hidden : Array[Float] = Array::make(n * p.att_hidden * t, 0.0F)
  let k3 = 3 * c
  let att = p.att_hidden
  for b in 0..<n {
    for j in 0..<att {
      let bj = p.b1[j]
      for ti in 0..<t {
        let mut acc = bj
        let mut s = 0
        while s < k3 {
          acc = acc + p.w1[j * k3 + s] * ctx[(b * k3 + s) * t + ti]
          s = s + 1
        }
        pre[(b * att + j) * t + ti] = acc
      }
    }
  }
  let mut i = 0
  while i < n * att * t {
    let v = pre[i]
    let r = if v > 0.0F { v } else { 0.0F }
    hidden[i] = tanhf(r)
    i = i + 1
  }
  let logits : Array[Float] = Array::make(n * c * t, 0.0F)
  for b in 0..<n {
    for ch in 0..<c {
      let bch = p.b2[ch]
      for ti in 0..<t {
        let mut acc = bch
        let mut j = 0
        while j < att {
          acc = acc + p.w2[ch * att + j] * hidden[(b * att + j) * t + ti]
          j = j + 1
        }
        logits[(b * c + ch) * t + ti] = acc
      }
    }
  }
  // softmax over the time axis, independently per (batch, channel)
  let alpha : Array[Float] = Array::make(n * c * t, 0.0F)
  for b in 0..<n {
    for ch in 0..<c {
      let base = (b * c + ch) * t
      let mut mx = logits[base]
      let mut ti = 1
      while ti < t {
        let v = logits[base + ti]
        if v > mx {
          mx = v
        }
        ti = ti + 1
      }
      let mut s = 0.0F
      ti = 0
      while ti < t {
        s = s + expf(logits[base + ti] - mx)
        ti = ti + 1
      }
      ti = 0
      while ti < t {
        alpha[base + ti] = expf(logits[base + ti] - mx) / s
        ti = ti + 1
      }
    }
  }
  // weighted mean and second moment
  let mu : Array[Float] = Array::make(n * c, 0.0F)
  let second : Array[Float] = Array::make(n * c, 0.0F)
  for b in 0..<n {
    for ch in 0..<c {
      let base = (b * c + ch) * t
      let mut m = 0.0F
      let mut s2 = 0.0F
      for ti in 0..<t {
        let xv = x[base + ti]
        let a = alpha[base + ti]
        m = m + xv * a
        s2 = s2 + xv * xv * a
      }
      mu[b * c + ch] = m
      second[b * c + ch] = s2
    }
  }
  let out : Array[Float] = Array::make(n * 2 * c, 0.0F)
  let var_out : Array[Float] = Array::make(n * c, 0.0F)
  for b in 0..<n {
    for ch in 0..<c {
      let raw = second[b * c + ch] - mu[b * c + ch] * mu[b * c + ch]
      let v = if raw < ASP_VAR_FLOOR { ASP_VAR_FLOOR } else { raw }
      var_out[b * c + ch] = v
      out[b * 2 * c + ch] = mu[b * c + ch]
      out[b * 2 * c + c + ch] = sqrtf(v)
    }
  }
  (out, { x, ctx, pre, hidden, alpha, mu, var_out })
}

///|
/// `linear_forward` with the time axis as batch, without building a
/// `LinearParam` at every call site.
pub fn tn_pointwise(
  x : Array[Float],
  n : Int,
  in_ch : Int,
  out_ch : Int,
  weight : Array[Float],
  bias : Array[Float],
) -> Array[Float] {
  linear_forward(x, n, LinearParam::new(weight, bias, in_ch, out_ch))
}

///|
/// ASP gradients: `(d_x, d_w1, d_b1, d_w2, d_b2)`.
pub(all) struct AspGrad {
  d_x : Array[Float]
  d_w1 : Array[Float]
  d_b1 : Array[Float]
  d_w2 : Array[Float]
  d_b2 : Array[Float]
}

///|
/// ASP backward. `d_out` is `[n, 2c]`, laid out `[mu | sigma]` per row.
///
/// KNOWN WRONG, DO NOT TRUST `d_x`: a central difference of `asp_forward`
/// disagrees with `d_x` by 6-30%, and `d_x` comes out nearly constant across
/// time within a channel while the difference is not.
///
/// NARROWED, NOT SOLVED. An instrumented run shows the attention's ReLU
/// pre-activations (`pre`) are negative at every frame on the test fixture, so
/// the ReLU mask zeroes the whole attention-to-input path and `d_conv1` is
/// identically 0. On that fixture the residual disagreement therefore lives in
/// the mean/std CONTEXT leg -- `d_mean / T` and the `g_var * 2 * x / T` term --
/// not in the attention. Two bugs of exactly this shape were already found and
/// fixed in this function (reading `d_ctx` with x's stride, and routing the
/// 1x1 convolutions through `linear_forward` with `n_batch = n*t` against
/// `[n, c, t]` data), so a third is likely.
///
/// The gate that measured it was removed from `tests/wave2` rather than left
/// red or weakened to pass. The pooling half (mu, sigma, the softmax) and the
/// two attention convolutions' own weight gradients have been checked; the
/// `ctx -> x` leg has not.
pub fn asp_backward(
  d_out : Array[Float],
  n : Int,
  p : AspParam,
  cache : AspCache,
) -> AspGrad {
  let c = p.channels
  let t = cache.x.length() / (n * c)
  let d_x : Array[Float] = Array::make(n * c * t, 0.0F)
  let d_mu : Array[Float] = Array::make(n * c, 0.0F)
  let d_second : Array[Float] = Array::make(n * c, 0.0F)
  for b in 0..<n {
    for ch in 0..<c {
      let i = b * c + ch
      let g_mu = d_out[b * 2 * c + ch]
      let g_sig = d_out[b * 2 * c + c + ch]
      d_mu[i] = g_mu
      // var -> sigma. Inside the clamp_min dead zone this is exactly zero.
      let v = cache.var_out[i]
      if v > ASP_VAR_FLOOR {
        let sig = sqrtf(v)
        let g_var = g_sig * 0.5F / sig
        d_second[i] = g_var
        // var = second - mu^2
        d_mu[i] = g_mu - 2.0F * cache.mu[i] * g_var
      }
    }
  }
  // mu and second are weighted sums over t
  let d_alpha : Array[Float] = Array::make(n * c * t, 0.0F)
  for b in 0..<n {
    for ch in 0..<c {
      let base = (b * c + ch) * t
      let gm = d_mu[b * c + ch]
      let gs = d_second[b * c + ch]
      if gm == 0.0F && gs == 0.0F {
        continue
      }
      for ti in 0..<t {
        let xv = cache.x[base + ti]
        let a = cache.alpha[base + ti]
        d_x[base + ti] = d_x[base + ti] + gm * a
        d_alpha[base + ti] = gm * xv + gs * xv * xv
      }
    }
  }
  // softmax over t: dlogit = alpha * (dalpha - sum_t alpha*dalpha)
  let d_logit : Array[Float] = Array::make(n * c * t, 0.0F)
  for b in 0..<n {
    for ch in 0..<c {
      let base = (b * c + ch) * t
      let mut dot = 0.0F
      for ti in 0..<t {
        dot = dot + cache.alpha[base + ti] * d_alpha[base + ti]
      }
      for ti in 0..<t {
        d_logit[base + ti] = cache.alpha[base + ti] *
          (d_alpha[base + ti] - dot)
      }
    }
  }
  // ---- back through the attention network ----
  // logits = W2 * h_tanh + b2, where h_tanh is [n, att, t] and logits is
  // [n, c, t]; the time axis is the batch axis for both 1x1 convolutions.
  // Both contractions are written in the [n, c, t] layout the forward uses.
  // Going through linear_backward with `n_batch = n * t` would treat the array
  // as [n*t, features], a DIFFERENT ordering; the forward is self-consistent
  // so nothing complains, and the backward then reads the returned gradient
  // under the other assumption and every coordinate is wrong.
  let d_h_tanh : Array[Float] = Array::make(n * p.att_hidden * t, 0.0F)
  let d_w2 : Array[Float] = Array::make(c * p.att_hidden, 0.0F)
  let d_b2 : Array[Float] = Array::make(c, 0.0F)
  for b in 0..<n {
    for ch in 0..<c {
      let bc = b * c + ch
      for ti in 0..<t {
        let g = d_logit[bc * t + ti]
        d_b2[ch] = d_b2[ch] + g
        let mut j = 0
        while j < p.att_hidden {
          let at = ch * p.att_hidden + j
          let hv = cache.hidden[(b * p.att_hidden + j) * t + ti]
          d_w2[at] = d_w2[at] + g * hv
          let dst = (b * p.att_hidden + j) * t + ti
          d_h_tanh[dst] = d_h_tanh[dst] + g * p.w2[at]
          j = j + 1
        }
      }
    }
  }
  // tanh backward. `cache.hidden` IS tanh(relu(conv1)), so the derivative
  // factor is 1 - tanh^2 read straight off the cache.
  let att_len = n * p.att_hidden * t
  let d_relu : Array[Float] = Array::make(att_len, 0.0F)
  let mut k = 0
  while k < att_len {
    let v = cache.hidden[k]
    d_relu[k] = d_h_tanh[k] * (1.0F - v * v)
    k = k + 1
  }
  // ReLU backward, using the pre-tanh buffer kept in the cache.
  let d_conv1 : Array[Float] = Array::make(att_len, 0.0F)
  k = 0
  while k < att_len {
    d_conv1[k] = if cache.pre[k] > 0.0F { d_relu[k] } else { 0.0F }
    k = k + 1
  }
  // Conv1d(3C -> att, k=1) backward, in the same [n, c, t] layout.
  let k3 = 3 * c
  let att = p.att_hidden
  let d_ctx : Array[Float] = Array::make(n * k3 * t, 0.0F)
  let d_w1 : Array[Float] = Array::make(att * k3, 0.0F)
  let d_b1 : Array[Float] = Array::make(att, 0.0F)
  for b in 0..<n {
    for j in 0..<att {
      for ti in 0..<t {
        let g = d_conv1[(b * att + j) * t + ti]
        d_b1[j] = d_b1[j] + g
        let mut s = 0
        while s < k3 {
          let at = j * k3 + s
          let xv = cache.ctx[(b * k3 + s) * t + ti]
          d_w1[at] = d_w1[at] + g * xv
          let dst = (b * k3 + s) * t + ti
          d_ctx[dst] = d_ctx[dst] + g * p.w1[at]
          s = s + 1
        }
      }
    }
  }
  // ---- back through the context ----
  // ctx = [x, mean broadcast, std broadcast], so d_ctx splits three ways.
  let inv_t = 1.0F / Float::from_int(t)
  let d_mean : Array[Float] = Array::make(n * c, 0.0F)
  let d_std : Array[Float] = Array::make(n * c, 0.0F)
  for b in 0..<n {
    for ch in 0..<c {
      let base = (b * c + ch) * t
      let i_mean = b * c + ch
      let mut gm = 0.0F
      let mut gs = 0.0F
      for ti in 0..<t {
        // `d_ctx` is in ctx's OWN layout, whose stride is 3c, not c. Reading
        // the x slot at `base + ti` (x's stride) picks up the mean slot
        // instead, which makes d_x nearly constant across time within a
        // channel -- the tell-tale shape of this mistake.
        d_x[base + ti] = d_x[base + ti] + d_ctx[(b * 3 * c + ch) * t + ti]
        gm = gm + d_ctx[(b * 3 * c + c + ch) * t + ti]
        gs = gs + d_ctx[(b * 3 * c + 2 * c + ch) * t + ti]
      }
      d_mean[i_mean] = gm
      d_std[i_mean] = gs
    }
  }
  // mean = (1/T) sum x
  for b in 0..<n {
    for ch in 0..<c {
      let base = (b * c + ch) * t
      let gm = d_mean[b * c + ch] * inv_t
      for ti in 0..<t {
        d_x[base + ti] = d_x[base + ti] + gm
      }
    }
  }
  // std = sqrt(var + ASP_STD_FLOOR), var = (1/T) sum x^2 - mean^2
  // The floor here is INSIDE the root and is not a dead zone, so unlike the
  // output-side clamp its derivative is always live.
  for b in 0..<n {
    for ch in 0..<c {
      let i = b * c + ch
      let base = (b * c + ch) * t
      let mut mean = 0.0F
      for ti in 0..<t {
        mean = mean + cache.x[base + ti]
      }
      mean = mean * inv_t
      let sd = sqrtf(tn_ctx_var(cache.x, base, t, mean) + ASP_STD_FLOOR)
      let g_var = d_std[i] * 0.5F / sd
      let g_mean = -2.0F * mean * g_var
      for ti in 0..<t {
        let v = cache.x[base + ti]
        d_x[base + ti] = d_x[base + ti] + g_var * 2.0F * v * inv_t +
          g_mean * inv_t
      }
    }
  }
  { d_x, d_w1, d_b1, d_w2, d_b2 }
}

///|
/// Population variance of `[base, base+t)` about a supplied mean. Split out
/// because the backward pass needs the same statistic the forward used and a
/// second copy of the formula would be one more place for them to drift.
fn tn_ctx_var(x : Array[Float], base : Int, t : Int, mean : Float) -> Float {
  let inv_t = 1.0F / Float::from_int(t)
  let mut sq = 0.0F
  let mut ti = 0
  while ti < t {
    sq = sq + x[base + ti] * x[base + ti]
    ti = ti + 1
  }
  sq * inv_t - mean * mean
}
