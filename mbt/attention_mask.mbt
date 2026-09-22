// attention_mask.mbt — Attention mask utilities for transformer
// self-attention.
//
// Two common mask shapes are supported:
//
//   Per-head      (num_heads × seq_len × seq_len)  → fed directly
//                                                  to MHA.
//   Head-broadcast (seq_len × seq_len)            → expanded to
//                                                  per-head via
//                                                  mask_broadcast.
//
// Mask values are ADDITIVE — they are added to the attention scores
// before softmax. Use a large negative value (e.g. -1e9) to suppress
// a position; use 0 to leave it unconstrained.
//
// Causal mask is the upper-triangular -1e9 matrix used in
// autoregressive (decoder-style) attention.

///|
/// Build a causal mask of shape (seq_len, seq_len). Positions
/// `(i, j)` with `j > i` are masked (set to `-1e9`); `j <= i` are
/// unconstrained (set to 0).
///
/// Returns row-major flat `Array[Float]` of length `seq_len * seq_len`.
pub fn causal_mask(seq_len : Int) -> Array[Float] {
  let mask : Array[Float] = Array::make(seq_len * seq_len, 0.0F)
  for i in 0..<seq_len {
    for j in 0..<seq_len {
      if j > i {
        mask[i * seq_len + j] = -1000000000.0F
      }
    }
  }
  mask
}

///|
/// Broadcast a 2D mask (seq_len × seq_len) to per-head shape
/// (num_heads × seq_len × seq_len). All heads share the same mask.
pub fn mask_broadcast(
  mask_2d : Array[Float],
  seq_len : Int,
  num_heads : Int,
) -> Array[Float] {
  if mask_2d.length() != seq_len * seq_len {
    abort(
      "mask_broadcast: mask length \{mask_2d.length()} != seq_len^2 \{seq_len * seq_len}",
    )
  }
  let out : Array[Float] = Array::make(num_heads * seq_len * seq_len, 0.0F)
  for h in 0..<num_heads {
    for i in 0..<seq_len {
      for j in 0..<seq_len {
        out[h * seq_len * seq_len + i * seq_len + j] = mask_2d[i * seq_len + j]
      }
    }
  }
  out
}

///|
/// Build a per-position allowed-keys mask from a 2D boolean table.
///
/// `allowed[i][j]` is true iff position i is allowed to attend to key j.
/// Returns a (seq_len × seq_len) additive mask: 0 where allowed, -1e9
/// where blocked.
pub fn mask_from_allowed(
  seq_len : Int,
  allowed : Array[Array[Bool]],
) -> Array[Float] {
  if allowed.length() != seq_len {
    abort(
      "mask_from_allowed: outer length \{allowed.length()} != seq_len \{seq_len}",
    )
  }
  let mask : Array[Float] = Array::make(seq_len * seq_len, 0.0F)
  for i in 0..<seq_len {
    let row = allowed[i]
    if row.length() != seq_len {
      abort(
        "mask_from_allowed: row \{i} length \{row.length()} != seq_len \{seq_len}",
      )
    }
    for j in 0..<seq_len {
      if !row[j] {
        mask[i * seq_len + j] = -1000000000.0F
      }
    }
  }
  mask
}

///|
/// Convenience: causal mask directly broadcast to (num_heads × seq_len
/// × seq_len) for direct MHA consumption.
pub fn causal_mask_heads(seq_len : Int, num_heads : Int) -> Array[Float] {
  mask_broadcast(causal_mask(seq_len), seq_len, num_heads)
}