// multi_head_attention.mbt — Standard multi-head self-attention
// (Vaswani et al. 2017).
//
// Self-attention forward (single sequence, no batch):
//
//   x  : (seq_len, d_model)         row-major flat
//   q  = x @ W_q                    (seq_len, d_model)
//   k  = x @ W_k                    (seq_len, d_model)
//   v  = x @ W_v                    (seq_len, d_model)
//
//   split heads: q' has shape (num_heads, seq_len, d_k) where
//                d_k = d_model / num_heads.
//                Flat offset: h * seq_len * d_k + s * d_k + k
//                Maps to x-flat: x[s * d_model + h * d_k + k].
//
//   scores = q' @ k'^T / sqrt(d_k)  shape (num_heads, seq_len, seq_len)
//   if mask:  scores += mask        (additive; -inf blocks)
//   weights = softmax(scores, axis=-1)
//   out_pre = weights @ v'           shape (num_heads, seq_len, d_k)
//
//   merge heads: out_pre' has shape (seq_len, d_model)
//                Flat offset: s * d_model + h * d_k + k.
//   y  = out_pre' @ W_o              (seq_len, d_model)
//
// Bias is added inside each Linear projection.
//
// Initialisation: Xavier-normal for all 4 weight matrices
//   N(0, sqrt(2 / (d_in + d_out)))
//   For square d_in = d_out = d_model, std = sqrt(1/d_model).
//   Biases initialised to zero.
//
// Backward returns (d_x, MHAGrad). The cache retains x / q / k / v /
// scores / weights / out_pre for the reverse pass.

///|
/// Multi-head self-attention parameter container.
pub struct MultiHeadAttention {
  d_model : Int
  num_heads : Int
  d_k : Int
  w_q : LinearParam
  w_k : LinearParam
  w_v : LinearParam
  w_o : LinearParam
}

///|
/// Gradient bundle for an MHA module: d_weight / d_bias for each of
/// the four projections.
pub struct MHAGrad {
  d_w_q : Array[Float]
  d_b_q : Array[Float]
  d_w_k : Array[Float]
  d_b_k : Array[Float]
  d_w_v : Array[Float]
  d_b_v : Array[Float]
  d_w_o : Array[Float]
  d_b_o : Array[Float]
}

///|
/// Forward / backward cache for an MHA forward pass.
pub struct AttnCache {
  seq_len : Int
  // Forward input.
  x : Array[Float]
  // Post-projection q / k / v (each shape seq_len × d_model).
  q : Array[Float]
  k : Array[Float]
  v : Array[Float]
  // Pre-softmax scores, shape num_heads × seq_len × seq_len.
  scores : Array[Float]
  // Post-softmax attention weights.
  weights : Array[Float]
  // Output of weights @ v, pre-merge shape (seq_len × d_model,
  // flat).  Filled by merging heads on the fly.
  out_pre : Array[Float]
  // Optional mask (same shape as scores). Zero when no mask.
  mask : Array[Float]
  // 1 / sqrt(d_k) precomputed.
  scale : Float
}

// ---- initialisation helpers ---------------------------------------

///|
/// Fill an Array[Float] with Xavier-normal draws:
/// N(0, sqrt(2 / (fan_in + fan_out))). Uses Float32 Box-Muller.
fn xavier_normal_init(
  rng : Xoshiro,
  n : Int,
  fan_in : Int,
  fan_out : Int,
) -> Array[Float] {
  let out : Array[Float] = Array::make(n, 0.0F)
  let std = sqrtf(2.0F / Float::from_int(fan_in + fan_out))
  let mut i = 0
  while i < n {
    let (z1, _) = box_muller(rng)
    out[i] = Float::from_double(z1) * std
    i = i + 1
  }
  out
}

///|
/// Build a square LinearParam with Xavier-normal init + zero bias.
fn linear_xavier(
  rng : Xoshiro,
  d : Int,
) -> LinearParam {
  let weight = xavier_normal_init(rng, d * d, d, d)
  let bias : Array[Float] = Array::make(d, 0.0F)
  LinearParam::new(weight, bias, d, d)
}

// ---- public API ---------------------------------------------------

///|
/// Construct a fresh MHA. `seed` initialises the four weight matrices
/// deterministically (xoshiro256++ from_state with derived seeds).
pub fn MultiHeadAttention::new(
  d_model : Int,
  num_heads : Int,
  seed : UInt64,
) -> MultiHeadAttention {
  // Sanity: d_model must be divisible by num_heads.
  if d_model % num_heads != 0 {
    abort("MultiHeadAttention::new: d_model \{d_model} not divisible by num_heads \{num_heads}")
  }
  let rng = Xoshiro::from_state(seed, seed + 1UL, seed + 2UL, seed + 3UL)
  let w_q = linear_xavier(rng, d_model)
  let w_k = linear_xavier(rng, d_model)
  let w_v = linear_xavier(rng, d_model)
  let w_o = linear_xavier(rng, d_model)
  { d_model, num_heads, d_k: d_model / num_heads, w_q, w_k, w_v, w_o }
}

///|
/// Self-attention forward pass.
///
/// `x`         : length `seq_len * d_model`, row-major
/// `mask~`     : optional additive mask shape `num_heads * seq_len *
///               seq_len` (pass an empty array to skip).
/// returns     : `(out, cache)` where `out` is length
///               `seq_len * d_model`.
pub fn multi_head_attention_forward(
  x : Array[Float],
  mha : MultiHeadAttention,
  mask : Array[Float],
) -> (Array[Float], AttnCache) {
  let seq_len = x.length() / mha.d_model
  let d_model = mha.d_model
  let num_heads = mha.num_heads
  let d_k = mha.d_k
  let scale = 1.0F / sqrtf(Float::from_int(d_k))

  // 1) Q / K / V projections (each shape seq_len × d_model).
  let q = linear_forward(x, seq_len, mha.w_q)
  let k = linear_forward(x, seq_len, mha.w_k)
  let v = linear_forward(x, seq_len, mha.w_v)

  // 2) Scaled dot-product scores per head.
  //    scores[h, s, t] = sum_k q[s, h*d_k+k] * k[t, h*d_k+k] * scale
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

  // 3) Softmax along last axis (per (h, s)).
  let weights : Array[Float] = Array::make(num_heads * seq_len * seq_len, 0.0F)
  for h in 0..<num_heads {
    let score_off = h * seq_len * seq_len
    for s in 0..<seq_len {
      let s_row = score_off + s * seq_len
      // Max for numerical stability.
      let mut max_v = scores[s_row]
      let mut t = 1
      while t < seq_len {
        if scores[s_row + t] > max_v {
          max_v = scores[s_row + t]
        }
        t = t + 1
      }
      let mut sum_exp = 0.0F
      t = 0
      while t < seq_len {
        let e = expf(scores[s_row + t] - max_v)
        weights[s_row + t] = e
        sum_exp = sum_exp + e
        t = t + 1
      }
      t = 0
      while t < seq_len {
        weights[s_row + t] = weights[s_row + t] / sum_exp
        t = t + 1
      }
    }
  }

  // 4) Merge heads + apply W_o in one go: for each (s, h, kk),
  //    compute out_pre[s, h*d_k + kk] = sum_t weights[h, s, t] *
  //    v[t, h*d_k + kk].  Then linear_forward through W_o.
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

  // 5) Output projection.
  let out = linear_forward(out_pre, seq_len, mha.w_o)

  let cache = {
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
  }
  (out, cache)
}

///|
/// Self-attention backward pass. Returns `(d_input, MHAGrad)`.
pub fn multi_head_attention_backward(
  cache : AttnCache,
  d_output : Array[Float],
  mha : MultiHeadAttention,
) -> (Array[Float], MHAGrad) {
  let seq_len = cache.seq_len
  let d_model = mha.d_model
  let num_heads = mha.num_heads
  let d_k = mha.d_k
  let scale = cache.scale

  // 1) d_out_pre, d_W_o, d_b_o from d_output through W_o.
  let (d_out_pre, d_w_o, d_b_o) = linear_backward(
    cache.out_pre, d_output, seq_len, mha.w_o,
  )

  // Reshape d_out_pre to per-head layout (num_heads, seq_len, d_k).
  // d_out_h[h, s, kk] = d_out_pre[s * d_model + h * d_k + kk]

  // 2) d_weights[h, s, t] and d_v contribution.
  let weights = cache.weights
  let v = cache.v
  let d_weights : Array[Float] = Array::make(
    num_heads * seq_len * seq_len,
    0.0F,
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

  // 3) Backward through softmax(scores) -> weights.
  //    d_scores[h, s, t] = weights[h, s, t] * (d_weights[h, s, t]
  //        - sum_{t'} weights[h, s, t'] * d_weights[h, s, t'])
  let d_scores : Array[Float] = Array::make(
    num_heads * seq_len * seq_len,
    0.0F,
  )
  for h in 0..<num_heads {
    let score_off = h * seq_len * seq_len
    for s in 0..<seq_len {
      let s_row = score_off + s * seq_len
      let mut dot = 0.0F
      let mut t = 0
      while t < seq_len {
        dot = dot + weights[s_row + t] * d_weights[s_row + t]
        t = t + 1
      }
      t = 0
      while t < seq_len {
        let w_v = weights[s_row + t]
        let dw_v = d_weights[s_row + t]
        d_scores[s_row + t] = w_v * (dw_v - dot)
        t = t + 1
      }
    }
  }

  // 4) Backward through scores = (q @ k^T) * scale.
  let q = cache.q
  let k = cache.k
  let d_q : Array[Float] = Array::make(seq_len * d_model, 0.0F)
  let d_k_buf : Array[Float] = Array::make(seq_len * d_model, 0.0F)
  for h in 0..<num_heads {
    let h_off = h * d_k
    let score_off = h * seq_len * seq_len
    // d_q[s, h*d_k + kk] += scale * sum_t d_scores[h,s,t] * k[t,h*d_k+kk]
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
    // d_k[t, h*d_k + kk] += scale * sum_s d_scores[h,s,t] * q[s,h*d_k+kk]
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

  // 5) Backward through Q / K / V projections, then sum d_input.
  let x = cache.x
  let (d_x_q, d_w_q, d_b_q) = linear_backward(x, d_q, seq_len, mha.w_q)
  let (d_x_k, d_w_k, d_b_k) = linear_backward(x, d_k_buf, seq_len, mha.w_k)
  let (d_x_v, d_w_v, d_b_v) = linear_backward(x, d_v, seq_len, mha.w_v)
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