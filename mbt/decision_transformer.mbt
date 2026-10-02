// decision_transformer.mbt — Decision Transformer model: full forward
// (tokenize trajectory → GTrXL block → action prediction head)
// (v0.77.0).
//
// Architecture:
//   tokens_t  =  DTEmbeddings(rtg_t, state_t, action_t, timestep_t)    ∈ R^{d_model}
//   hidden_t  =  GTrXLBlock(tokens_t)                       ∈ R^{d_model}
//   a_pre_t   =  W_action · hidden_t + b_action             ∈ R^{action_dim}
//
// Loss: MSE between a_pre_t and the actual action_t. The MSE is
// computed by the trainer (v0.79.0); this file only ships forward.
//
// Reference: Chen et al. 2021 "Decision Transformer: Reinforcement
// Learning through Sequence Modeling". The original DT uses GPT-style
// causal attention; here we substitute a GTrXL block as the per-token
// recurrent memory primitive (the GTrXL block's gated residual update
// provides token-to-token carry).

///|
/// Decision Transformer model. Combines DTEmbeddings + GTrXLBlock +
/// action prediction head. The action head at the *last* timestep is
/// the policy output (the predicted next action for the desired return);
/// earlier timesteps are also predicted (used as MSE targets during
/// training).
pub struct DecisionTransformer {
  state_dim : Int
  action_dim : Int
  d_model : Int
  d_ff : Int
  max_timestep : Int
  embeddings : DTEmbeddings
  backbone : GTrXLBlock
  action_w : Array[Array[Float]]
  action_b : Array[Float]
}

///|
/// Build fresh DecisionTransformer.
///   - embeddings: DTEmbeddings::new with seed
///   - backbone: GTrXLBlock::new(d_model, d_ff, seed + 4UL)
///   - action_w: (action_dim × d_model), Xavier-normal scaled by sqrt(2/d_model)
///   - action_b: zeros
pub fn DecisionTransformer::new(
  state_dim : Int,
  action_dim : Int,
  d_model : Int,
  d_ff : Int,
  max_timestep : Int,
  seed : UInt64,
) -> DecisionTransformer {
  let embeddings = DTEmbeddings::new(
    state_dim, action_dim, d_model, max_timestep, seed,
  )
  let backbone = GTrXLBlock::new(d_model, d_ff, seed + 16UL)
  let rng = Xoshiro::from_state(seed + 20UL, seed + 21UL, seed + 22UL, seed + 23UL)
  let std_head = sqrtf(2.0F / Float::from_int(d_model))
  let action_w = xavier_normal(action_dim, d_model, std_head, rng)
  let action_b : Array[Float] = Array::make(action_dim, 0.0F)
  {
    state_dim,
    action_dim,
    d_model,
    d_ff,
    max_timestep,
    embeddings,
    backbone,
    action_w,
    action_b,
  }
}

///|
/// Full Decision Transformer forward. Returns
/// `(predicted_action_seq, block_cache)` where:
///   - `predicted_action_seq` is flat `[seq_len × action_dim]`
///   - `block_cache` is the GTrXLBlockCache for future BPTT
pub fn dt_forward(
  dt : DecisionTransformer,
  rtg_seq : Array[Float],
  state_seq : Array[Float],
  action_seq : Array[Float],
  timesteps : Array[Int],
  seq_len : Int,
) -> (Array[Float], GTrXLBlockCache) {
  // Tokenize the full trajectory.
  let tokens = dt_embed_trajectory(
    dt.embeddings, rtg_seq, state_seq, action_seq, timesteps, seq_len,
  )
  // Run through the GTrXL backbone.
  let (hidden_seq, cache) = gtrxl_block_seq_forward(
    dt.backbone, tokens, seq_len,
  )
  // Action prediction head.
  let predicted_action_seq : Array[Float] = Array::make(
    seq_len * dt.action_dim, 0.0F,
  )
  for t in 0..<seq_len {
    let off = t * dt.d_model
    let act_off = t * dt.action_dim
    for i in 0..<dt.action_dim {
      let mut s = dt.action_b[i]
      for k in 0..<dt.d_model {
        s = s + dt.action_w[i][k] * hidden_seq[off + k]
      }
      predicted_action_seq[act_off + i] = s
    }
  }
  (predicted_action_seq, cache)
}

///|
/// Inference helper: condition on (rtg_seq, state_seq, action_seq)
/// history and return the predicted next action (length action_dim) for
/// the last timestep. The caller passes a context of length seq_len and
/// gets back the action predicted at position seq_len - 1.
pub fn dt_predict_next_action(
  dt : DecisionTransformer,
  rtg_seq : Array[Float],
  state_seq : Array[Float],
  action_seq : Array[Float],
  timesteps : Array[Int],
  seq_len : Int,
) -> Array[Float] {
  let (predicted_action_seq, _) = dt_forward(
    dt, rtg_seq, state_seq, action_seq, timesteps, seq_len,
  )
  let off = (seq_len - 1) * dt.action_dim
  let action : Array[Float] = Array::make(dt.action_dim, 0.0F)
  for i in 0..<dt.action_dim {
    action[i] = predicted_action_seq[off + i]
  }
  action
}