// dt_embeddings.mbt — Decision Transformer primitive: tokenization of
// (rtg, state, action, timestep) trajectories into d_model-dimensional
// tokens (v0.76.0).
//
// Reference: Chen et al. 2021 "Decision Transformer: Reinforcement
// Learning through Sequence Modeling". The original DT uses GPT-style
// causal attention; here we substitute a GTrXL block (Parisotto et al.
// 2020) as the per-token recurrent memory primitive. This keeps DT in
// the same architecture family as the v0.72–v0.75 GTrXL primitives.
//
// Per-timestep token layout:
//   token_t = Linear_rtg(R_t) + Linear_state(s_t) + Linear_action(a_t) + Embed_timestep(t)
//
// All three modality projections (rtg_w, state_w, action_w) are linear
// without bias. The timestep embedding is a small lookup table
// (max_timestep slots) of size d_model each. The sum of the four terms
// becomes the input to the GTrXL block.
//
// Scope of v0.76.0:
//   - DTEmbeddings struct + constructor
//   - dt_embed_trajectory: stitch (rtg_seq, state_seq, action_seq,
//     timesteps) into a flat [seq_len × d_model] token sequence
//   - dt_embed_single_token: helper for inference (one token at a time,
//     no batch dim)
//   - Returns-to-go are NOT computed here — that's TrajectoryBuffer's
//     job (v0.78.0).

///|
/// Decision Transformer token embeddings. Three modality projections
/// (rtg, state, action) + a timestep embedding lookup. d_model is
/// shared across all four (each adds into the same d_model vec).
pub struct DTEmbeddings {
  state_dim : Int
  action_dim : Int
  d_model : Int
  max_timestep : Int
  rtg_w : Array[Array[Float]]
  state_w : Array[Array[Float]]
  action_w : Array[Array[Float]]
  timestep_emb : Array[Array[Float]]
}

///|
/// Build fresh DTEmbeddings.
///   - rtg_w: (d_model × 1), each row scaled by sqrtf(2 / 1) = sqrt(2)
///   - state_w: (d_model × state_dim), Xavier-normal scaled by sqrt(2 / state_dim)
///   - action_w: (d_model × action_dim), Xavier-normal scaled by sqrt(2 / action_dim)
///   - timestep_emb: (max_timestep × d_model), Xavier-normal scaled by 0.02 (small,
///     like original Transformer init for positional embeddings)
pub fn DTEmbeddings::new(
  state_dim : Int,
  action_dim : Int,
  d_model : Int,
  max_timestep : Int,
  seed : UInt64,
) -> DTEmbeddings {
  let rng1 = Xoshiro::from_state(seed, seed + 1UL, seed + 2UL, seed + 3UL)
  let std_rtg = sqrtf(2.0F / 1.0F)
  let rtg_w = xavier_normal(d_model, 1, std_rtg, rng1)
  let rng2 = Xoshiro::from_state(seed + 4UL, seed + 5UL, seed + 6UL, seed + 7UL)
  let std_state = sqrtf(2.0F / Float::from_int(state_dim))
  let state_w = xavier_normal(d_model, state_dim, std_state, rng2)
  let rng3 = Xoshiro::from_state(seed + 8UL, seed + 9UL, seed + 10UL, seed + 11UL)
  let std_action = sqrtf(2.0F / Float::from_int(action_dim))
  let action_w = xavier_normal(d_model, action_dim, std_action, rng3)
  let rng4 = Xoshiro::from_state(seed + 12UL, seed + 13UL, seed + 14UL, seed + 15UL)
  let std_ts = 0.02F
  let timestep_emb = xavier_normal(max_timestep, d_model, std_ts, rng4)
  // xavier_normal returns (rows, cols). We want (max_timestep × d_model).
  // If std_ts * sqrt(2 / fan_in) where fan_in = d_model is too small,
  // fall back to a hand-rolled init that produces max_timestep × d_model.
  // xavier_normal returns (rows × cols); we passed (max_timestep, d_model, std_ts, rng4),
  // so the result is already max_timestep rows × d_model cols. Good.
  {
    state_dim,
    action_dim,
    d_model,
    max_timestep,
    rtg_w,
    state_w,
    action_w,
    timestep_emb,
  }
}

///|
/// Helper: project a single scalar `x` (treated as 1-vec) through `w`
/// (shape d_model × 1) with optional bias (length 1). Returns
/// Array[Float] of length d_model.
fn dt_project_scalar(
  w : Array[Array[Float]],
  x : Float,
) -> Array[Float] {
  let out : Array[Float] = Array::make(w.length(), 0.0F)
  for i in 0..<w.length() {
    out[i] = w[i][0] * x
  }
  out
}

///|
/// Helper: project a vector `x` (length n) through `w` (shape d_model × n).
/// Returns Array[Float] of length d_model.
fn dt_project_vector(
  w : Array[Array[Float]],
  x : Array[Float],
) -> Array[Float] {
  let out : Array[Float] = Array::make(w.length(), 0.0F)
  for i in 0..<w.length() {
    let mut s = 0.0F
    for k in 0..<x.length() {
      s = s + w[i][k] * x[k]
    }
    out[i] = s
  }
  out
}

///|
/// Embed a single trajectory token. `rtg_t` is a scalar, `state_t`
/// length state_dim, `action_t` length action_dim, `timestep_t` an Int
/// in `[0, max_timestep)`. Returns an Array[Float] of length d_model
/// = Linear_rtg(rtg_t) + Linear_state(state_t) + Linear_action(action_t)
/// + timestep_emb[timestep_t].
pub fn dt_embed_single_token(
  emb : DTEmbeddings,
  rtg_t : Float,
  state_t : Array[Float],
  action_t : Array[Float],
  timestep_t : Int,
) -> Array[Float] {
  let rtg_proj = dt_project_scalar(emb.rtg_w, rtg_t)
  let state_proj = dt_project_vector(emb.state_w, state_t)
  let action_proj = dt_project_vector(emb.action_w, action_t)
  let token : Array[Float] = Array::make(emb.d_model, 0.0F)
  for k in 0..<emb.d_model {
    token[k] = rtg_proj[k] + state_proj[k] + action_proj[k] +
      emb.timestep_emb[timestep_t][k]
  }
  token
}

///|
/// Embed a full trajectory. `rtg_seq` is flat `[seq_len]`, `state_seq`
/// is flat `[seq_len × state_dim]`, `action_seq` is flat
/// `[seq_len × action_dim]`, `timesteps` is an Array[Int] of length
/// seq_len. Returns flat `[seq_len × d_model]` token sequence.
pub fn dt_embed_trajectory(
  emb : DTEmbeddings,
  rtg_seq : Array[Float],
  state_seq : Array[Float],
  action_seq : Array[Float],
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
    let timestep_t = timesteps[t]
    let token = dt_embed_single_token(
      emb, rtg_seq[t], state_t, action_t, timestep_t,
    )
    let tok_off = t * emb.d_model
    for k in 0..<emb.d_model {
      tokens[tok_off + k] = token[k]
    }
  }
  tokens
}