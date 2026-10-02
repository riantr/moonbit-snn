// gtrxl_ddpg.mbt — DDPG agent with GTrXL actor + twin GTrXL critics for
// partially-observable continuous-control RL (v0.74.0).
//
// Architecture: GTrXL actor + twin GTrXL critics + 5 Polyak-averaged
// target networks. The recurrent memory is the gated-residual update
// inside each GTrXL block (carried token-to-token via the y → x
// residual).
//
// Scope of v0.74.0:
//   - GTrXL_DDPG struct + constructor (6 distinct seeds)
//   - gtrxl_ddpg_select_action (single-step inference + Gaussian noise)
//   - gtrxl_ddpg_compute_td_target_seq (twin-critic forward, no param
//     update)
//   - gtrxl_ddpg_soft_update (Polyak averaging across all 5 targets:
//     each network has 2 MLP weight matrices + 2 biases + GTrXL block
//     (7 tensors: ffn_w1, ffn_b1, ffn_w2, ffn_b2, ffn_gate_w,
//     ffn_gate_b + scalar mlp_b2 for critics) = 9 tensors per network,
//     45 total)
//
// BPTT-driven parameter update deferred — see PR for rationale.

///|
/// DDPG agent with GTrXL actor + twin GTrXL critics.
pub(all) struct GTrXL_DDPG {
  actor : GTrXLDeterministicPolicy
  critic1 : GTrXLQNetworkContinuous
  critic2 : GTrXLQNetworkContinuous
  actor_target : GTrXLDeterministicPolicy
  critic1_target : GTrXLQNetworkContinuous
  critic2_target : GTrXLQNetworkContinuous
  gamma : Float
  mut tau : Float
  exploration_noise : Float
  d_model : Int
  d_ff : Int
}

///|
/// Build fresh GTrXL_DDPG. 6 distinct seeds so Polyak targets start far
/// from source networks (they get soft-updated into place).
pub fn GTrXL_DDPG::new(
  state_dim : Int,
  action_dim : Int,
  d_model : Int,
  d_ff : Int,
  action_low : Float,
  action_high : Float,
  gamma : Float,
  tau : Float,
  exploration_noise : Float,
  seed : UInt64,
) -> GTrXL_DDPG {
  let actor : GTrXLDeterministicPolicy = GTrXLDeterministicPolicy::new(
    state_dim, action_dim, d_model, d_ff, action_low, action_high, seed,
  )
  let critic1 : GTrXLQNetworkContinuous = GTrXLQNetworkContinuous::new(
    state_dim, action_dim, d_model, d_ff, seed + 1UL,
  )
  let critic2 : GTrXLQNetworkContinuous = GTrXLQNetworkContinuous::new(
    state_dim, action_dim, d_model, d_ff, seed + 2UL,
  )
  let actor_target : GTrXLDeterministicPolicy = GTrXLDeterministicPolicy::new(
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
    exploration_noise,
    d_model,
    d_ff,
  }
}

///|
/// Single-step inference with Gaussian noise (clipped to [action_low,
/// action_high]). `noise_std=0.0F` recovers deterministic output.
pub fn gtrxl_ddpg_select_action(
  agent : GTrXL_DDPG,
  obs : Array[Float],
  noise_std : Float,
  rng : Xoshiro,
) -> Array[Float] {
  let (action, _cache) = gtrxl_actor_step(agent.actor, obs)
  if noise_std <= 0.0F {
    return action
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
  noisy
}

///|
/// 1-step twin-critic TD target: target_t = r_t + γ(1 - d_t) *
/// min(q1_target(next_obs_t, next_act_t), q2_target(next_obs_t, next_act_t)).
/// `next_act_seq` from actor_target (caller computes).
pub fn gtrxl_ddpg_compute_td_target_seq(
  agent : GTrXL_DDPG,
  next_obs_seq : Array[Float],
  next_act_seq : Array[Float],
  reward_seq : Array[Float],
  done_seq : Array[Float],
  seq_len : Int,
) -> Array[Float] {
  let (q1_next_seq, _) = gtrxl_qnetwork_continuous_seq_forward(
    agent.critic1_target, next_obs_seq, next_act_seq, seq_len,
  )
  let (q2_next_seq, _) = gtrxl_qnetwork_continuous_seq_forward(
    agent.critic2_target, next_obs_seq, next_act_seq, seq_len,
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
/// Polyak averaging for GTrXLDeterministicPolicy: target <- (1 - tau) *
/// target + tau * source. Operates on MLP weights + biases + GTrXL
/// block (ffn_w1, ffn_b1, ffn_w2, ffn_b2, ffn_gate_w, ffn_gate_b).
fn gtrxl_deterministic_policy_soft_update(
  target : GTrXLDeterministicPolicy,
  source : GTrXLDeterministicPolicy,
  tau : Float,
) -> Unit {
  let one_minus = 1.0F - tau
  // MLP w1
  for i in 0..<target.mlp_w1.length() {
    for k in 0..<target.mlp_w1[i].length() {
      target.mlp_w1[i][k] = one_minus * target.mlp_w1[i][k] + tau * source.mlp_w1[i][k]
    }
  }
  // MLP b1
  for i in 0..<target.mlp_b1.length() {
    target.mlp_b1[i] = one_minus * target.mlp_b1[i] + tau * source.mlp_b1[i]
  }
  // MLP w2
  for i in 0..<target.mlp_w2.length() {
    for k in 0..<target.mlp_w2[i].length() {
      target.mlp_w2[i][k] = one_minus * target.mlp_w2[i][k] + tau * source.mlp_w2[i][k]
    }
  }
  // MLP b2
  for i in 0..<target.mlp_b2.length() {
    target.mlp_b2[i] = one_minus * target.mlp_b2[i] + tau * source.mlp_b2[i]
  }
  // GTrXL block: ffn_w1, ffn_b1, ffn_w2, ffn_b2, ffn_gate_w, ffn_gate_b
  let target_block = target.block
  let source_block = source.block
  for i in 0..<target_block.ffn_w1.length() {
    for k in 0..<target_block.ffn_w1[i].length() {
      target_block.ffn_w1[i][k] = one_minus * target_block.ffn_w1[i][k] + tau * source_block.ffn_w1[i][k]
      target_block.ffn_w2[i][k] = one_minus * target_block.ffn_w2[i][k] + tau * source_block.ffn_w2[i][k]
      target_block.ffn_gate_w[i][k] = one_minus * target_block.ffn_gate_w[i][k] + tau * source_block.ffn_gate_w[i][k]
    }
  }
  for i in 0..<target_block.ffn_b1.length() {
    target_block.ffn_b1[i] = one_minus * target_block.ffn_b1[i] + tau * source_block.ffn_b1[i]
    target_block.ffn_b2[i] = one_minus * target_block.ffn_b2[i] + tau * source_block.ffn_b2[i]
    target_block.ffn_gate_b[i] = one_minus * target_block.ffn_gate_b[i] + tau * source_block.ffn_gate_b[i]
  }
}

///|
/// Polyak averaging for GTrXLQNetworkContinuous. Same as policy version
/// plus scalar mlp_b2.
fn gtrxl_qnetwork_continuous_soft_update(
  target : GTrXLQNetworkContinuous,
  source : GTrXLQNetworkContinuous,
  tau : Float,
) -> Unit {
  let one_minus = 1.0F - tau
  // MLP w1
  for i in 0..<target.mlp_w1.length() {
    for k in 0..<target.mlp_w1[i].length() {
      target.mlp_w1[i][k] = one_minus * target.mlp_w1[i][k] + tau * source.mlp_w1[i][k]
    }
  }
  // MLP b1
  for i in 0..<target.mlp_b1.length() {
    target.mlp_b1[i] = one_minus * target.mlp_b1[i] + tau * source.mlp_b1[i]
  }
  // MLP w2
  for i in 0..<target.mlp_w2.length() {
    for k in 0..<target.mlp_w2[i].length() {
      target.mlp_w2[i][k] = one_minus * target.mlp_w2[i][k] + tau * source.mlp_w2[i][k]
    }
  }
  // MLP b2 (scalar)
  target.mlp_b2 = one_minus * target.mlp_b2 + tau * source.mlp_b2
  // GTrXL block
  let target_block = target.block
  let source_block = source.block
  for i in 0..<target_block.ffn_w1.length() {
    for k in 0..<target_block.ffn_w1[i].length() {
      target_block.ffn_w1[i][k] = one_minus * target_block.ffn_w1[i][k] + tau * source_block.ffn_w1[i][k]
      target_block.ffn_w2[i][k] = one_minus * target_block.ffn_w2[i][k] + tau * source_block.ffn_w2[i][k]
      target_block.ffn_gate_w[i][k] = one_minus * target_block.ffn_gate_w[i][k] + tau * source_block.ffn_gate_w[i][k]
    }
  }
  for i in 0..<target_block.ffn_b1.length() {
    target_block.ffn_b1[i] = one_minus * target_block.ffn_b1[i] + tau * source_block.ffn_b1[i]
    target_block.ffn_b2[i] = one_minus * target_block.ffn_b2[i] + tau * source_block.ffn_b2[i]
    target_block.ffn_gate_b[i] = one_minus * target_block.ffn_gate_b[i] + tau * source_block.ffn_gate_b[i]
  }
}

///|
/// Soft-update all 3 target networks toward source via Polyak.
pub fn gtrxl_ddpg_soft_update(agent : GTrXL_DDPG) -> Unit {
  gtrxl_deterministic_policy_soft_update(agent.actor_target, agent.actor, agent.tau)
  gtrxl_qnetwork_continuous_soft_update(agent.critic1_target, agent.critic1, agent.tau)
  gtrxl_qnetwork_continuous_soft_update(agent.critic2_target, agent.critic2, agent.tau)
}
