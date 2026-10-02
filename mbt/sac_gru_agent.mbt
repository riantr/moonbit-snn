// sac_gru_agent.mbt — SAC_GRU agent: stochastic recurrent SAC for
// partially-observable continuous-control POMDPs (v0.70.0).
//
// Wires:
//   - GRUSACActor (stochastic actor, v0.68.0)
//   - twin GRUQNetworkContinuous critics (Q-networks, Batch C v0.59.0)
//   - 5 Polyak-averaged target networks (actor + 2 critics + 3 targets)
//   - automatic alpha (entropy temperature) via `log_alpha` parameter
//     and `target_entropy = -action_dim` default
//
// Scope of v0.70.0:
//   - SAC_GRU struct + constructor (6 distinct seeds)
//   - select_action (stochastic forward, returns action + log_prob + new hidden)
//   - compute_td_target_seq (twin-critic min, no log-prob needed)
//   - soft_update (Polyak averaging on all 5 targets — each MLP has 2
//     weight matrices + 2 biases; each GRU has 3 gate matrices × (W+b) =
//     6 tensors; so 8 tensors per target, 40 total across actor + 2 critics)
//   - update_alpha (gradient step on log_alpha toward target_entropy)
//
// BPTT-driven actor + critic update + SAC soft actor gradient (with
// entropy bonus) are deferred — parallel to Batch D's BPTT follow-up.

///|
/// SAC_GRU agent: stochastic actor + twin recurrent critics + 5
/// Polyak-averaged target networks + automatic alpha.
pub(all) struct SAC_GRU {
  actor : GRUSACActor
  critic1 : GRUQNetworkContinuous
  critic2 : GRUQNetworkContinuous
  actor_target : GRUSACActor
  critic1_target : GRUQNetworkContinuous
  critic2_target : GRUQNetworkContinuous
  gamma : Float
  mut tau : Float
  hidden : Int
  mut log_alpha : Float
  target_entropy : Float
}

///|
/// Build a fresh SAC_GRU. 6 distinct seeds so Polyak targets start
/// far from source networks (they get soft-updated into place).
/// `initial_alpha = 1.0` (log_alpha = 0).
pub fn SAC_GRU::new(
  state_dim : Int,
  action_dim : Int,
  hidden : Int,
  action_low : Float,
  action_high : Float,
  gamma : Float,
  tau : Float,
  seed : UInt64,
) -> SAC_GRU {
  let actor : GRUSACActor = GRUSACActor::new(
    state_dim, action_dim, hidden, action_low, action_high, seed,
  )
  let critic1 : GRUQNetworkContinuous = GRUQNetworkContinuous::new(
    state_dim, action_dim, hidden, seed + 1UL,
  )
  let critic2 : GRUQNetworkContinuous = GRUQNetworkContinuous::new(
    state_dim, action_dim, hidden, seed + 2UL,
  )
  let actor_target : GRUSACActor = GRUSACActor::new(
    state_dim, action_dim, hidden, action_low, action_high, seed + 100UL,
  )
  let critic1_target : GRUQNetworkContinuous = GRUQNetworkContinuous::new(
    state_dim, action_dim, hidden, seed + 101UL,
  )
  let critic2_target : GRUQNetworkContinuous = GRUQNetworkContinuous::new(
    state_dim, action_dim, hidden, seed + 102UL,
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
    hidden,
    log_alpha : 0.0F,
    target_entropy : -Float::from_int(action_dim),
  }
}

///|
/// Sample a stochastic action for the current observation + recurrent
/// hidden. Returns `(action, new_hidden, log_prob)`. Used at training
/// time (with exploration noise baked into the Gaussian sampling).
pub fn sac_gru_act(
  agent : SAC_GRU,
  obs : Array[Float],
  hidden : Array[Float],
  rng : Xoshiro,
) -> (Array[Float], Array[Float], Float) {
  let (a, log_prob, hidden_next) = sac_gru_actor_step(
    agent.actor, obs, hidden, rng,
  )
  (a, hidden_next, log_prob)
}

///|
/// Compute the 1-step twin-critic TD target for a T-step sequence.
/// target_t = r_t + γ(1 - d_t) * min(q1_target(next_obs_t, next_act_t),
///                                 q2_target(next_obs_t, next_act_t))
/// `next_act_seq` should be the stochastic action sample from the
/// target actor (caller computes via
/// `sac_gru_target_actor_seq` on `actor_target`).
pub fn sac_gru_compute_td_target_seq(
  agent : SAC_GRU,
  next_obs_seq : Array[Float],
  next_act_seq : Array[Float],
  reward_seq : Array[Float],
  done_seq : Array[Float],
  seq_len : Int,
) -> Array[Float] {
  let hidden_init : Array[Float] = Array::make(agent.hidden, 0.0F)
  let (q1_next, _) = gru_qnetwork_continuous_seq_forward(
    agent.critic1_target, next_obs_seq, next_act_seq, seq_len, hidden_init,
  )
  let (q2_next, _) = gru_qnetwork_continuous_seq_forward(
    agent.critic2_target, next_obs_seq, next_act_seq, seq_len, hidden_init,
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
/// Polyak averaging for GRUSACActor. target <- (1 - tau) * target +
/// tau * source. Operates on all MLP weights + biases + GRU
/// sub-parameters (w_z, w_r, w_n + b_z, b_r, b_n).
fn sac_gru_actor_soft_update(
  target : GRUSACActor,
  source : GRUSACActor,
  tau : Float,
) -> Unit {
  let one_minus = 1.0F - tau
  for i in 0..<target.mlp_w1.length() {
    for k in 0..<target.mlp_w1[i].length() {
      target.mlp_w1[i][k] = one_minus * target.mlp_w1[i][k] +
        tau * source.mlp_w1[i][k]
    }
  }
  for i in 0..<target.mlp_b1.length() {
    target.mlp_b1[i] = one_minus * target.mlp_b1[i] + tau * source.mlp_b1[i]
  }
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
  let target_gru = target.gru
  let source_gru = source.gru
  for i in 0..<target_gru.d_h {
    let in_dim = target_gru.w_z[i].length()
    for k in 0..<in_dim {
      target_gru.w_z[i][k] = one_minus * target_gru.w_z[i][k] +
        tau * source_gru.w_z[i][k]
      target_gru.w_r[i][k] = one_minus * target_gru.w_r[i][k] +
        tau * source_gru.w_r[i][k]
      target_gru.w_n[i][k] = one_minus * target_gru.w_n[i][k] +
        tau * source_gru.w_n[i][k]
    }
    target_gru.b_z[i] = one_minus * target_gru.b_z[i] + tau * source_gru.b_z[i]
    target_gru.b_r[i] = one_minus * target_gru.b_r[i] + tau * source_gru.b_r[i]
    target_gru.b_n[i] = one_minus * target_gru.b_n[i] + tau * source_gru.b_n[i]
  }
}

///|
/// Soft-update all 3 target networks (actor_target, critic1_target,
/// critic2_target) toward source via Polyak averaging.
pub fn sac_gru_soft_update(agent : SAC_GRU) -> Unit {
  sac_gru_actor_soft_update(agent.actor_target, agent.actor, agent.tau)
  gru_qnetwork_continuous_soft_update(agent.critic1_target, agent.critic1, agent.tau)
  gru_qnetwork_continuous_soft_update(agent.critic2_target, agent.critic2, agent.tau)
}

///|
/// Auto-alpha gradient step. Updates `log_alpha` toward
/// `target_entropy` (typically `-action_dim`). Caller passes
/// `mean_log_prob` = mean of per-timestep log π across the T-step
/// window. Loss: `alpha * (-log_prob - target_entropy)`.
/// Gradient w.r.t. log_alpha: `-(mean_log_prob + target_entropy)`.
pub fn sac_gru_update_alpha(
  agent : SAC_GRU,
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
pub fn sac_gru_get_alpha(agent : SAC_GRU) -> Float {
  expf(agent.log_alpha)
}