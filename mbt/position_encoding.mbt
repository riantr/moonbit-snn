// position_encoding.mbt — sinusoidal and learnable position embeddings.
//
// Two complementary encodings for transformer input:
//
// 1. Sinusoidal (Vaswani 2017 §3.5) — fixed, non-trainable:
//      PE(pos, 2i)   = sin(pos / 10000^(2i / d_model))
//      PE(pos, 2i+1) = cos(pos / 10000^(2i / d_model))
//    Returns the full (max_len, d_model) table.
//
// 2. Learnable (BERT / ViT / GPT-2 style) — trainable parameter
//    matrix initialised with N(0, 1) (or Xavier), shape
//    (max_len, d_model).  Backward only accumulates gradients at the
//    active positions actually used.
//
// Combine via element-wise add: x + positional_embedding(x).

///|
/// Sinusoidal positional encoding table (non-trainable).
///
/// For position `pos` ∈ [0, max_len) and embedding index `j` ∈
/// [0, d_model):
///
///   PE[pos, j] = sin(pos / 10000^(⌊j/2⌋·2 / d_model))   if j is even
///   PE[pos, j] = cos(pos / 10000^(⌊j/2⌋·2 / d_model))   if j is odd
///
/// Returns row-major flat array of length `max_len * d_model`.
pub fn sinusoidal_position_encoding(
  max_len : Int,
  d_model : Int,
) -> Array[Float] {
  let out : Array[Float] = Array::make(max_len * d_model, 0.0F)
  for pos in 0..<max_len {
    for j in 0..<d_model {
      // Frequency band: 1 / 10000^(2·⌊j/2⌋ / d_model)
      let two_i_over_d = 2.0F * Float::from_int(j / 2) / Float::from_int(d_model)
      let freq = 1.0F / pow_10000(two_i_over_d)
      let angle = Float::from_int(pos) * freq
      let v = if j % 2 == 0 { sinf(angle) } else { cosf(angle) }
      out[pos * d_model + j] = v
    }
  }
  out
}

///|
/// Helper: `10000^x` for Float32 via natural log / exp.
fn pow_10000(x : Float) -> Float {
  expf(x * 4.605170185988091F) // 10000 = e^(ln 10000)
}

///|
/// Learnable positional embedding parameter container.
pub struct PositionalEmbedding {
  max_len : Int
  d_model : Int
  // Row-major flat (max_len, d_model).
  weight : Array[Float]
}

///|
/// Construct a learnable positional embedding with Xavier-normal
/// init (N(0, sqrt(2 / (1 + d_model)))).
pub fn PositionalEmbedding::new(
  max_len : Int,
  d_model : Int,
  seed : UInt64,
) -> PositionalEmbedding {
  let rng = Xoshiro::from_state(seed, seed + 1UL, seed + 2UL, seed + 3UL)
  let std = sqrtf(2.0F / Float::from_int(1 + d_model))
  let weight : Array[Float] = Array::make(max_len * d_model, 0.0F)
  let mut i = 0
  while i < max_len * d_model {
    let (z1, _) = box_muller(rng)
    weight[i] = Float::from_double(z1) * std
    i = i + 1
  }
  { max_len, d_model, weight }
}

///|
/// Slice the embedding table to the first `seq_len` rows. Returns a
/// fresh row-major array of length `seq_len * d_model`. Caller must
/// ensure `seq_len <= max_len`.
pub fn positional_embedding_forward(
  pe : PositionalEmbedding,
  seq_len : Int,
) -> Array[Float] {
  if seq_len > pe.max_len {
    abort("positional_embedding_forward: seq_len \{seq_len} > max_len \{pe.max_len}")
  }
  let d = pe.d_model
  let out : Array[Float] = Array::make(seq_len * d, 0.0F)
  for s in 0..<seq_len {
    for j in 0..<d {
      out[s * d + j] = pe.weight[s * d + j]
    }
  }
  out
}

///|
/// Backward for learnable positional embedding. `d_output` has shape
/// (seq_len, d_model). Returns a fresh d_weight of shape
/// (max_len, d_model) — only the first `seq_len` rows carry gradient.
pub fn positional_embedding_backward(
  pe : PositionalEmbedding,
  d_output : Array[Float],
  seq_len : Int,
) -> Array[Float] {
  let d = pe.d_model
  let d_weight : Array[Float] = Array::make(pe.max_len * d, 0.0F)
  for s in 0..<seq_len {
    for j in 0..<d {
      d_weight[s * d + j] = d_output[s * d + j]
    }
  }
  d_weight
}