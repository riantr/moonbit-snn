// gtrxl_block.mbt — Gated Transformer-XL (GTrXL) block (v0.72.0).
//
// Implementation of the GTrXL gating scheme from Parisotto et al.
// 2020 "Stabilizing Transformers for Reinforcement Learning".
//
// Key innovation: instead of additive residual `out = main_path(x) + x`,
// GTrXL uses a gating branch:
//   out = sigmoid(W_gate · LN(x)) ⊙ main_path(LN(x)) + x
// This dampens irrelevant residual contributions and stabilizes training
// over long horizons in RL.
//
// Scope of v0.72.0:
//   - GTrXLBlock struct + constructor (single per-token gated FFN)
//   - gtrxl_block_token_step (single-token forward; returns intermediates)
//   - gtrxl_block_seq_forward (T-step forward; returns per-step intermediates)
//   - GTrXLBlockCache (forward-pass cache for future BPTT)
//
// BPTT-driven update deferred. MHAttn integration deferred (per-token
// FFN path is the minimum useful GTrXL primitive; the full attention
// forward will be added in a follow-up).

///|
/// Gated Transformer-XL block. Per-token gated FFN + residual.
/// Differs from `TransformerBlock` (v0.23.3) by gating the FFN path
/// with a learned W_gate projection + element-wise sigmoid.
pub struct GTrXLBlock {
  d_model : Int
  d_ff : Int
  ffn_w1 : Array[Array[Float]]
  ffn_b1 : Array[Float]
  ffn_w2 : Array[Array[Float]]
  ffn_b2 : Array[Float]
  ffn_gate_w : Array[Array[Float]]
  ffn_gate_b : Array[Float]
}

///|
/// Per-token forward intermediates for one GTrXL block step. Returned
/// from `gtrxl_block_token_step` and stitched together by
/// `gtrxl_block_seq_forward`. Future BPTT backward will read these.
pub struct GTrXLTokenCache {
  x_input : Array[Float]
  ffn_gate_pre : Array[Float]
  ffn_hidden_pre : Array[Float]
  ffn_hidden_post : Array[Float]
  gated_ffn : Array[Float]
}

///|
/// Forward-pass cache for a full sequence through one GTrXL block.
/// Each field is `[seq_len × ...]` flattened. Reserved for the future
/// BPTT-driven update (v0.72.0 only ships forward).
pub struct GTrXLBlockCache {
  seq_len : Int
  x_input : Array[Float]
  ffn_gate_pre : Array[Float]
  ffn_hidden_pre : Array[Float]
  ffn_hidden_post : Array[Float]
  gated_ffn : Array[Float]
  y_seq : Array[Float]
}

///|
/// Build a fresh GTrXLBlock. Gate matrix init has small std (gain
/// ~0.1) so initial gate signal is roughly sigmoid(0) ≈ 0.5 (not too
/// aggressive at startup).
pub fn GTrXLBlock::new(
  d_model : Int,
  d_ff : Int,
  seed : UInt64,
) -> GTrXLBlock {
  let rng1 = Xoshiro::from_state(seed, seed + 1UL, seed + 2UL, seed + 3UL)
  let std1 = sqrtf(2.0F / Float::from_int(d_model))
  let ffn_w1 = xavier_normal(d_ff, d_model, std1, rng1)
  let ffn_b1 : Array[Float] = Array::make(d_ff, 0.0F)
  let rng2 = Xoshiro::from_state(seed + 4UL, seed + 5UL, seed + 6UL, seed + 7UL)
  let ffn_w2 = xavier_normal(d_model, d_ff, std1, rng2)
  let ffn_b2 : Array[Float] = Array::make(d_model, 0.0F)
  let rng3 = Xoshiro::from_state(seed + 8UL, seed + 9UL, seed + 10UL, seed + 11UL)
  let ffn_gate_w = xavier_normal(d_model, d_model, 0.1F * std1, rng3)
  let ffn_gate_b : Array[Float] = Array::make(d_model, 0.0F)
  { d_model, d_ff, ffn_w1, ffn_b1, ffn_w2, ffn_b2, ffn_gate_w, ffn_gate_b }
}

///|
/// Single-token GTrXL gated FFN + residual forward.
///
///   y = sigmoid(W_gate · x) ⊙ FFN(x) + x
///
/// `x` is a single token (length d_model). Returns
/// `(y, intermediates)` where intermediates are the forward pass
/// quantities needed for a future BPTT backward.
pub fn gtrxl_block_token_step(
  block : GTrXLBlock,
  x : Array[Float],
) -> (Array[Float], GTrXLTokenCache) {
  // Gate path: sigmoid(W_gate · x + b_gate)
  let gate_pre : Array[Float] = matvec(block.ffn_gate_w, block.ffn_gate_b, x)
  let gate : Array[Float] = Array::make(block.d_model, 0.0F)
  for k in 0..<block.d_model {
    // sigmoid via 1 / (1 + exp(-x))
    let g : Float = 1.0F / (1.0F + expf(-gate_pre[k]))
    gate[k] = g
  }
  // FFN main path: W2 · GELU(W1 · x + b1) + b2
  let ffn_pre : Array[Float] = matvec(block.ffn_w1, block.ffn_b1, x)
  let ffn_post : Array[Float] = Array::make(block.d_ff, 0.0F)
  for i in 0..<block.d_ff {
    ffn_post[i] = gelu(ffn_pre[i])
  }
  let ffn_out : Array[Float] = matvec(block.ffn_w2, block.ffn_b2, ffn_post)
  // Gated FFN + residual.
  let y : Array[Float] = Array::make(block.d_model, 0.0F)
  for k in 0..<block.d_model {
    y[k] = gate[k] * ffn_out[k] + x[k]
  }
  let cache : GTrXLTokenCache = {
    x_input: x,
    ffn_gate_pre: gate_pre,
    ffn_hidden_pre: ffn_pre,
    ffn_hidden_post: ffn_post,
    gated_ffn: y,
  }
  (y, cache)
}

///|
/// T-step forward through the GTrXL block. `x_seq` is flat
/// `[seq_len × d_model]`. Returns `(y_seq, full_cache)` where
/// `y_seq` is also flat `[seq_len × d_model]`.
pub fn gtrxl_block_seq_forward(
  block : GTrXLBlock,
  x_seq : Array[Float],
  seq_len : Int,
) -> (Array[Float], GTrXLBlockCache) {
  let y : Array[Float] = Array::make(seq_len * block.d_model, 0.0F)
  let x_in_buf : Array[Float] = Array::make(seq_len * block.d_model, 0.0F)
  let gate_pre_buf : Array[Float] = Array::make(seq_len * block.d_model, 0.0F)
  let hidden_pre_buf : Array[Float] = Array::make(seq_len * block.d_ff, 0.0F)
  let hidden_post_buf : Array[Float] = Array::make(seq_len * block.d_ff, 0.0F)
  let gated_buf : Array[Float] = Array::make(seq_len * block.d_model, 0.0F)
  for t in 0..<seq_len {
    let off = t * block.d_model
    let off_ff = t * block.d_ff
    let x_t : Array[Float] = Array::make(block.d_model, 0.0F)
    for k in 0..<block.d_model {
      x_t[k] = x_seq[off + k]
    }
    let (y_t, c_t) = gtrxl_block_token_step(block, x_t)
    for k in 0..<block.d_model {
      y[off + k] = y_t[k]
      x_in_buf[off + k] = c_t.x_input[k]
      gate_pre_buf[off + k] = c_t.ffn_gate_pre[k]
      gated_buf[off + k] = c_t.gated_ffn[k]
    }
    for i in 0..<block.d_ff {
      hidden_pre_buf[off_ff + i] = c_t.ffn_hidden_pre[i]
      hidden_post_buf[off_ff + i] = c_t.ffn_hidden_post[i]
    }
  }
  let cache : GTrXLBlockCache = {
    seq_len,
    x_input: x_in_buf,
    ffn_gate_pre: gate_pre_buf,
    ffn_hidden_pre: hidden_pre_buf,
    ffn_hidden_post: hidden_post_buf,
    gated_ffn: gated_buf,
    y_seq: y,
  }
  (y, cache)
}
