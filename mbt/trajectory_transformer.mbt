// trajectory_transformer.mbt — Trajectory Transformer primitive:
// 4-modality (state, action, reward, done) tokenization + 4 prediction
// heads (next_state, reward, done, value) on a GTrXL backbone (v0.80.0).
//
// Reference: Janner et al. 2021 "Trajectory Transformer: One Model to
// Plan Them All". The original TT uses a single transformer to model
// the joint distribution over (states, actions, rewards, done-flags) in
// a continuous-control trajectory. Here we substitute a GTrXL block as
// the per-token recurrent memory primitive (same substitution as
// Decision Transformer in Batch H).
//
// Per-timestep token layout (similar to DT but with reward + done):
//   token_t = Linear_state(s_t) + Linear_action(a_t) + Linear_reward(r_t) + Linear_done(d_t) + Embed_timestep(t)
//
// Four prediction heads (applied to the post-GTrXL hidden state):
//   next_state_t = W_state · hidden_t + b_state     ∈ R^{state_dim}
//   reward_t     = W_reward · hidden_t + b_reward   ∈ R
//   done_t       = W_done · hidden_t + b_done       ∈ R
//   value_t      = W_value · hidden_t + b_value     ∈ R
//
// Scope of v0.80.0:
//   - TTEmbeddings struct + constructor (4 modality projections + timestep)
//   - TrajectoryTransformer struct + constructor (embeddings + GTrXLBlock + 4 heads)
//   - tt_forward: full forward pass — tokenize → GTrXL → 4 heads
//   - tt_predict_next (helper): one-step prediction for inference
//   - tt_predict_value (helper): value-only prediction at a given position

///|
/// Trajectory Transformer token embeddings. Four modality projections
/// (state, action, reward, done) + a timestep embedding lookup. All
/// five add into the same d_model vec.
pub struct TTEmbeddings {
  state_dim : Int
  action_dim : Int
  d_model : Int
  max_timestep : Int
  state_w : Array[Array[Float]]
  action_w : Array[Array[Float]]
  reward_w : Array[Array[Float]]
  done_w : Array[Array[Float]]
  timestep_emb : Array[Array[Float]]
}

///|
/// Build fresh TTEmbeddings.
///   - state_w: (d_model × state_dim), Xavier-normal scaled by sqrt(2/state_dim)
///   - action_w: (d_model × action_dim), Xavier-normal scaled by sqrt(2/action_dim)
///   - reward_w: (d_model × 1), Xavier-normal scaled by sqrt(2/1)
///   - done_w: (d_model × 1), Xavier-normal scaled by sqrt(2/1)
///   - timestep_emb: (max_timestep × d_model), std = 0.02
pub fn TTEmbeddings::new(
  state_dim : Int,
  action_dim : Int,
  d_model : Int,
  max_timestep : Int,
  seed : UInt64,
) -> TTEmbeddings {
  let rng1 = Xoshiro::from_state(seed, seed + 1UL, seed + 2UL, seed + 3UL)
  let std_state = sqrtf(2.0F / Float::from_int(state_dim))
  let state_w = xavier_normal(d_model, state_dim, std_state, rng1)
  let rng2 = Xoshiro::from_state(seed + 4UL, seed + 5UL, seed + 6UL, seed + 7UL)
  let std_action = sqrtf(2.0F / Float::from_int(action_dim))
  let action_w = xavier_normal(d_model, action_dim, std_action, rng2)
  let rng3 = Xoshiro::from_state(seed + 8UL, seed + 9UL, seed + 10UL, seed + 11UL)
  let std_scalar = sqrtf(2.0F / 1.0F)
  let reward_w = xavier_normal(d_model, 1, std_scalar, rng3)
  let rng4 = Xoshiro::from_state(seed + 12UL, seed + 13UL, seed + 14UL, seed + 15UL)
  let done_w = xavier_normal(d_model, 1, std_scalar, rng4)
  let rng5 = Xoshiro::from_state(seed + 16UL, seed + 17UL, seed + 18UL, seed + 19UL)
  let std_ts = 0.02F
  let timestep_emb = xavier_normal(max_timestep, d_model, std_ts, rng5)
  {
    state_dim,
    action_dim,
    d_model,
    max_timestep,
    state_w,
    action_w,
    reward_w,
    done_w,
    timestep_emb,
  }
}

///|
/// Embed a single TT trajectory token. All four modality inputs are
/// vector projections; `timestep_t` is an Int in `[0, max_timestep)`.
/// Returns Array[Float] of length d_model.
pub fn tt_embed_single_token(
  emb : TTEmbeddings,
  state_t : Array[Float],
  action_t : Array[Float],
  reward_t : Float,
  done_t : Float,
  timestep_t : Int,
) -> Array[Float] {
  let state_proj : Array[Float] = Array::make(emb.d_model, 0.0F)
  for i in 0..<emb.d_model {
    let mut s = 0.0F
    for k in 0..<emb.state_dim {
      s = s + emb.state_w[i][k] * state_t[k]
    }
    state_proj[i] = s
  }
  let action_proj : Array[Float] = Array::make(emb.d_model, 0.0F)
  for i in 0..<emb.d_model {
    let mut s = 0.0F
    for k in 0..<emb.action_dim {
      s = s + emb.action_w[i][k] * action_t[k]
    }
    action_proj[i] = s
  }
  let reward_proj : Array[Float] = Array::make(emb.d_model, 0.0F)
  for i in 0..<emb.d_model {
    reward_proj[i] = emb.reward_w[i][0] * reward_t
  }
  let done_proj : Array[Float] = Array::make(emb.d_model, 0.0F)
  for i in 0..<emb.d_model {
    done_proj[i] = emb.done_w[i][0] * done_t
  }
  let token : Array[Float] = Array::make(emb.d_model, 0.0F)
  for k in 0..<emb.d_model {
    token[k] = state_proj[k] + action_proj[k] + reward_proj[k] +
      done_proj[k] + emb.timestep_emb[timestep_t][k]
  }
  token
}

///|
/// Embed a full trajectory. `state_seq` flat `[seq_len × state_dim]`,
/// `action_seq` flat `[seq_len × action_dim]`, `reward_seq` flat
/// `[seq_len]`, `done_seq` flat `[seq_len]`, `timesteps` length
/// `seq_len`. Returns flat `[seq_len × d_model]` token sequence.
pub fn tt_embed_trajectory(
  emb : TTEmbeddings,
  state_seq : Array[Float],
  action_seq : Array[Float],
  reward_seq : Array[Float],
  done_seq : Array[Float],
  timesteps : Array[Int],
  seq_len : Int,
) -> Array[Float] {
  let tokens : Array[Float] = Array::make(seq_len * emb.d_model, 0.0F)
  for t in 0..<seq_len {
    let state_off = t * emb.state_dim
    let action_off = t * emb.action_dim
    let state_t : Array[Float] = Array::make(emb.state_dim, 0.0F)
    let action_t : Array[Float] = Array::make(emb.action_dim, 0.0F)
    for k in 0..<emb.state_dim {
      state_t[k] = state_seq[state_off + k]
    }
    for k in 0..<emb.action_dim {
      action_t[k] = action_seq[action_off + k]
    }
    let token = tt_embed_single_token(
      emb, state_t, action_t, reward_seq[t], done_seq[t], timesteps[t],
    )
    let tok_off = t * emb.d_model
    for k in 0..<emb.d_model {
      tokens[tok_off + k] = token[k]
    }
  }
  tokens
}

///|
/// Trajectory Transformer model. Embeddings + GTrXLBlock backbone + 4
/// prediction heads (next_state, reward, done, value). All four heads
/// are Linear with zero bias.
pub struct TrajectoryTransformer {
  state_dim : Int
  action_dim : Int
  d_model : Int
  d_ff : Int
  max_timestep : Int
  embeddings : TTEmbeddings
  backbone : GTrXLBlock
  state_w : Array[Array[Float]]
  state_b : Array[Float]
  reward_w : Array[Array[Float]]
  reward_b : Float
  done_w : Array[Array[Float]]
  done_b : Float
  value_w : Array[Array[Float]]
  value_b : Float
}

///|
/// Build fresh TrajectoryTransformer. 4 heads, each `Linear(d_model → out)`
/// with std = sqrt(2/d_model).
pub fn TrajectoryTransformer::new(
  state_dim : Int,
  action_dim : Int,
  d_model : Int,
  d_ff : Int,
  max_timestep : Int,
  seed : UInt64,
) -> TrajectoryTransformer {
  let embeddings = TTEmbeddings::new(
    state_dim, action_dim, d_model, max_timestep, seed,
  )
  let backbone = GTrXLBlock::new(d_model, d_ff, seed + 20UL)
  let rng1 = Xoshiro::from_state(seed + 24UL, seed + 25UL, seed + 26UL, seed + 27UL)
  let std_h = sqrtf(2.0F / Float::from_int(d_model))
  let state_w = xavier_normal(state_dim, d_model, std_h, rng1)
  let state_b : Array[Float] = Array::make(state_dim, 0.0F)
  let rng2 = Xoshiro::from_state(seed + 28UL, seed + 29UL, seed + 30UL, seed + 31UL)
  let reward_w = xavier_normal(1, d_model, std_h, rng2)
  let rng3 = Xoshiro::from_state(seed + 32UL, seed + 33UL, seed + 34UL, seed + 35UL)
  let done_w = xavier_normal(1, d_model, std_h, rng3)
  let rng4 = Xoshiro::from_state(seed + 36UL, seed + 37UL, seed + 38UL, seed + 39UL)
  let value_w = xavier_normal(1, d_model, std_h, rng4)
  {
    state_dim,
    action_dim,
    d_model,
    d_ff,
    max_timestep,
    embeddings,
    backbone,
    state_w,
    state_b,
    reward_w,
    reward_b: 0.0F,
    done_w,
    done_b: 0.0F,
    value_w,
    value_b: 0.0F,
  }
}

///|
/// Full Trajectory Transformer forward pass. Returns:
///   - pred_next_state_seq: [seq_len × state_dim]
///   - pred_reward_seq:     [seq_len]
///   - pred_done_seq:       [seq_len]
///   - pred_value_seq:      [seq_len]
///   - block_cache:         GTrXLBlockCache for future BPTT
pub fn tt_forward(
  tt : TrajectoryTransformer,
  state_seq : Array[Float],
  action_seq : Array[Float],
  reward_seq : Array[Float],
  done_seq : Array[Float],
  timesteps : Array[Int],
  seq_len : Int,
) -> (
  Array[Float],
  Array[Float],
  Array[Float],
  Array[Float],
  GTrXLBlockCache,
) {
  let tokens = tt_embed_trajectory(
    tt.embeddings, state_seq, action_seq, reward_seq, done_seq,
    timesteps, seq_len,
  )
  let (hidden_seq, cache) = gtrxl_block_seq_forward(
    tt.backbone, tokens, seq_len,
  )
  // Head projections.
  let pred_next_state_seq : Array[Float] = Array::make(
    seq_len * tt.state_dim, 0.0F,
  )
  let pred_reward_seq : Array[Float] = Array::make(seq_len, 0.0F)
  let pred_done_seq : Array[Float] = Array::make(seq_len, 0.0F)
  let pred_value_seq : Array[Float] = Array::make(seq_len, 0.0F)
  for t in 0..<seq_len {
    let off = t * tt.d_model
    let st_off = t * tt.state_dim
    for i in 0..<tt.state_dim {
      let mut s = tt.state_b[i]
      for k in 0..<tt.d_model {
        s = s + tt.state_w[i][k] * hidden_seq[off + k]
      }
      pred_next_state_seq[st_off + i] = s
    }
    let mut r = tt.reward_b
    let mut d = tt.done_b
    let mut v = tt.value_b
    for k in 0..<tt.d_model {
      let h = hidden_seq[off + k]
      r = r + tt.reward_w[0][k] * h
      d = d + tt.done_w[0][k] * h
      v = v + tt.value_w[0][k] * h
    }
    pred_reward_seq[t] = r
    pred_done_seq[t] = d
    pred_value_seq[t] = v
  }
  (pred_next_state_seq, pred_reward_seq, pred_done_seq, pred_value_seq, cache)
}

///|
/// One-step prediction helper. Returns `(next_state, reward, done,
/// value)` for the position `seq_len - 1` (the last token).
pub fn tt_predict_next(
  tt : TrajectoryTransformer,
  state_seq : Array[Float],
  action_seq : Array[Float],
  reward_seq : Array[Float],
  done_seq : Array[Float],
  timesteps : Array[Int],
  seq_len : Int,
) -> (Array[Float], Float, Float, Float) {
  let (ns_seq, r_seq, d_seq, v_seq, _) = tt_forward(
    tt, state_seq, action_seq, reward_seq, done_seq, timesteps, seq_len,
  )
  let off = (seq_len - 1) * tt.state_dim
  let next_state : Array[Float] = Array::make(tt.state_dim, 0.0F)
  for i in 0..<tt.state_dim {
    next_state[i] = ns_seq[off + i]
  }
  (next_state, r_seq[seq_len - 1], d_seq[seq_len - 1], v_seq[seq_len - 1])
}

///|
/// Value-only prediction at the last position.
pub fn tt_predict_value(
  tt : TrajectoryTransformer,
  state_seq : Array[Float],
  action_seq : Array[Float],
  reward_seq : Array[Float],
  done_seq : Array[Float],
  timesteps : Array[Int],
  seq_len : Int,
) -> Float {
  let (_, _, _, v_seq, _) = tt_forward(
    tt, state_seq, action_seq, reward_seq, done_seq, timesteps, seq_len,
  )
  v_seq[seq_len - 1]
}