// gtrxl_actor.mbt — Recurrent deterministic actor with GTrXL block as
// recurrent memory (v0.73.0).
//
// Architecture: state_t → MLP w1 → ReLU → embed_t (length d_model) →
// GTrXL block (maintains a d_model-dimensional hidden state across time
// via gated residual updates) → MLP w2 → tanh squash to [action_low,
// action_high].
//
//   state_t ∈ R^{state_dim}
//   embed_t = ReLU(w1 · state_t + b1)                  ∈ R^{d_model}
//   hidden_t = gtrxl_block_token_step(embed_t).y       ∈ R^{d_model}
//   a_pre_t = w2 · hidden_t + b2                       ∈ R^{action_dim}
//   action_t = tanh(a_pre_t) * (high - low) / 2
//            + (high + low) / 2                        ∈ R^{action_dim}
//
// This is the GTrXL counterpart of `GRUDeterministicPolicy` (v0.58.0)
// and `LSTMDeterministicPolicy` (v0.61.0). The recurrent memory here
// is the GTrXL block's per-token gated residual update (the GTrXL
// block has its own internal state in the form of `x_t`-to-`x_t`
// identity residual; we feed it the embedding from the MLP).
//
// Reference: Parisotto et al. 2020 "Stabilizing Transformers for
// Reinforcement Learning"; Hausknecht & Stone 2015 for the recurrent
// actor pattern.

///|
/// Recurrent deterministic actor. The GTrXL block's d_model equals the
/// MLP hidden dimension so the ReLU-projected state and the GTrXL
/// block's token dim match.
pub struct GTrXLDeterministicPolicy {
  state_dim : Int
  action_dim : Int
  d_model : Int
  d_ff : Int
  mlp_w1 : Array[Array[Float]]
  mlp_b1 : Array[Float]
  block : GTrXLBlock
  mlp_w2 : Array[Array[Float]]
  mlp_b2 : Array[Float]
  action_low : Float
  action_high : Float
}

///|
/// Build a fresh GTrXLDeterministicPolicy. The MLP weights get
/// Xavier-normal init scaled by `sqrtf(2.0 / fan_in)` (He-style for
/// ReLU), and the GTrXL block uses its own init (Xavier-normal across
/// ffn_w1/ffn_w2 + 0.1×std for the gate matrix). Zero biases.
/// `action_low` / `action_high` define the squash range.
pub fn GTrXLDeterministicPolicy::new(
  state_dim : Int,
  action_dim : Int,
  d_model : Int,
  d_ff : Int,
  action_low : Float,
  action_high : Float,
  seed : UInt64,
) -> GTrXLDeterministicPolicy {
  let rng1 = Xoshiro::from_state(seed, seed + 1UL, seed + 2UL, seed + 3UL)
  let mlp_w1_std = sqrtf(2.0F / Float::from_int(state_dim))
  let mlp_w1 = xavier_normal(d_model, state_dim, mlp_w1_std, rng1)
  let mlp_b1 : Array[Float] = Array::make(d_model, 0.0F)
  let block = GTrXLBlock::new(d_model, d_ff, seed + 4UL)
  let rng2 = Xoshiro::from_state(seed + 8UL, seed + 9UL, seed + 10UL, seed + 11UL)
  let mlp_w2_std = sqrtf(2.0F / Float::from_int(d_model))
  let mlp_w2 = xavier_normal(action_dim, d_model, mlp_w2_std, rng2)
  let mlp_b2 : Array[Float] = Array::make(action_dim, 0.0F)
  {
    state_dim,
    action_dim,
    d_model,
    d_ff,
    mlp_w1,
    mlp_b1,
    block,
    mlp_w2,
    mlp_b2,
    action_low,
    action_high,
  }
}

///|
/// Single-step forward. `obs` is the current observation (length
/// state_dim). Returns `(action, block_cache)` where `block_cache`
/// holds the GTrXL per-step intermediates for a future BPTT
/// backward, and `action` is the squashed continuous action (length
/// action_dim).
pub fn gtrxl_actor_step(
  policy : GTrXLDeterministicPolicy,
  obs : Array[Float],
) -> (Array[Float], GTrXLTokenCache) {
  // embed = ReLU(w1 · obs + b1)
  let x_proj_pre = matvec(policy.mlp_w1, policy.mlp_b1, obs)
  let x_proj = relu_forward(x_proj_pre)
  // hidden_next = gtrxl_block_token_step(embed)
  let (hidden_next, cache) = gtrxl_block_token_step(policy.block, x_proj)
  // a_pre = w2 · hidden_next + b2
  let a_pre = matvec(policy.mlp_w2, policy.mlp_b2, hidden_next)
  // squash: action = tanh(a_pre) * (high - low) / 2 + (high + low) / 2
  let half_range = (policy.action_high - policy.action_low) * 0.5F
  let mid = (policy.action_high + policy.action_low) * 0.5F
  let action : Array[Float] = Array::make(policy.action_dim, 0.0F)
  for i in 0..<policy.action_dim {
    action[i] = tanhf(a_pre[i]) * half_range + mid
  }
  (action, cache)
}

///|
/// Sequence forward. `obs_seq` is flat row-major of shape
/// `[seq_len × state_dim]`. Returns `(action_seq, full_cache)` where
/// `action_seq` is flat `[seq_len × action_dim]` and `full_cache`
/// stitches per-step GTrXL intermediates into a single record for
/// future BPTT.
pub fn gtrxl_actor_seq_forward(
  policy : GTrXLDeterministicPolicy,
  obs_seq : Array[Float],
  seq_len : Int,
) -> (Array[Float], GTrXLBlockCache) {
  let action_seq : Array[Float] = Array::make(
    seq_len * policy.action_dim, 0.0F,
  )
  // Stitch per-step caches into a single record. The block receives
  // the freshly-embedded token each step (no carry-over of an external
  // hidden state — the gated-residual inside the GTrXL block is the
  // recurrent memory itself, propagated token-to-token via `y`).
  let x_in_buf : Array[Float] = Array::make(seq_len * policy.d_model, 0.0F)
  let gate_pre_buf : Array[Float] = Array::make(
    seq_len * policy.d_model, 0.0F,
  )
  let hidden_pre_buf : Array[Float] = Array::make(seq_len * policy.d_ff, 0.0F)
  let hidden_post_buf : Array[Float] = Array::make(
    seq_len * policy.d_ff, 0.0F,
  )
  let gated_buf : Array[Float] = Array::make(seq_len * policy.d_model, 0.0F)
  for t in 0..<seq_len {
    let obs_off = t * policy.state_dim
    let off = t * policy.d_model
    let off_ff = t * policy.d_ff
    let obs : Array[Float] = Array::make(policy.state_dim, 0.0F)
    for k in 0..<policy.state_dim {
      obs[k] = obs_seq[obs_off + k]
    }
    let (action, c_t) = gtrxl_actor_step(policy, obs)
    let act_off = t * policy.action_dim
    for k in 0..<policy.action_dim {
      action_seq[act_off + k] = action[k]
    }
    for k in 0..<policy.d_model {
      x_in_buf[off + k] = c_t.x_input[k]
      gate_pre_buf[off + k] = c_t.ffn_gate_pre[k]
      gated_buf[off + k] = c_t.gated_ffn[k]
    }
    for i in 0..<policy.d_ff {
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
    y_seq: action_seq,
  }
  (action_seq, cache)
}
