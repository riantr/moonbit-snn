// sattention.mbt — Consolidated Spiking Attention (SAttention) module.
//
// Three related attention variants sharing the same fast-sigmoid
// forward + surrogate backward pattern (Zenke & Ganguli 2018):
//
//   - SpikingMultiHeadAttention (multi-head self-attention, MHA-style)
//   - SpikingSelfAttention (single-head self-attention; just
//                            num_heads=1 convenience over the above)
//   - SpikingCrossAttention (cross-attention: separate Q and K/V inputs)
//
// All three replace softmax along the key axis with
// `fast_sigmoid_forward` from `surrogate.mbt` (v0.22.0) and use
// `fast_sigmoid_surrogate` as the backward gradient.
//
// Backward-compat: `SpikingAttention` is typealiased to
// `SpikingMultiHeadAttention`, so existing calls in `spiking_attention.mbt`
// keep working.

// ============================================================================
// Multi-head self-attention
// ============================================================================

///|
/// Spiking multi-head self-attention parameter container.
pub struct SpikingMultiHeadAttention {
  d_model : Int
  num_heads : Int
  d_k : Int
  beta : Float
  w_q : LinearParam
  w_k : LinearParam
  w_v : LinearParam
  w_o : LinearParam
}

///|
/// Forward / backward cache for multi-head self-attention.
pub struct SpikingMultiHeadAttnCache {
  seq_len : Int
  x : Array[Float]
  q : Array[Float]
  k : Array[Float]
  v : Array[Float]
  scores : Array[Float]
  weights : Array[Float]
  out_pre : Array[Float]
  mask : Array[Float]
  scale : Float
  beta : Float
}

///|
/// Backward-compat struct (was the v0.24.0 name for multi-head
/// self-attention). Structurally identical to `SpikingMultiHeadAttention`.
/// New code should use `SpikingMultiHeadAttention` directly.
pub struct SpikingAttention {
  d_model : Int
  num_heads : Int
  d_k : Int
  beta : Float
  w_q : LinearParam
  w_k : LinearParam
  w_v : LinearParam
  w_o : LinearParam
}

///|
/// Backward-compat cache (was `SpikingAttnCache` in v0.24.0).
pub struct SpikingAttnCache {
  seq_len : Int
  x : Array[Float]
  q : Array[Float]
  k : Array[Float]
  v : Array[Float]
  scores : Array[Float]
  weights : Array[Float]
  out_pre : Array[Float]
  mask : Array[Float]
  scale : Float
  beta : Float
}

///|
/// Construct a SpikingMultiHeadAttention with Xavier-normal init.
pub fn SpikingMultiHeadAttention::new(
  d_model : Int,
  num_heads : Int,
  beta : Float,
  seed : UInt64,
) -> SpikingMultiHeadAttention {
  if d_model % num_heads != 0 {
    abort("SpikingMultiHeadAttention::new: d_model not divisible by num_heads")
  }
  let rng = Xoshiro::from_state(seed, seed + 1UL, seed + 2UL, seed + 3UL)
  let w_q = linear_xavier(rng, d_model)
  let w_k = linear_xavier(rng, d_model)
  let w_v = linear_xavier(rng, d_model)
  let w_o = linear_xavier(rng, d_model)
  { d_model, num_heads, d_k: d_model / num_heads, beta, w_q, w_k, w_v, w_o }
}

///|
/// Convenience: build a single-head spiking self-attention by
/// calling `SpikingMultiHeadAttention::new(d_model, 1, beta, seed)`.
/// No separate struct — single-head is just multi-head with
/// num_heads=1.
pub fn spiking_self_attention_new(
  d_model : Int,
  beta : Float,
  seed : UInt64,
) -> SpikingMultiHeadAttention {
  SpikingMultiHeadAttention::new(d_model, 1, beta, seed)
}

///|
/// Multi-head self-attention forward. `mask` is the same additive
/// mask as standard MHA (pass empty array to skip).
pub fn spiking_multi_head_attention_forward(
  x : Array[Float],
  sa : SpikingMultiHeadAttention,
  mask : Array[Float],
) -> (Array[Float], SpikingMultiHeadAttnCache) {
  let seq_len = x.length() / sa.d_model
  let d_model = sa.d_model
  let num_heads = sa.num_heads
  let d_k = sa.d_k
  let scale = 1.0F / sqrtf(Float::from_int(d_k))
  let beta = sa.beta

  let q = linear_forward(x, seq_len, sa.w_q)
  let k = linear_forward(x, seq_len, sa.w_k)
  let v = linear_forward(x, seq_len, sa.w_v)

  let scores : Array[Float] = Array::make(num_heads * seq_len * seq_len, 0.0F)
  let use_mask = mask.length() > 0
  for h in 0..<num_heads {
    let h_off = h * d_k
    let score_off = h * seq_len * seq_len
    for s in 0..<seq_len {
      let s_row = score_off + s * seq_len
      for t in 0..<seq_len {
        let mut acc = 0.0F
        for kk in 0..<d_k {
          acc = acc + q[s * d_model + h_off + kk] * k[t * d_model + h_off + kk]
        }
        scores[s_row + t] = acc * scale
        if use_mask {
          scores[s_row + t] = scores[s_row + t] + mask[s_row + t]
        }
      }
    }
  }

  // Spiking attention weights via fast_sigmoid_forward (NO softmax).
  let weights : Array[Float] = Array::make(
    num_heads * seq_len * seq_len, 0.0F,
  )
  for h in 0..<num_heads {
    let score_off = h * seq_len * seq_len
    for s in 0..<seq_len {
      let s_row = score_off + s * seq_len
      for t in 0..<seq_len {
        weights[s_row + t] = fast_sigmoid_forward(scores[s_row + t], beta)
      }
    }
  }

  // Per-head weighted sum, merged into (seq_len, d_model) layout.
  let out_pre : Array[Float] = Array::make(seq_len * d_model, 0.0F)
  for h in 0..<num_heads {
    let h_off = h * d_k
    let score_off = h * seq_len * seq_len
    for s in 0..<seq_len {
      let s_row = score_off + s * seq_len
      for kk in 0..<d_k {
        let mut acc = 0.0F
        let mut t = 0
        while t < seq_len {
          acc = acc + weights[s_row + t] * v[t * d_model + h_off + kk]
          t = t + 1
        }
        out_pre[s * d_model + h_off + kk] = acc
      }
    }
  }

  let out = linear_forward(out_pre, seq_len, sa.w_o)
  let cache : SpikingMultiHeadAttnCache = {
    seq_len,
    x,
    q,
    k,
    v,
    scores,
    weights,
    out_pre,
    mask: if use_mask { mask } else { Array::make(0, 0.0F) },
    scale,
    beta,
  }
  (out, cache)
}

///|
/// Multi-head self-attention backward.
pub fn spiking_multi_head_attention_backward(
  cache : SpikingMultiHeadAttnCache,
  d_output : Array[Float],
  sa : SpikingMultiHeadAttention,
) -> (Array[Float], MHAGrad) {
  let seq_len = cache.seq_len
  let d_model = sa.d_model
  let num_heads = sa.num_heads
  let d_k = sa.d_k
  let scale = cache.scale
  let beta = cache.beta

  let (d_out_pre, d_w_o, d_b_o) = linear_backward(
    cache.out_pre, d_output, seq_len, sa.w_o,
  )

  let weights = cache.weights
  let v = cache.v
  let d_weights : Array[Float] = Array::make(
    num_heads * seq_len * seq_len, 0.0F,
  )
  let d_v : Array[Float] = Array::make(seq_len * d_model, 0.0F)
  for h in 0..<num_heads {
    let h_off = h * d_k
    let score_off = h * seq_len * seq_len
    for s in 0..<seq_len {
      let s_row = score_off + s * seq_len
      for t in 0..<seq_len {
        let w_v = weights[s_row + t]
        let mut acc_dw = 0.0F
        for kk in 0..<d_k {
          let d_o = d_out_pre[s * d_model + h_off + kk]
          acc_dw = acc_dw + d_o * v[t * d_model + h_off + kk]
          d_v[t * d_model + h_off + kk] = d_v[t * d_model + h_off + kk] +
            w_v * d_o
        }
        d_weights[s_row + t] = acc_dw
      }
    }
  }

  let d_scores : Array[Float] = Array::make(
    num_heads * seq_len * seq_len, 0.0F,
  )
  for h in 0..<num_heads {
    let score_off = h * seq_len * seq_len
    for s in 0..<seq_len {
      let s_row = score_off + s * seq_len
      for t in 0..<seq_len {
        let sigma_prime = fast_sigmoid_surrogate(scores_at(cache.scores, score_off + s * seq_len + t), beta)
        d_scores[s_row + t] = d_weights[s_row + t] * sigma_prime
      }
    }
  }

  let q = cache.q
  let k = cache.k
  let d_q : Array[Float] = Array::make(seq_len * d_model, 0.0F)
  let d_k_buf : Array[Float] = Array::make(seq_len * d_model, 0.0F)
  for h in 0..<num_heads {
    let h_off = h * d_k
    let score_off = h * seq_len * seq_len
    for s in 0..<seq_len {
      let s_row = score_off + s * seq_len
      for kk in 0..<d_k {
        let mut acc = 0.0F
        let mut t = 0
        while t < seq_len {
          acc = acc + d_scores[s_row + t] * k[t * d_model + h_off + kk]
          t = t + 1
        }
        d_q[s * d_model + h_off + kk] = d_q[s * d_model + h_off + kk] +
          scale * acc
      }
    }
    for t in 0..<seq_len {
      for kk in 0..<d_k {
        let mut acc = 0.0F
        let mut s = 0
        while s < seq_len {
          acc = acc + d_scores[score_off + s * seq_len + t] *
            q[s * d_model + h_off + kk]
          s = s + 1
        }
        d_k_buf[t * d_model + h_off + kk] = d_k_buf[t * d_model + h_off + kk] +
          scale * acc
      }
    }
  }

  let x = cache.x
  let (d_x_q, d_w_q, d_b_q) = linear_backward(x, d_q, seq_len, sa.w_q)
  let (d_x_k, d_w_k, d_b_k) = linear_backward(x, d_k_buf, seq_len, sa.w_k)
  let (d_x_v, d_w_v, d_b_v) = linear_backward(x, d_v, seq_len, sa.w_v)
  let d_x : Array[Float] = Array::make(seq_len * d_model, 0.0F)
  for i in 0..<(seq_len * d_model) {
    d_x[i] = d_x_q[i] + d_x_k[i] + d_x_v[i]
  }
  let grads : MHAGrad = {
    d_w_q,
    d_b_q,
    d_w_k,
    d_b_k,
    d_w_v,
    d_b_v,
    d_w_o,
    d_b_o,
  }
  (d_x, grads)
}

// Tiny helper to keep the inner loop above readable.
fn scores_at(arr : Array[Float], i : Int) -> Float {
  arr[i]
}

// ============================================================================
// Cross-attention (Q from one input, K/V from another)
// ============================================================================

///|
/// Spiking cross-attention parameter container. Q is computed from
/// `x_q`, K/V are computed from a separate `x_kv` input.
pub struct SpikingCrossAttention {
  d_model : Int
  num_heads : Int
  d_k : Int
  beta : Float
  w_q : LinearParam
  w_k : LinearParam
  w_v : LinearParam
  w_o : LinearParam
}

///|
/// Forward / backward cache for cross-attention.
pub struct SpikingCrossAttnCache {
  seq_len_q : Int
  seq_len_kv : Int
  x_q : Array[Float]
  x_kv : Array[Float]
  q : Array[Float]
  k : Array[Float]
  v : Array[Float]
  scores : Array[Float]  // (num_heads * seq_len_q * seq_len_kv)
  weights : Array[Float]
  out_pre : Array[Float]  // (seq_len_q * d_model)
  mask : Array[Float]
  scale : Float
  beta : Float
}

///|
/// Construct a SpikingCrossAttention with Xavier-normal init.
pub fn SpikingCrossAttention::new(
  d_model : Int,
  num_heads : Int,
  beta : Float,
  seed : UInt64,
) -> SpikingCrossAttention {
  if d_model % num_heads != 0 {
    abort("SpikingCrossAttention::new: d_model not divisible by num_heads")
  }
  let rng = Xoshiro::from_state(seed, seed + 1UL, seed + 2UL, seed + 3UL)
  let w_q = linear_xavier(rng, d_model)
  let w_k = linear_xavier(rng, d_model)
  let w_v = linear_xavier(rng, d_model)
  let w_o = linear_xavier(rng, d_model)
  { d_model, num_heads, d_k: d_model / num_heads, beta, w_q, w_k, w_v, w_o }
}

///|
/// Cross-attention forward.
///
/// `x_q`  : (seq_len_q, d_model)
/// `x_kv` : (seq_len_kv, d_model)
/// `mask` : (num_heads, seq_len_q, seq_len_kv) additive, or empty.
/// `returns` : out of shape (seq_len_q, d_model).
pub fn spiking_cross_attention_forward(
  x_q : Array[Float],
  x_kv : Array[Float],
  sa : SpikingCrossAttention,
  mask : Array[Float],
) -> (Array[Float], SpikingCrossAttnCache) {
  let seq_len_q = x_q.length() / sa.d_model
  let seq_len_kv = x_kv.length() / sa.d_model
  let d_model = sa.d_model
  let num_heads = sa.num_heads
  let d_k = sa.d_k
  let scale = 1.0F / sqrtf(Float::from_int(d_k))
  let beta = sa.beta

  let q = linear_forward(x_q, seq_len_q, sa.w_q)
  let k = linear_forward(x_kv, seq_len_kv, sa.w_k)
  let v = linear_forward(x_kv, seq_len_kv, sa.w_v)

  // scores[h, s, t] = sum_k q[s, h*d_k+k] * k[t, h*d_k+k] * scale
  let scores : Array[Float] = Array::make(
    num_heads * seq_len_q * seq_len_kv, 0.0F,
  )
  let use_mask = mask.length() > 0
  for h in 0..<num_heads {
    let h_off = h * d_k
    let score_off = h * seq_len_q * seq_len_kv
    for s in 0..<seq_len_q {
      let s_row = score_off + s * seq_len_kv
      for t in 0..<seq_len_kv {
        let mut acc = 0.0F
        for kk in 0..<d_k {
          acc = acc + q[s * d_model + h_off + kk] *
            k[t * d_model + h_off + kk]
        }
        scores[s_row + t] = acc * scale
        if use_mask {
          scores[s_row + t] = scores[s_row + t] + mask[s_row + t]
        }
      }
    }
  }

  // Spiking weights via fast_sigmoid_forward.
  let weights : Array[Float] = Array::make(
    num_heads * seq_len_q * seq_len_kv, 0.0F,
  )
  for h in 0..<num_heads {
    let score_off = h * seq_len_q * seq_len_kv
    for s in 0..<seq_len_q {
      let s_row = score_off + s * seq_len_kv
      for t in 0..<seq_len_kv {
        weights[s_row + t] = fast_sigmoid_forward(scores[s_row + t], beta)
      }
    }
  }

  // Per-head weighted sum, merged into (seq_len_q, d_model) layout.
  let out_pre : Array[Float] = Array::make(seq_len_q * d_model, 0.0F)
  for h in 0..<num_heads {
    let h_off = h * d_k
    let score_off = h * seq_len_q * seq_len_kv
    for s in 0..<seq_len_q {
      let s_row = score_off + s * seq_len_kv
      for kk in 0..<d_k {
        let mut acc = 0.0F
        let mut t = 0
        while t < seq_len_kv {
          acc = acc + weights[s_row + t] * v[t * d_model + h_off + kk]
          t = t + 1
        }
        out_pre[s * d_model + h_off + kk] = acc
      }
    }
  }

  let out = linear_forward(out_pre, seq_len_q, sa.w_o)
  let cache : SpikingCrossAttnCache = {
    seq_len_q,
    seq_len_kv,
    x_q,
    x_kv,
    q,
    k,
    v,
    scores,
    weights,
    out_pre,
    mask: if use_mask { mask } else { Array::make(0, 0.0F) },
    scale,
    beta,
  }
  (out, cache)
}

///|
/// Cross-attention backward. Returns `(d_x_q, d_x_kv, MHAGrad)`.
/// Q and KV paths share the MHAGrad bundle since the projection
/// structure is identical.
pub fn spiking_cross_attention_backward(
  cache : SpikingCrossAttnCache,
  d_output : Array[Float],
  sa : SpikingCrossAttention,
) -> (Array[Float], Array[Float], MHAGrad) {
  let seq_len_q = cache.seq_len_q
  let seq_len_kv = cache.seq_len_kv
  let d_model = sa.d_model
  let num_heads = sa.num_heads
  let d_k = sa.d_k
  let scale = cache.scale
  let beta = cache.beta

  let (d_out_pre, d_w_o, d_b_o) = linear_backward(
    cache.out_pre, d_output, seq_len_q, sa.w_o,
  )

  let weights = cache.weights
  let v = cache.v
  let d_weights : Array[Float] = Array::make(
    num_heads * seq_len_q * seq_len_kv, 0.0F,
  )
  let d_v : Array[Float] = Array::make(seq_len_kv * d_model, 0.0F)
  for h in 0..<num_heads {
    let h_off = h * d_k
    let score_off = h * seq_len_q * seq_len_kv
    for s in 0..<seq_len_q {
      let s_row = score_off + s * seq_len_kv
      for t in 0..<seq_len_kv {
        let w_v = weights[s_row + t]
        let mut acc_dw = 0.0F
        for kk in 0..<d_k {
          let d_o = d_out_pre[s * d_model + h_off + kk]
          acc_dw = acc_dw + d_o * v[t * d_model + h_off + kk]
          d_v[t * d_model + h_off + kk] = d_v[t * d_model + h_off + kk] +
            w_v * d_o
        }
        d_weights[s_row + t] = acc_dw
      }
    }
  }

  let d_scores : Array[Float] = Array::make(
    num_heads * seq_len_q * seq_len_kv, 0.0F,
  )
  for h in 0..<num_heads {
    let score_off = h * seq_len_q * seq_len_kv
    for s in 0..<seq_len_q {
      let s_row = score_off + s * seq_len_kv
      for t in 0..<seq_len_kv {
        let sigma_prime = fast_sigmoid_surrogate(
          scores_at(cache.scores, score_off + s * seq_len_kv + t), beta,
        )
        d_scores[s_row + t] = d_weights[s_row + t] * sigma_prime
      }
    }
  }

  let q = cache.q
  let k = cache.k
  let d_q : Array[Float] = Array::make(seq_len_q * d_model, 0.0F)
  let d_k_buf : Array[Float] = Array::make(seq_len_kv * d_model, 0.0F)
  for h in 0..<num_heads {
    let h_off = h * d_k
    let score_off = h * seq_len_q * seq_len_kv
    // d_q[s, h*d_k + kk] = scale * sum_t d_scores[h,s,t] * k[t, h*d_k+kk]
    for s in 0..<seq_len_q {
      let s_row = score_off + s * seq_len_kv
      for kk in 0..<d_k {
        let mut acc = 0.0F
        let mut t = 0
        while t < seq_len_kv {
          acc = acc + d_scores[s_row + t] * k[t * d_model + h_off + kk]
          t = t + 1
        }
        d_q[s * d_model + h_off + kk] = d_q[s * d_model + h_off + kk] +
          scale * acc
      }
    }
    // d_k[t, h*d_k + kk] = scale * sum_s d_scores[h,s,t] * q[s, h*d_k+kk]
    for t in 0..<seq_len_kv {
      for kk in 0..<d_k {
        let mut acc = 0.0F
        let mut s = 0
        while s < seq_len_q {
          acc = acc + d_scores[score_off + s * seq_len_kv + t] *
            q[s * d_model + h_off + kk]
          s = s + 1
        }
        d_k_buf[t * d_model + h_off + kk] = d_k_buf[t * d_model + h_off + kk] +
          scale * acc
      }
    }
  }

  let x_q = cache.x_q
  let x_kv = cache.x_kv
  // Q path: d_x_q via d_q.
  let (d_x_q, d_w_q, d_b_q) = linear_backward(x_q, d_q, seq_len_q, sa.w_q)
  // KV path: d_x_kv via d_k + d_v.
  let (d_x_k, d_w_k, d_b_k) = linear_backward(
    x_kv, d_k_buf, seq_len_kv, sa.w_k,
  )
  let (d_x_v, d_w_v, d_b_v) = linear_backward(x_kv, d_v, seq_len_kv, sa.w_v)
  let d_x_kv : Array[Float] = Array::make(seq_len_kv * d_model, 0.0F)
  for i in 0..<(seq_len_kv * d_model) {
    d_x_kv[i] = d_x_k[i] + d_x_v[i]
  }
  let grads : MHAGrad = {
    d_w_q,
    d_b_q,
    d_w_k,
    d_b_k,
    d_w_v,
    d_b_v,
    d_w_o,
    d_b_o,
  }
  (d_x_q, d_x_kv, grads)
}