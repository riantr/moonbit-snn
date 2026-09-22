// spiking_attention.mbt — Spiking self-attention (SpikeFormer-style).
//
// Like standard MHA but replaces softmax along the key axis with the
// fast-sigmoid surrogate from `surrogate.mbt` (v0.22.0):
//
//   weights[h, s, t] = fast_sigmoid_forward(scores[h, s, t], beta)
//                    = scores[h, s, t] / (1 + β · |scores[h, s, t]|)
//
// Range per element: (-1/β, 1/β). The backward pass uses
// `fast_sigmoid_surrogate` (σ' = 1/(1 + β·|x|)², peak 1 at x=0).
//
// Forward:
//
//   q = x @ W_q, k = x @ W_k, v = x @ W_v
//   scores = q @ k^T / sqrt(d_k)        (per-head)
//   weights = fast_sigmoid_forward(scores, beta)   (no softmax!)
//   out_pre = weights @ v                (per-head)
//   y = out_pre @ W_o                    (head-merged)
//
// Backward:
//
//   d_out_pre, d_W_o, d_b_o from linear_backward through W_o
//   d_weights = (d_out_pre per-head) @ V^T
//   d_V      += W^T @ (d_out_pre per-head)
//   d_scores  = d_weights * fast_sigmoid_surrogate(scores, beta)
//   d_q, d_k  from scaled-dot-product backward (×scale)
//   d_x       = linear_backward_input (×Q, K, V) summed
//
// Float32 throughout. `mask` is the same additive mask as standard
// MHA (apply to scores before fast_sigmoid).

///|
/// Spiking self-attention parameter container.
pub struct SpikingAttention {
  d_model : Int
  num_heads : Int
  d_k : Int
  beta : Float    // surrogate inverse-slope factor (larger β = sharper)
  w_q : LinearParam
  w_k : LinearParam
  w_v : LinearParam
  w_o : LinearParam
}

///|
/// Forward / backward cache.
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
/// Forward spiking self-attention. `mask` is the same additive
/// mask as standard MHA (pass empty array to skip).
pub fn spiking_attention_forward(
  x : Array[Float],
  sa : SpikingAttention,
  mask : Array[Float],
) -> (Array[Float], SpikingAttnCache) {
  let seq_len = x.length() / sa.d_model
  let d_model = sa.d_model
  let num_heads = sa.num_heads
  let d_k = sa.d_k
  let scale = 1.0F / sqrtf(Float::from_int(d_k))
  let beta = sa.beta

  let q = linear_forward(x, seq_len, sa.w_q)
  let k = linear_forward(x, seq_len, sa.w_k)
  let v = linear_forward(x, seq_len, sa.w_v)

  // Scaled dot-product scores per head.
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
  let cache : SpikingAttnCache = {
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
/// Spiking attention backward pass. Returns `(d_input, MHAGrad)`.
pub fn spiking_attention_backward(
  cache : SpikingAttnCache,
  d_output : Array[Float],
  sa : SpikingAttention,
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

  // Per-head d_weights[h, s, t] and d_V contribution.
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

  // Backward through fast_sigmoid: d_scores = d_weights * σ'(scores).
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

  // Backward through scaled dot-product: d_q, d_k from d_scores × scale.
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

  // Backward through Q / K / V projections.
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

///|
/// Build a SpikingAttention with Xavier-normal init.
pub fn SpikingAttention::new(
  d_model : Int,
  num_heads : Int,
  beta : Float,
  seed : UInt64,
) -> SpikingAttention {
  if d_model % num_heads != 0 {
    abort("SpikingAttention::new: d_model not divisible by num_heads")
  }
  let rng = Xoshiro::from_state(seed, seed + 1UL, seed + 2UL, seed + 3UL)
  let w_q = linear_xavier(rng, d_model)
  let w_k = linear_xavier(rng, d_model)
  let w_v = linear_xavier(rng, d_model)
  let w_o = linear_xavier(rng, d_model)
  { d_model, num_heads, d_k: d_model / num_heads, beta, w_q, w_k, w_v, w_o }
}