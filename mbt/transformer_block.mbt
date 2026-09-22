// transformer_block.mbt — Pre-Norm Transformer block
// (Vaswani 2017 §3.1, Pre-LN variant).
//
// One block, two sub-layers, each Pre-LN + residual:
//
//   x2 = x  +  MHA( LN1(x) )                       # self-attention path
//   y  = x2 +  FFN( LN2(x2) )                      # feed-forward path
//
// where:
//
//   FFN(h) = Linear2( GELU( Linear1(h) ) )
//         = W2 · GELU(W1 · h + b1) + b2
//
// and W1 : (d_model → d_ff), W2 : (d_ff → d_model), d_ff = 4 d_model
// by default.
//
// All sub-modules (LayerNorm, MHA, Linear, GELU) are composed. The
// block has its own parameter container and forward/backward cache.
//
// Float32 throughout. `mask` is an optional additive attention mask
// passed straight through to MHA; pass an empty array to skip.

///|
/// Transformer block parameter container.
pub struct TransformerBlock {
  d_model : Int
  num_heads : Int
  d_ff : Int
  ln_1 : LayerNorm
  ln_2 : LayerNorm
  mha : MultiHeadAttention
  ffn_w1 : LinearParam
  ffn_w2 : LinearParam
}

///|
/// Per-block gradient bundle.
pub struct TransformerBlockGrad {
  ln1_d_gamma : Array[Float]
  ln1_d_beta : Array[Float]
  mha_grad : MHAGrad
  ln2_d_gamma : Array[Float]
  ln2_d_beta : Array[Float]
  ffn_d_w1 : Array[Float]
  ffn_d_b1 : Array[Float]
  ffn_d_w2 : Array[Float]
  ffn_d_b2 : Array[Float]
}

///|
/// Forward / backward cache for a Transformer block.
pub struct TransformerBlockCache {
  seq_len : Int
  // Original input (for residual d_x).
  x : Array[Float]
  // Post-attention residual sum (for residual d_x2 from FFN path).
  x2 : Array[Float]
  // Output of LN1, input to MHA (needed to reconstruct d_x_norm1
  // during backward when only LN1's cache is available).
  x_norm1 : Array[Float]
  // Output of LN2, input to linear1 of FFN (needed by linear_backward).
  x_norm2 : Array[Float]
  // Sub-layer caches.
  ln1_cache : LayerNormCache
  attn_cache : AttnCache
  ln2_cache : LayerNormCache
  // FFN intermediates: pre-GELU hidden state (for GELU backward) +
  // post-GELU hidden state (for linear2 backward).
  ffn_hidden_pre_gelu : Array[Float]
  ffn_hidden_post_gelu : Array[Float]
  // Mask used in MHA (or empty array for none).
  mask : Array[Float]
}

///|
/// Construct a Transformer block. `d_ff` defaults to `4 * d_model`.
pub fn TransformerBlock::new(
  d_model : Int,
  num_heads : Int,
  seed : UInt64,
  d_ff? : Int = -1,
) -> TransformerBlock {
  let actual_d_ff = if d_ff < 0 { 4 * d_model } else { d_ff }
  let ln_1 = LayerNorm::new(1, 1, d_model)
  let ln_2 = LayerNorm::new(1, 1, d_model)
  let mha = MultiHeadAttention::new(d_model, num_heads, seed + 100UL)
  let rng = Xoshiro::from_state(seed, seed + 1UL, seed + 2UL, seed + 3UL)
  // FFN linear1: (d_model -> d_ff), linear2: (d_ff -> d_model).
  let w1_weight = xavier_normal_init(rng, d_model * actual_d_ff, d_model, actual_d_ff)
  let w1_bias : Array[Float] = Array::make(actual_d_ff, 0.0F)
  let ffn_w1 = LinearParam::new(w1_weight, w1_bias, d_model, actual_d_ff)
  let w2_weight = xavier_normal_init(rng, actual_d_ff * d_model, actual_d_ff, d_model)
  let w2_bias : Array[Float] = Array::make(d_model, 0.0F)
  let ffn_w2 = LinearParam::new(w2_weight, w2_bias, actual_d_ff, d_model)
  { d_model, num_heads, d_ff: actual_d_ff, ln_1, ln_2, mha, ffn_w1, ffn_w2 }
}

///|
/// Forward pass. `mask` is forwarded to MHA (pass empty array to skip).
pub fn transformer_block_forward(
  x : Array[Float],
  block : TransformerBlock,
  mask : Array[Float],
) -> (Array[Float], TransformerBlockCache) {
  let seq_len = x.length() / block.d_model
  let d_model = block.d_model

  // 1) Pre-LN1 + self-attention + residual.
  let (x_norm1, ln1_cache) = layer_norm_forward(
    x, seq_len, 1, 1, d_model, block.ln_1,
  )
  let (attn_out, attn_cache) = multi_head_attention_forward(
    x_norm1, block.mha, mask,
  )
  let x2 : Array[Float] = Array::make(seq_len * d_model, 0.0F)
  for i in 0..<(seq_len * d_model) {
    x2[i] = x[i] + attn_out[i]
  }

  // 2) Pre-LN2 + FFN + residual.
  let (x_norm2, ln2_cache) = layer_norm_forward(
    x2, seq_len, 1, 1, d_model, block.ln_2,
  )
  let pre_gelu = linear_forward(x_norm2, seq_len, block.ffn_w1)
  let ffn_hidden_pre_gelu = Array::make(pre_gelu.length(), 0.0F)
  for i in 0..<pre_gelu.length() {
    ffn_hidden_pre_gelu[i] = pre_gelu[i]
  }
  let post_gelu = gelu_forward(pre_gelu)
  let ffn_hidden_post_gelu = Array::make(post_gelu.length(), 0.0F)
  for i in 0..<post_gelu.length() {
    ffn_hidden_post_gelu[i] = post_gelu[i]
  }
  let ffn_out = linear_forward(post_gelu, seq_len, block.ffn_w2)
  let out : Array[Float] = Array::make(seq_len * d_model, 0.0F)
  for i in 0..<(seq_len * d_model) {
    out[i] = x2[i] + ffn_out[i]
  }

  let cache : TransformerBlockCache = {
    seq_len,
    x,
    x2,
    x_norm1,
    x_norm2,
    ln1_cache,
    attn_cache,
    ln2_cache,
    ffn_hidden_pre_gelu,
    ffn_hidden_post_gelu,
    mask,
  }
  (out, cache)
}

///|
/// Backward pass. Returns `(d_input, TransformerBlockGrad)`.
pub fn transformer_block_backward(
  cache : TransformerBlockCache,
  d_output : Array[Float],
  block : TransformerBlock,
) -> (Array[Float], TransformerBlockGrad) {
  let seq_len = cache.seq_len
  let d_model = block.d_model

  // ---- Step A: residual split on FFN path ----
  // out = x2 + ffn_out   ⇒   d_x2 (FFN residual) = d_output,
  //                          d_ffn_out = d_output.
  // d_x2_from_ln2 will be added below.

  // ---- Step B: FFN path backward ----
  // linear2 backward (input = ffn_hidden_post_gelu).
  let (d_ffn_hidden_post_gelu, d_w2, d_b2) = linear_backward(
    cache.ffn_hidden_post_gelu, d_output, seq_len, block.ffn_w2,
  )
  // GELU backward (input = ffn_hidden_pre_gelu).
  let d_ffn_pre_gelu = gelu_backward(
    cache.ffn_hidden_pre_gelu, d_ffn_hidden_post_gelu,
  )
  // linear1 backward (input = x_norm2).
  let (d_x_norm2, d_w1, d_b1) = linear_backward(
    cache.x_norm2, d_ffn_pre_gelu, seq_len, block.ffn_w1,
  )

  // ---- Step C: LN2 backward (input = x2 from cache) ----
  let (d_x2_from_ln2, d_gamma2, d_beta2) = layer_norm_backward(
    d_x_norm2, cache.ln2_cache, block.ln_2,
  )
  // Total d_x2 = d_output (residual) + d_x2_from_ln2.
  let d_x2 : Array[Float] = Array::make(seq_len * d_model, 0.0F)
  for i in 0..<(seq_len * d_model) {
    d_x2[i] = d_output[i] + d_x2_from_ln2[i]
  }

  // ---- Step D: residual split on MHA path ----
  // x2 = x + attn_out   ⇒   d_attn_out = d_x2,
  //                          d_x (residual from MHA) = d_x2.

  // ---- Step E: MHA backward ----
  let (d_x_norm1, mha_grad) = multi_head_attention_backward(
    cache.attn_cache, d_x2, block.mha,
  )

  // ---- Step F: LN1 backward ----
  let (d_x_from_ln1, d_gamma1, d_beta1) = layer_norm_backward(
    d_x_norm1, cache.ln1_cache, block.ln_1,
  )

  // ---- Step G: total d_input ----
  // d_x = d_x2 (residual from MHA path) + d_x_from_ln1.
  let d_x : Array[Float] = Array::make(seq_len * d_model, 0.0F)
  for i in 0..<(seq_len * d_model) {
    d_x[i] = d_x2[i] + d_x_from_ln1[i]
  }

  let grads : TransformerBlockGrad = {
    ln1_d_gamma: d_gamma1,
    ln1_d_beta: d_beta1,
    mha_grad,
    ln2_d_gamma: d_gamma2,
    ln2_d_beta: d_beta2,
    ffn_d_w1: d_w1,
    ffn_d_b1: d_b1,
    ffn_d_w2: d_w2,
    ffn_d_b2: d_b2,
  }
  (d_x, grads)
}