// vit_block.mbt — ViTBlock: transformer block for Vision Transformer
// (v0.110.0).
//
// A standard ViT block (pre-norm variant):
//   x' = x + MHA(LayerNorm(x))
//   y  = x' + MLP(LayerNorm(x'))
// where MLP is a 2-layer FFN with GELU activation:
//   MLP(z) = W_2 · GELU(W_1 · z + b_1) + b_2
//
// Scope of v0.110.0:
//   - ViTBlock struct (MHA + LayerNorm params + 2-layer MLP weights)
//   - vit_block_forward: full pre-norm block pass
//   - vit_block_apply_layernorm: per-token LayerNorm helper
//   - vit_block_mlp: standalone 2-layer FFN with GELU
//
// Reference: Dosovitskiy et al. 2020; standard ViT block follows the
// pre-norm Transformer block from "On Layer Normalization in the
// Transformer Architecture".

///|
/// ViTBlock: MHA + LayerNorm + 2-layer MLP. All weights are Linear
/// projections without bias on the MHA (uses the existing
/// MultiHeadAttention struct from multi_head_attention.mbt).
pub struct ViTBlock {
  d_model : Int
  num_heads : Int
  // Pre-norm parameters (per-token LayerNorm).
  ln1_gamma : Array[Float]
  ln1_beta : Array[Float]
  ln2_gamma : Array[Float]
  ln2_beta : Array[Float]
  // MHA.
  mha : MultiHeadAttention
  // MLP (2-layer FFN with GELU).
  mlp_w1 : Array[Array[Float]]
  mlp_b1 : Array[Float]
  mlp_w2 : Array[Array[Float]]
  mlp_b2 : Array[Float]
  mlp_hidden : Int
}

///|
/// Build a fresh ViTBlock.
pub fn ViTBlock::new(
  d_model : Int,
  num_heads : Int,
  mlp_hidden : Int,
  seed : UInt64,
) -> ViTBlock {
  let ln1_gamma : Array[Float] = Array::make(d_model, 1.0F)
  let ln1_beta : Array[Float] = Array::make(d_model, 0.0F)
  let ln2_gamma : Array[Float] = Array::make(d_model, 1.0F)
  let ln2_beta : Array[Float] = Array::make(d_model, 0.0F)
  let mha = MultiHeadAttention::new(d_model, num_heads, seed)
  let rng1 = Xoshiro::from_state(seed + 4UL, seed + 5UL, seed + 6UL, seed + 7UL)
  let std1 = sqrtf(2.0F / Float::from_int(d_model))
  let mlp_w1 = xavier_normal(mlp_hidden, d_model, std1, rng1)
  let mlp_b1 : Array[Float] = Array::make(mlp_hidden, 0.0F)
  let rng2 = Xoshiro::from_state(seed + 8UL, seed + 9UL, seed + 10UL, seed + 11UL)
  let std2 = sqrtf(2.0F / Float::from_int(mlp_hidden))
  let mlp_w2 = xavier_normal(d_model, mlp_hidden, std2, rng2)
  let mlp_b2 : Array[Float] = Array::make(d_model, 0.0F)
  { d_model, num_heads, ln1_gamma, ln1_beta, ln2_gamma, ln2_beta, mha, mlp_w1, mlp_b1, mlp_w2, mlp_b2, mlp_hidden }
}

///|
/// Per-token LayerNorm (one mean/variance per token row). Operates on
/// flat row-major [seq_len x d_model]. `gamma` and `beta` are length
/// d_model.
pub fn vit_block_apply_layernorm(
  x : Array[Float],
  seq_len : Int,
  d_model : Int,
  gamma : Array[Float],
  beta : Array[Float],
  eps : Float,
) -> Array[Float] {
  let out : Array[Float] = Array::make(seq_len * d_model, 0.0F)
  for s in 0..<seq_len {
    let off = s * d_model
    // Compute mean.
    let mut mean = 0.0F
    for i in 0..<d_model {
      mean = mean + x[off + i]
    }
    mean = mean / Float::from_int(d_model)
    // Compute variance.
    let mut variance = 0.0F
    for i in 0..<d_model {
      let d = x[off + i] - mean
      variance = variance + d * d
    }
    variance = variance / Float::from_int(d_model)
    let std = sqrtf(variance + eps)
    for i in 0..<d_model {
      out[off + i] = gamma[i] * (x[off + i] - mean) / std + beta[i]
    }
  }
  out
}

///|
/// 2-layer FFN with GELU activation: y = W_2 · GELU(W_1 · x + b_1) + b_2.
pub fn vit_block_mlp(
  block : ViTBlock,
  x : Array[Float],
  seq_len : Int,
) -> Array[Float] {
  let hidden : Array[Float] = Array::make(
    seq_len * block.mlp_hidden, 0.0F,
  )
  let out : Array[Float] = Array::make(
    seq_len * block.d_model, 0.0F,
  )
  for s in 0..<seq_len {
    let off = s * block.d_model
    let h_off = s * block.mlp_hidden
    // First Linear + GELU.
    for i in 0..<block.mlp_hidden {
      let mut a = block.mlp_b1[i]
      for k in 0..<block.d_model {
        a = a + block.mlp_w1[i][k] * x[off + k]
      }
      hidden[h_off + i] = gelu(a)
    }
    // Second Linear.
    for i in 0..<block.d_model {
      let mut a = block.mlp_b2[i]
      for k in 0..<block.mlp_hidden {
        a = a + block.mlp_w2[i][k] * hidden[h_off + k]
      }
      out[s * block.d_model + i] = a
    }
  }
  out
}

///|
/// ViTBlock forward pass. Input is flat row-major
/// [seq_len x d_model]. Returns the output tensor (same shape).
pub fn vit_block_forward(
  block : ViTBlock,
  x : Array[Float],
  seq_len : Int,
) -> Array[Float] {
  let d_model = block.d_model
  // 1. Pre-norm + MHA + residual.
  let norm1 = vit_block_apply_layernorm(
    x, seq_len, d_model, block.ln1_gamma, block.ln1_beta, 1.0e-5F,
  )
  // Build an empty mask (no masking).
  let empty_mask : Array[Float] = Array::make(0, 0.0F)
  let (attn_out, _cache) = multi_head_attention_forward(
    norm1, block.mha, empty_mask,
  )
  let residual1 : Array[Float] = Array::make(
    seq_len * d_model, 0.0F,
  )
  let total_elems = seq_len * d_model
  for i in 0..<total_elems {
    residual1[i] = x[i] + attn_out[i]
  }
  // 2. Pre-norm + MLP + residual.
  let norm2 = vit_block_apply_layernorm(
    residual1, seq_len, d_model, block.ln2_gamma, block.ln2_beta, 1.0e-5F,
  )
  let mlp_out = vit_block_mlp(block, norm2, seq_len)
  let out : Array[Float] = Array::make(
    seq_len * d_model, 0.0F,
  )
  for i in 0..<total_elems {
    out[i] = residual1[i] + mlp_out[i]
  }
  out
}