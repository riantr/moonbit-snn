// gru_ddpg.mbt — DDPG agent with GRU actor + twin GRU critics for
// partially-observable continuous-control RL (v0.60.0).
//
// Wires the recurrent actor (`GRUDeterministicPolicy`) and twin
// recurrent critics (`GRUQNetworkContinuous`) into a single agent with
// 5 Polyak-averaged target networks (actor_target, critic1_target,
// critic2_target). The recurrent hidden state is threaded through the
// target twin-critic TD update: critic1/critic2 process the full T-step
// sequence with their respective hidden states, and the TD target
// uses `min(critic1_target(next_seq), critic2_target(next_seq))`.
//
// Scope of v0.60.0:
//   - DDPG_GRU struct + constructor
//   - select_action / select_action_seq (single-step + sequence
//     inference with Gaussian exploration noise)
//   - twin-critic TD forward pass (computes 1-step TD target; no
//     parameter update yet)
//   - soft_update (Polyak averaging across all 5 target networks,
//     including each MLP+GRU sub-parameter)
//
// BPTT-driven critic/actor update is deferred to a follow-up version
// (requires matvec_backward over T steps + gru_cell_backward over T
// steps + sign(ReLU_out) gate per timestep; significant incremental
// work that doesn't belong in a single version).

///|
/// DDPG agent with GRU-based actor + twin GRU-based critics + 5
/// Polyak-averaged target networks. All targets share the same
/// parameter shape as their corresponding source network.
pub(all) struct DDPG_GRU {
  actor : GRUDeterministicPolicy
  critic1 : GRUQNetworkContinuous
  critic2 : GRUQNetworkContinuous
  actor_target : GRUDeterministicPolicy
  critic1_target : GRUQNetworkContinuous
  critic2_target : GRUQNetworkContinuous
  gamma : Float
  mut tau : Float
  exploration_noise : Float
  hidden : Int
}

///|
/// Build a fresh DDPG_GRU agent. Each of the 6 networks (actor,
/// critic1, critic2, plus their targets) gets a distinct seed so the
/// Polyak-averaged targets start far from the source networks (they
/// will be soft-updated into place during training).
pub fn DDPG_GRU::new(
  state_dim : Int,
  action_dim : Int,
  hidden : Int,
  action_low : Float,
  action_high : Float,
  gamma : Float,
  tau : Float,
  exploration_noise : Float,
  seed : UInt64,
) -> DDPG_GRU {
  let actor : GRUDeterministicPolicy = GRUDeterministicPolicy::new(
    state_dim, action_dim, hidden, action_low, action_high, seed,
  )
  let critic1 : GRUQNetworkContinuous = GRUQNetworkContinuous::new(
    state_dim, action_dim, hidden, seed + 1UL,
  )
  let critic2 : GRUQNetworkContinuous = GRUQNetworkContinuous::new(
    state_dim, action_dim, hidden, seed + 2UL,
  )
  let actor_target : GRUDeterministicPolicy = GRUDeterministicPolicy::new(
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
    exploration_noise,
    hidden,
  }
}

///|
/// Single-step inference with Gaussian exploration noise (clipped to
/// `[action_low, action_high]` after adding noise). Returns the noisy
/// action and the next GRU hidden state. Used at training time;
/// `noise_std=0.0F` recovers the deterministic policy output.
pub fn ddpg_gru_select_action(
  agent : DDPG_GRU,
  obs : Array[Float],
  hidden : Array[Float],
  noise_std : Float,
  rng : Xoshiro,
) -> (Array[Float], Array[Float]) {
  let (action, hidden_next) = gru_deterministic_policy_step(agent.actor, obs, hidden)
  if noise_std <= 0.0F {
    return (action, hidden_next)
  }
  let n = action.length()
  let noisy : Array[Float] = Array::make(n, 0.0F)
  for i in 0..<n {
    let (z, _) = box_muller(rng)
    let noise = Float::from_double(z) * noise_std
    let mut v = action[i] + noise
    if v < agent.actor.action_low {
      v = agent.actor.action_low
    } else if v > agent.actor.action_high {
      v = agent.actor.action_high
    }
    noisy[i] = v
  }
  (noisy, hidden_next)
}

///|
/// Compute the 1-step twin-critic TD target for a T-step sequence.
/// For each timestep t, target_t = reward_t + gamma * (1 - done_t) *
/// min(q1_target(next_obs_t, next_act_t), q2_target(next_obs_t, next_act_t)).
/// `next_act_seq` is the deterministic-policy action produced from
/// `next_obs_seq` (caller computes via `gru_deterministic_policy_seq_forward`
/// on `actor_target`).
///
/// Returns the per-timestep TD target array of length `seq_len`.
/// (This is the forward-only TD target; the parameter-update step
/// is deferred to a follow-up.)
pub fn ddpg_gru_compute_td_target_seq(
  agent : DDPG_GRU,
  next_obs_seq : Array[Float],
  next_act_seq : Array[Float],
  reward_seq : Array[Float],
  done_seq : Array[Float],
  seq_len : Int,
) -> Array[Float] {
  let (q1_next_seq, _) = gru_qnetwork_continuous_seq_forward(
    agent.critic1_target, next_obs_seq, next_act_seq, seq_len,
    Array::make(agent.hidden, 0.0F),
  )
  let (q2_next_seq, _) = gru_qnetwork_continuous_seq_forward(
    agent.critic2_target, next_obs_seq, next_act_seq, seq_len,
    Array::make(agent.hidden, 0.0F),
  )
  let target : Array[Float] = Array::make(seq_len, 0.0F)
  for t in 0..<seq_len {
    let q_next = if q1_next_seq[t] < q2_next_seq[t] {
      q1_next_seq[t]
    } else {
      q2_next_seq[t]
    }
    target[t] = reward_seq[t] + agent.gamma * (1.0F - done_seq[t]) * q_next
  }
  target
}

///|
/// Polyak averaging for the GRUDeterministicPolicy: target <- (1 - tau)
/// * target + tau * source. Operates on the MLP weights + biases + GRU
/// sub-parameters (w_z, w_r, w_n, b_z, b_r, b_n). All elements are
/// updated element-wise.
fn gru_deterministic_policy_soft_update(
  target : GRUDeterministicPolicy,
  source : GRUDeterministicPolicy,
  tau : Float,
) -> Unit {
  let one_minus = 1.0F - tau
  // mlp_w1
  for i in 0..<target.mlp_w1.length() {
    for k in 0..<target.mlp_w1[i].length() {
      target.mlp_w1[i][k] = one_minus * target.mlp_w1[i][k] + tau * source.mlp_w1[i][k]
    }
  }
  // mlp_b1
  for i in 0..<target.mlp_b1.length() {
    target.mlp_b1[i] = one_minus * target.mlp_b1[i] + tau * source.mlp_b1[i]
  }
  // mlp_w2
  for i in 0..<target.mlp_w2.length() {
    for k in 0..<target.mlp_w2[i].length() {
      target.mlp_w2[i][k] = one_minus * target.mlp_w2[i][k] + tau * source.mlp_w2[i][k]
    }
  }
  // mlp_b2
  for i in 0..<target.mlp_b2.length() {
    target.mlp_b2[i] = one_minus * target.mlp_b2[i] + tau * source.mlp_b2[i]
  }
  // GRU sub-parameters (w_z, w_r, w_n, b_z, b_r, b_n).
  let target_gru = target.gru
  let source_gru = source.gru
  for i in 0..<target_gru.w_z.length() {
    for k in 0..<target_gru.w_z[i].length() {
      target_gru.w_z[i][k] = one_minus * target_gru.w_z[i][k] + tau * source_gru.w_z[i][k]
      target_gru.w_r[i][k] = one_minus * target_gru.w_r[i][k] + tau * source_gru.w_r[i][k]
      target_gru.w_n[i][k] = one_minus * target_gru.w_n[i][k] + tau * source_gru.w_n[i][k]
    }
    target_gru.b_z[i] = one_minus * target_gru.b_z[i] + tau * source_gru.b_z[i]
    target_gru.b_r[i] = one_minus * target_gru.b_r[i] + tau * source_gru.b_r[i]
    target_gru.b_n[i] = one_minus * target_gru.b_n[i] + tau * source_gru.b_n[i]
  }
}

///|
/// Polyak averaging for the GRUQNetworkContinuous. Same pattern as
/// `gru_deterministic_policy_soft_update` plus the scalar `mlp_b2`.
fn gru_qnetwork_continuous_soft_update(
  target : GRUQNetworkContinuous,
  source : GRUQNetworkContinuous,
  tau : Float,
) -> Unit {
  let one_minus = 1.0F - tau
  for i in 0..<target.mlp_w1.length() {
    for k in 0..<target.mlp_w1[i].length() {
      target.mlp_w1[i][k] = one_minus * target.mlp_w1[i][k] + tau * source.mlp_w1[i][k]
    }
  }
  for i in 0..<target.mlp_b1.length() {
    target.mlp_b1[i] = one_minus * target.mlp_b1[i] + tau * source.mlp_b1[i]
  }
  for i in 0..<target.mlp_w2.length() {
    for k in 0..<target.mlp_w2[i].length() {
      target.mlp_w2[i][k] = one_minus * target.mlp_w2[i][k] + tau * source.mlp_w2[i][k]
    }
  }
  target.mlp_b2 = one_minus * target.mlp_b2 + tau * source.mlp_b2
  let target_gru = target.gru
  let source_gru = source.gru
  for i in 0..<target_gru.w_z.length() {
    for k in 0..<target_gru.w_z[i].length() {
      target_gru.w_z[i][k] = one_minus * target_gru.w_z[i][k] + tau * source_gru.w_z[i][k]
      target_gru.w_r[i][k] = one_minus * target_gru.w_r[i][k] + tau * source_gru.w_r[i][k]
      target_gru.w_n[i][k] = one_minus * target_gru.w_n[i][k] + tau * source_gru.w_n[i][k]
    }
    target_gru.b_z[i] = one_minus * target_gru.b_z[i] + tau * source_gru.b_z[i]
    target_gru.b_r[i] = one_minus * target_gru.b_r[i] + tau * source_gru.b_r[i]
    target_gru.b_n[i] = one_minus * target_gru.b_n[i] + tau * source_gru.b_n[i]
  }
}

///|
/// Soft-update all 3 target networks (actor_target, critic1_target,
/// critic2_target) toward their respective source networks via Polyak
/// averaging with rate `agent.tau`. Mutates the target networks in
/// place (they share storage with the agent struct).
pub fn ddpg_gru_soft_update(agent : DDPG_GRU) -> Unit {
  gru_deterministic_policy_soft_update(agent.actor_target, agent.actor, agent.tau)
  gru_qnetwork_continuous_soft_update(agent.critic1_target, agent.critic1, agent.tau)
  gru_qnetwork_continuous_soft_update(agent.critic2_target, agent.critic2, agent.tau)
}
