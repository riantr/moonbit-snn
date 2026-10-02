// gtrxl_qnetwork_continuous.mbt — Recurrent continuous-action Q-network
// with GTrXL block as recurrent memory (v0.74.0).
//
// Architecture: MLP w1 (concat(state, action) -> hidden) -> ReLU ->
// GTrXL block (maintains a d_model-dimensional hidden state across
// time via gated residual updates) -> MLP w2 (hidden -> scalar Q).
//
//   [state_t ; action_t]    ∈ R^{state_dim + action_dim}
//   x_proj_t = ReLU(w1 · [state_t ; action_t] + b1)         ∈ R^{d_model}
//   hidden_t = gtrxl_block_token_step(x_proj_t).y           ∈ R^{d_model}
//   q_t = w2 · hidden_t + b2                                ∈ R
//
// This is the GTrXL counterpart of `LSTMQNetworkContinuous` (v0.62.0)
// for the GTrXL_DDPG agent (v0.74.0). The recurrent memory is the
// GTrXL block's per-token gated residual update (carried token-to-token
// via the y → x residual).
//
// Reference: Parisotto et al. 2020; twin-critic TD pattern from
// Fujimoto et al. 2018 (TD3) adapted to recurrent actors.

///|
/// Recurrent continuous-action Q-network. MLP hidden dim equals the
/// GTrXL block's d_model so the ReLU-projected [state; action] and the
/// block's token dim match. `mlp_b2` is a scalar (the critic outputs a
/// single Q-value per timestep).
pub struct GTrXLQNetworkContinuous {
  state_dim : Int
  action_dim : Int
  d_model : Int
  d_ff : Int
  mlp_w1 : Array[Array[Float]]
  mlp_b1 : Array[Float]
  block : GTrXLBlock
  mlp_w2 : Array[Array[Float]]
  mut mlp_b2 : Float
}

///|
/// Build a fresh GTrXLQNetworkContinuous. MLP weights use
/// Xavier-normal init scaled by sqrtf(2 / fan_in) (He-style for ReLU).
/// GTrXL block uses its own init. Zero biases.
pub fn GTrXLQNetworkContinuous::new(
  state_dim : Int,
  action_dim : Int,
  d_model : Int,
  d_ff : Int,
  seed : UInt64,
) -> GTrXLQNetworkContinuous {
  let in_dim = state_dim + action_dim
  let rng1 = Xoshiro::from_state(seed, seed + 1UL, seed + 2UL, seed + 3UL)
  let std1 = sqrtf(2.0F / Float::from_int(in_dim))
  let mlp_w1 = xavier_normal(d_model, in_dim, std1, rng1)
  let mlp_b1 : Array[Float] = Array::make(d_model, 0.0F)
  let block = GTrXLBlock::new(d_model, d_ff, seed + 4UL)
  let rng2 = Xoshiro::from_state(seed + 8UL, seed + 9UL, seed + 10UL, seed + 11UL)
  let std2 = sqrtf(2.0F / Float::from_int(d_model))
  let mlp_w2 = xavier_normal(1, d_model, std2, rng2)
  {
    state_dim,
    action_dim,
    d_model,
    d_ff,
    mlp_w1,
    mlp_b1,
    block,
    mlp_w2,
    mlp_b2: 0.0F,
  }
}

///|
/// Single-step forward. Returns the scalar Q-value for the given
/// (state, action) pair plus the per-step GTrXL intermediates for a
/// future BPTT backward.
pub fn gtrxl_qnetwork_continuous_step(
  qnet : GTrXLQNetworkContinuous,
  state : Array[Float],
  action : Array[Float],
) -> (Float, GTrXLTokenCache) {
  // sa = concat(state, action)
  let sa = vec_concat(state, action)
  // x_proj = ReLU(w1 · sa + b1)
  let x_proj_pre = matvec(qnet.mlp_w1, qnet.mlp_b1, sa)
  let x_proj = relu_forward(x_proj_pre)
  // hidden_next = gtrxl_block_token_step(x_proj)
  let (hidden_next, cache) = gtrxl_block_token_step(qnet.block, x_proj)
  // q = w2 · hidden_next + b2
  let q_pre = matvec(qnet.mlp_w2, [qnet.mlp_b2], hidden_next)
  (q_pre[0], cache)
}

///|
/// Sequence forward. `state_seq` is flat row-major
/// `[seq_len × state_dim]`, `action_seq` is flat row-major
/// `[seq_len × action_dim]`. Returns `(q_seq, full_cache)` where
/// `q_seq` is flat `[seq_len]` and `full_cache` stitches per-step
/// GTrXL intermediates into a single record for future BPTT.
pub fn gtrxl_qnetwork_continuous_seq_forward(
  qnet : GTrXLQNetworkContinuous,
  state_seq : Array[Float],
  action_seq : Array[Float],
  seq_len : Int,
) -> (Array[Float], GTrXLBlockCache) {
  let q_seq : Array[Float] = Array::make(seq_len, 0.0F)
  let x_in_buf : Array[Float] = Array::make(seq_len * qnet.d_model, 0.0F)
  let gate_pre_buf : Array[Float] = Array::make(
    seq_len * qnet.d_model, 0.0F,
  )
  let hidden_pre_buf : Array[Float] = Array::make(seq_len * qnet.d_ff, 0.0F)
  let hidden_post_buf : Array[Float] = Array::make(
    seq_len * qnet.d_ff, 0.0F,
  )
  let gated_buf : Array[Float] = Array::make(seq_len * qnet.d_model, 0.0F)
  for t in 0..<seq_len {
    let st_off = t * qnet.state_dim
    let at_off = t * qnet.action_dim
    let state : Array[Float] = Array::make(qnet.state_dim, 0.0F)
    let action : Array[Float] = Array::make(qnet.action_dim, 0.0F)
    for k in 0..<qnet.state_dim {
      state[k] = state_seq[st_off + k]
    }
    for k in 0..<qnet.action_dim {
      action[k] = action_seq[at_off + k]
    }
    let (q, c_t) = gtrxl_qnetwork_continuous_step(qnet, state, action)
    q_seq[t] = q
    let off = t * qnet.d_model
    let off_ff = t * qnet.d_ff
    for k in 0..<qnet.d_model {
      x_in_buf[off + k] = c_t.x_input[k]
      gate_pre_buf[off + k] = c_t.ffn_gate_pre[k]
      gated_buf[off + k] = c_t.gated_ffn[k]
    }
    for i in 0..<qnet.d_ff {
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
    y_seq: q_seq,
  }
  (q_seq, cache)
}
