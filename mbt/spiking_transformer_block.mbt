// spiking_transformer_block.mbt — Pre-Norm Spiking Transformer block.
//
// Like TransformerBlock but uses `SpikingAttention` (v0.24.0) instead
// of standard `MultiHeadAttention`. The softmax along the key axis is
// replaced by `fast_sigmoid_forward`, and the backward uses the
// `fast_sigmoid_surrogate` (v0.22.0) for BPTT-compatible gradients.
//
//   x2 = x  +  SpikingAttention( LN1(x) )    # spiking self-attention path
//   y  = x2 +  FFN( LN2(x2) )                 # feed-forward path
//
// where FFN(h) = Linear2(GELU(Linear1(h))) and d_ff = 4 d_model.

///|
/// Spiking transformer block parameter container.
pub struct SpikingTransformerBlock {
  d_model : Int
  num_heads : Int
  d_ff : Int
  beta : Float
  ln_1 : LayerNorm
  ln_2 : LayerNorm
  sa : SpikingAttention
  ffn_w1 : LinearParam
  ffn_w2 : LinearParam
}

///|
/// Forward / backward cache.
pub struct SpikingTransformerBlockCache {
  seq_len : Int
  x : Array[Float]
  x2 : Array[Float]
  x_norm1 : Array[Float]
  x_norm2 : Array[Float]
  ln1_cache : LayerNormCache
  sa_cache : SpikingAttnCache
  ln2_cache : LayerNormCache
  ffn_hidden_pre_gelu : Array[Float]
  ffn_hidden_post_gelu : Array[Float]
  mask : Array[Float]
}

///|
/// Gradient bundle (reuses MHAGrad for the spiking attention
/// projection gradients since the Linear layer structure is identical).
pub struct SpikingTransformerBlockGrad {
  ln1_d_gamma : Array[Float]
  ln1_d_beta : Array[Float]
  sa_grad : MHAGrad
  ln2_d_gamma : Array[Float]
  ln2_d_beta : Array[Float]
  ffn_d_w1 : Array[Float]
  ffn_d_b1 : Array[Float]
  ffn_d_w2 : Array[Float]
  ffn_d_b2 : Array[Float]
}

///|
/// Construct a SpikingTransformerBlock. `d_ff` defaults to 4·d_model.
pub fn SpikingTransformerBlock::new(
  d_model : Int,
  num_heads : Int,
  beta : Float,
  seed : UInt64,
  d_ff? : Int = -1,
) -> SpikingTransformerBlock {
  let actual_d_ff = if d_ff < 0 { 4 * d_model } else { d_ff }
  let ln_1 = LayerNorm::new(1, 1, d_model)
  let ln_2 = LayerNorm::new(1, 1, d_model)
  let sa = SpikingAttention::new(d_model, num_heads, beta, seed + 100UL)
  let rng = Xoshiro::from_state(seed, seed + 1UL, seed + 2UL, seed + 3UL)
  let w1_weight = xavier_normal_init(rng, d_model * actual_d_ff, d_model, actual_d_ff)
  let w1_bias : Array[Float] = Array::make(actual_d_ff, 0.0F)
  let ffn_w1 = LinearParam::new(w1_weight, w1_bias, d_model, actual_d_ff)
  let w2_weight = xavier_normal_init(rng, actual_d_ff * d_model, actual_d_ff, d_model)
  let w2_bias : Array[Float] = Array::make(d_model, 0.0F)
  let ffn_w2 = LinearParam::new(w2_weight, w2_bias, actual_d_ff, d_model)
  { d_model, num_heads, d_ff: actual_d_ff, beta, ln_1, ln_2, sa, ffn_w1, ffn_w2 }
}

///|
/// Forward pass.
pub fn spiking_transformer_block_forward(
  x : Array[Float],
  block : SpikingTransformerBlock,
  mask : Array[Float],
) -> (Array[Float], SpikingTransformerBlockCache) {
  let seq_len = x.length() / block.d_model
  let d_model = block.d_model

  // 1) Pre-LN1 + spiking self-attention + residual.
  let (x_norm1, ln1_cache) = layer_norm_forward(
    x, seq_len, 1, 1, d_model, block.ln_1,
  )
  let (sa_out, sa_cache) = spiking_attention_forward(
    x_norm1, block.sa, mask,
  )
  let x2 : Array[Float] = Array::make(seq_len * d_model, 0.0F)
  for i in 0..<(seq_len * d_model) {
    x2[i] = x[i] + sa_out[i]
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

  let cache : SpikingTransformerBlockCache = {
    seq_len,
    x,
    x2,
    x_norm1,
    x_norm2,
    ln1_cache,
    sa_cache,
    ln2_cache,
    ffn_hidden_pre_gelu,
    ffn_hidden_post_gelu,
    mask,
  }
  (out, cache)
}

///|
/// Backward pass.
pub fn spiking_transformer_block_backward(
  cache : SpikingTransformerBlockCache,
  d_output : Array[Float],
  block : SpikingTransformerBlock,
) -> (Array[Float], SpikingTransformerBlockGrad) {
  let seq_len = cache.seq_len
  let d_model = block.d_model

  // ---- FFN residual split ----
  // ---- FFN path backward ----
  let (d_ffn_hidden_post_gelu, d_w2, d_b2) = linear_backward(
    cache.ffn_hidden_post_gelu, d_output, seq_len, block.ffn_w2,
  )
  let d_ffn_pre_gelu = gelu_backward(
    cache.ffn_hidden_pre_gelu, d_ffn_hidden_post_gelu,
  )
  let (d_x_norm2, d_w1, d_b1) = linear_backward(
    cache.x_norm2, d_ffn_pre_gelu, seq_len, block.ffn_w1,
  )
  // LN2 backward.
  let (d_x2_from_ln2, d_gamma2, d_beta2) = layer_norm_backward(
    d_x_norm2, cache.ln2_cache, block.ln_2,
  )
  let d_x2 : Array[Float] = Array::make(seq_len * d_model, 0.0F)
  for i in 0..<(seq_len * d_model) {
    d_x2[i] = d_output[i] + d_x2_from_ln2[i]
  }

  // ---- Spiking attention backward ----
  let (d_x_norm1, sa_grad) = spiking_attention_backward(
    cache.sa_cache, d_x2, block.sa,
  )
  let (d_x_from_ln1, d_gamma1, d_beta1) = layer_norm_backward(
    d_x_norm1, cache.ln1_cache, block.ln_1,
  )

  let d_x : Array[Float] = Array::make(seq_len * d_model, 0.0F)
  for i in 0..<(seq_len * d_model) {
    d_x[i] = d_x2[i] + d_x_from_ln1[i]
  }

  let grads : SpikingTransformerBlockGrad = {
    ln1_d_gamma: d_gamma1,
    ln1_d_beta: d_beta1,
    sa_grad,
    ln2_d_gamma: d_gamma2,
    ln2_d_beta: d_beta2,
    ffn_d_w1: d_w1,
    ffn_d_b1: d_b1,
    ffn_d_w2: d_w2,
    ffn_d_b2: d_b2,
  }
  (d_x, grads)
}