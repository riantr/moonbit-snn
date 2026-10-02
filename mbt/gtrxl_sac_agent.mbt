// gtrxl_sac_agent.mbt — SAC_GTrXL agent: stochastic recurrent SAC with
// GTrXL block memory for partially-observable continuous-control POMDPs
// (v0.75.0).
//
// Wires:
//   - GTrXLSACActor (stochastic actor, v0.75.0)
//   - twin GTrXLQNetworkContinuous critics (Q-networks, v0.74.0)
//   - 5 Polyak-averaged target networks (actor + 2 critics + 3 targets)
//   - automatic alpha (entropy temperature) via `log_alpha` parameter
//     and `target_entropy = -action_dim` default
//
// Scope of v0.75.0:
//   - SAC_GTrXL struct + constructor (6 distinct seeds)
//   - sac_gtrxl_act (stochastic forward, returns action + block_cache + log_prob)
//   - sac_gtrxl_compute_td_target_seq (twin-critic min, no log-prob needed)
//   - sac_gtrxl_soft_update (Polyak averaging on all 5 targets — each
//     network has MLP w1/b1/w_mean/b_mean/w_log_std/b_log_std + GTrXL
//     block (6 tensors), so 12 tensors per target, 60 total across
//     actor + 2 critics)
//   - sac_gtrxl_update_alpha (gradient step on log_alpha toward target_entropy)
//
// BPTT-driven actor + critic update + SAC soft actor gradient (with
// entropy bonus) are deferred — parallel to Batch F's BPTT follow-up.

///|
/// SAC_GTrXL agent: stochastic actor + twin recurrent critics + 5
/// Polyak-averaged target networks + automatic alpha.
pub(all) struct SAC_GTrXL {
  actor : GTrXLSACActor
  critic1 : GTrXLQNetworkContinuous
  critic2 : GTrXLQNetworkContinuous
  actor_target : GTrXLSACActor
  critic1_target : GTrXLQNetworkContinuous
  critic2_target : GTrXLQNetworkContinuous
  gamma : Float
  mut tau : Float
  d_model : Int
  d_ff : Int
  mut log_alpha : Float
  target_entropy : Float
}

///|
/// Build a fresh SAC_GTrXL. 6 distinct seeds so Polyak targets start
/// far from source networks (they get soft-updated into place).
/// `initial_alpha = 1.0` (log_alpha = 0).
pub fn SAC_GTrXL::new(
  state_dim : Int,
  action_dim : Int,
  d_model : Int,
  d_ff : Int,
  action_low : Float,
  action_high : Float,
  gamma : Float,
  tau : Float,
  seed : UInt64,
) -> SAC_GTrXL {
  let actor : GTrXLSACActor = GTrXLSACActor::new(
    state_dim, action_dim, d_model, d_ff, action_low, action_high, seed,
  )
  let critic1 : GTrXLQNetworkContinuous = GTrXLQNetworkContinuous::new(
    state_dim, action_dim, d_model, d_ff, seed + 1UL,
  )
  let critic2 : GTrXLQNetworkContinuous = GTrXLQNetworkContinuous::new(
    state_dim, action_dim, d_model, d_ff, seed + 2UL,
  )
  let actor_target : GTrXLSACActor = GTrXLSACActor::new(
    state_dim, action_dim, d_model, d_ff, action_low, action_high, seed + 100UL,
  )
  let critic1_target : GTrXLQNetworkContinuous = GTrXLQNetworkContinuous::new(
    state_dim, action_dim, d_model, d_ff, seed + 101UL,
  )
  let critic2_target : GTrXLQNetworkContinuous = GTrXLQNetworkContinuous::new(
    state_dim, action_dim, d_model, d_ff, seed + 102UL,
  )
  {
    actor,
    critic1,
    critic2,
    actor_target,
    critic1_target,
    critic2_target,
    gamma,
    tau,
    d_model,
    d_ff,
    log_alpha : 0.0F,
    target_entropy : -Float::from_int(action_dim),
  }
}

///|
/// Sample a stochastic action for the current observation. Returns
/// `(action, block_cache, log_prob)`. Used at training time (with
/// exploration noise baked into the Gaussian sampling).
pub fn sac_gtrxl_act(
  agent : SAC_GTrXL,
  obs : Array[Float],
  rng : Xoshiro,
) -> (Array[Float], GTrXLTokenCache, Float) {
  let (a, log_prob, cache) = gtrxl_sac_actor_step(agent.actor, obs, rng)
  (a, cache, log_prob)
}

///|
/// Compute the 1-step twin-critic TD target for a T-step sequence.
/// target_t = r_t + γ(1 - d_t) * min(q1_target(next_obs_t, next_act_t),
///                                 q2_target(next_obs_t, next_act_t))
/// `next_act_seq` should be the stochastic action sample from the
/// target actor (caller computes via `gtrxl_sac_select_action` on
/// `actor_target`).
pub fn sac_gtrxl_compute_td_target_seq(
  agent : SAC_GTrXL,
  next_obs_seq : Array[Float],
  next_act_seq : Array[Float],
  reward_seq : Array[Float],
  done_seq : Array[Float],
  seq_len : Int,
) -> Array[Float] {
  let (q1_next, _) = gtrxl_qnetwork_continuous_seq_forward(
    agent.critic1_target, next_obs_seq, next_act_seq, seq_len,
  )
  let (q2_next, _) = gtrxl_qnetwork_continuous_seq_forward(
    agent.critic2_target, next_obs_seq, next_act_seq, seq_len,
  )
  let target : Array[Float] = Array::make(seq_len, 0.0F)
  for t in 0..<seq_len {
    let q_next = if q1_next[t] < q2_next[t] {
      q1_next[t]
    } else {
      q2_next[t]
    }
    target[t] = reward_seq[t] + agent.gamma * (1.0F - done_seq[t]) * q_next
  }
  target
}

///|
/// Polyak averaging for GTrXLSACActor. target <- (1 - tau) * target +
/// tau * source. Operates on all MLP weights + biases + GTrXL block
/// (ffn_w1, ffn_b1, ffn_w2, ffn_b2, ffn_gate_w, ffn_gate_b).
fn sac_gtrxl_actor_soft_update(
  target : GTrXLSACActor,
  source : GTrXLSACActor,
  tau : Float,
) -> Unit {
  let one_minus = 1.0F - tau
  // MLP w1
  for i in 0..<target.mlp_w1.length() {
    for k in 0..<target.mlp_w1[i].length() {
      target.mlp_w1[i][k] = one_minus * target.mlp_w1[i][k] +
        tau * source.mlp_w1[i][k]
    }
  }
  // MLP b1
  for i in 0..<target.mlp_b1.length() {
    target.mlp_b1[i] = one_minus * target.mlp_b1[i] + tau * source.mlp_b1[i]
  }
  // MLP w_mean / b_mean
  for i in 0..<target.mlp_w_mean.length() {
    for k in 0..<target.mlp_w_mean[i].length() {
      target.mlp_w_mean[i][k] = one_minus * target.mlp_w_mean[i][k] +
        tau * source.mlp_w_mean[i][k]
    }
  }
  for i in 0..<target.mlp_b_mean.length() {
    target.mlp_b_mean[i] = one_minus * target.mlp_b_mean[i] +
      tau * source.mlp_b_mean[i]
  }
  // MLP w_log_std / b_log_std
  for i in 0..<target.mlp_w_log_std.length() {
    for k in 0..<target.mlp_w_log_std[i].length() {
      target.mlp_w_log_std[i][k] = one_minus * target.mlp_w_log_std[i][k] +
        tau * source.mlp_w_log_std[i][k]
    }
  }
  for i in 0..<target.mlp_b_log_std.length() {
    target.mlp_b_log_std[i] = one_minus * target.mlp_b_log_std[i] +
      tau * source.mlp_b_log_std[i]
  }
  // GTrXL block: ffn_w1, ffn_b1, ffn_w2, ffn_b2, ffn_gate_w, ffn_gate_b
  let target_block = target.block
  let source_block = source.block
  for i in 0..<target_block.ffn_w1.length() {
    for k in 0..<target_block.ffn_w1[i].length() {
      target_block.ffn_w1[i][k] = one_minus * target_block.ffn_w1[i][k] +
        tau * source_block.ffn_w1[i][k]
      target_block.ffn_w2[i][k] = one_minus * target_block.ffn_w2[i][k] +
        tau * source_block.ffn_w2[i][k]
      target_block.ffn_gate_w[i][k] = one_minus * target_block.ffn_gate_w[i][k] +
        tau * source_block.ffn_gate_w[i][k]
    }
  }
  for i in 0..<target_block.ffn_b1.length() {
    target_block.ffn_b1[i] = one_minus * target_block.ffn_b1[i] +
      tau * source_block.ffn_b1[i]
    target_block.ffn_b2[i] = one_minus * target_block.ffn_b2[i] +
      tau * source_block.ffn_b2[i]
    target_block.ffn_gate_b[i] = one_minus * target_block.ffn_gate_b[i] +
      tau * source_block.ffn_gate_b[i]
  }
}

///|
/// Soft-update all 3 target networks (actor_target, critic1_target,
/// critic2_target) toward source via Polyak averaging.
pub fn sac_gtrxl_soft_update(agent : SAC_GTrXL) -> Unit {
  sac_gtrxl_actor_soft_update(agent.actor_target, agent.actor, agent.tau)
  gtrxl_qnetwork_continuous_soft_update(agent.critic1_target, agent.critic1, agent.tau)
  gtrxl_qnetwork_continuous_soft_update(agent.critic2_target, agent.critic2, agent.tau)
}

///|
/// Auto-alpha gradient step. Updates `log_alpha` toward
/// `target_entropy` (typically `-action_dim`). Caller passes
/// `mean_log_prob` = mean of per-timestep log π across the T-step
/// window. Loss: `alpha * (-log_prob - target_entropy)`.
/// Gradient w.r.t. log_alpha: `-(mean_log_prob + target_entropy)`.
pub fn sac_gtrxl_update_alpha(
  agent : SAC_GTrXL,
  mean_log_prob : Float,
  lr : Float,
) -> Unit {
  let target = agent.target_entropy
  // d/d(log_alpha) [alpha * (-log_prob - target)] = -log_prob - target
  let grad_log_alpha = -(mean_log_prob + target)
  // log_alpha <- log_alpha - lr * grad
  agent.log_alpha = agent.log_alpha - lr * grad_log_alpha
}

///|
/// Get the current alpha = exp(log_alpha). Used by callers in
/// actor/critic updates.
pub fn sac_gtrxl_get_alpha(agent : SAC_GTrXL) -> Float {
  expf(agent.log_alpha)
}
