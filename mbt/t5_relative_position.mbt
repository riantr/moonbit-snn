// t5_relative_position.mbt — T5-style relative position bias
// (Raffel 2020 §3.2.3) for self-attention.
//
// Unlike absolute position embeddings (added to the input), T5 uses
// learned *relative* position biases that are added to the attention
// scores:
//
//   scores += bias[h, i, j]
//
// where the bias is indexed by the *relative* offset (j - i) clamped
// to `[-max_distance, max_distance]`. There is one bias table per
// attention head, shape `(num_heads, 2*max_distance+1)`.
//
// For a query at position `i` attending to a key at position `j`:
//
//   offset = j - i
//   bucket = clamp(offset, -max_distance, max_distance) + max_distance
//   bias[h, i, j] = weights[h, bucket]
//
// The bias table is added to the existing scores *before* softmax.
// This implementation uses a flat row-major layout with one shared
// bucket range per head (no log-bucket scheme for simplicity).

///|
/// T5-style relative position bias parameter container.
pub struct T5RelativePosition {
  num_heads : Int
  max_distance : Int
  // bias table: shape (num_heads, 2 * max_distance + 1).
  weight : Array[Float]
}

///|
/// Construct a T5 relative position bias. `max_distance` controls
/// how far apart positions can be before the bias saturates (T5
/// paper uses 128; we use a smaller default for demos).
pub fn T5RelativePosition::new(
  num_heads : Int,
  max_distance : Int,
  seed : UInt64,
) -> T5RelativePosition {
  let n_buckets = 2 * max_distance + 1
  let rng = Xoshiro::from_state(seed, seed + 1UL, seed + 2UL, seed + 3UL)
  let std = sqrtf(1.0F / Float::from_int(n_buckets))
  let weight : Array[Float] = Array::make(num_heads * n_buckets, 0.0F)
  let mut i = 0
  while i < num_heads * n_buckets {
    let (z, _) = box_muller(rng)
    weight[i] = Float::from_double(z) * std
    i = i + 1
  }
  { num_heads, max_distance, weight }
}

///|
/// Compute the per-head bias matrix of shape
/// (num_heads × seq_len × seq_len). `bias[h, i, j]` is indexed by
/// the clamped relative offset (j - i).
pub fn t5_compute_bias(
  rp : T5RelativePosition,
  seq_len : Int,
) -> Array[Float] {
  let out : Array[Float] = Array::make(
    rp.num_heads * seq_len * seq_len, 0.0F,
  )
  let n_buckets = 2 * rp.max_distance + 1
  for h in 0..<rp.num_heads {
    for i in 0..<seq_len {
      for j in 0..<seq_len {
        let offset = j - i
        let clamped = if offset < -rp.max_distance {
          -rp.max_distance
        } else if offset > rp.max_distance {
          rp.max_distance
        } else {
          offset
        }
        let bucket = clamped + rp.max_distance
        out[h * seq_len * seq_len + i * seq_len + j] =
          rp.weight[h * n_buckets + bucket]
      }
    }
  }
  out
}

///|
/// Backward: returns `d_weight` of shape (num_heads, 2*max_distance+1).
///
/// `d_bias[h, i, j]` is the upstream gradient on the bias entry at
/// (h, i, j). Sum over (i, j) bucketed by `bucket(j - i)`:
///
///   d_weight[h, b] = sum_{i, j: bucket(j-i)==b} d_bias[h, i, j]
pub fn t5_backward(
  rp : T5RelativePosition,
  d_bias : Array[Float],
  seq_len : Int,
) -> Array[Float] {
  let n_buckets = 2 * rp.max_distance + 1
  let d_weight : Array[Float] = Array::make(
    rp.num_heads * n_buckets, 0.0F,
  )
  for h in 0..<rp.num_heads {
    for i in 0..<seq_len {
      for j in 0..<seq_len {
        let offset = j - i
        let clamped = if offset < -rp.max_distance {
          -rp.max_distance
        } else if offset > rp.max_distance {
          rp.max_distance
        } else {
          offset
        }
        let bucket = clamped + rp.max_distance
        d_weight[h * n_buckets + bucket] = d_weight[h * n_buckets + bucket] +
          d_bias[h * seq_len * seq_len + i * seq_len + j]
      }
    }
  }
  d_weight
}