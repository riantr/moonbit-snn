// lstm_ddpg.mbt — DDPG agent with LSTM actor + twin LSTM critics for
// partially-observable continuous-control RL (v0.63.0).
//
// Architecture: LSTM actor + twin LSTM critics + 5 Polyak-averaged
// target networks. Recurrent (hidden, cell) state pair threads through
// target twin-critic TD update.
//
// Scope of v0.63.0:
//   - LSTM_DDPG struct + constructor (6 distinct seeds)
//   - select_action (single-step inference + Gaussian noise)
//   - compute_td_target_seq (twin-critic forward, no param update)
//   - soft_update (Polyak averaging across all 5 targets: each MLP + LSTM
//     has 4 gate matrices × (W + b) = 8 tensors per LSTM cell, plus 2 MLP
//     weight matrices + 2 biases per network = 12 tensors per target,
//     60 total)
//
// BPTT-driven parameter update deferred — see PR for rationale.
pub(all) struct LSTM_DDPG {
  actor : LSTMDeterministicPolicy
  critic1 : LSTMQNetworkContinuous
  critic2 : LSTMQNetworkContinuous
  actor_target : LSTMDeterministicPolicy
  critic1_target : LSTMQNetworkContinuous
  critic2_target : LSTMQNetworkContinuous
  gamma : Float
  mut tau : Float
  exploration_noise : Float
  hidden : Int
}

///|
/// Build fresh LSTM_DDPG. 6 distinct seeds so Polyak targets start far
/// from source networks (they get soft-updated into place).
pub fn LSTM_DDPG::new(
  state_dim : Int,
  action_dim : Int,
  hidden : Int,
  action_low : Float,
  action_high : Float,
  gamma : Float,
  tau : Float,
  exploration_noise : Float,
  seed : UInt64,
) -> LSTM_DDPG {
  let actor : LSTMDeterministicPolicy = LSTMDeterministicPolicy::new(
    state_dim, action_dim, hidden, action_low, action_high, seed,
  )
  let critic1 : LSTMQNetworkContinuous = LSTMQNetworkContinuous::new(
    state_dim, action_dim, hidden, seed + 1UL,
  )
  let critic2 : LSTMQNetworkContinuous = LSTMQNetworkContinuous::new(
    state_dim, action_dim, hidden, seed + 2UL,
  )
  let actor_target : LSTMDeterministicPolicy = LSTMDeterministicPolicy::new(
    state_dim, action_dim, hidden, action_low, action_high, seed + 100UL,
  )
  let critic1_target : LSTMQNetworkContinuous = LSTMQNetworkContinuous::new(
    state_dim, action_dim, hidden, seed + 101UL,
  )
  let critic2_target : LSTMQNetworkContinuous = LSTMQNetworkContinuous::new(
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
/// Single-step inference with Gaussian noise (clipped to [action_low,
/// action_high]). `noise_std=0.0F` recovers deterministic output.
pub fn lstm_ddpg_select_action(
  agent : LSTM_DDPG,
  obs : Array[Float],
  hidden : Array[Float],
  cell : Array[Float],
  noise_std : Float,
  rng : Xoshiro,
) -> (Array[Float], Array[Float], Array[Float]) {
  let (action, hidden_next, cell_next) = lstm_deterministic_policy_step(
    agent.actor, obs, hidden, cell,
  )
  if noise_std <= 0.0F {
    return (action, hidden_next, cell_next)
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
  (noisy, hidden_next, cell_next)
}

///|
/// 1-step twin-critic TD target: target_t = r_t + γ(1 - d_t) *
/// min(q1_target(next_obs_t, next_act_t), q2_target(next_obs_t, next_act_t)).
/// `next_act_seq` from actor_target (caller computes).
pub fn lstm_ddpg_compute_td_target_seq(
  agent : LSTM_DDPG,
  next_obs_seq : Array[Float],
  next_act_seq : Array[Float],
  reward_seq : Array[Float],
  done_seq : Array[Float],
  seq_len : Int,
) -> Array[Float] {
  let (q1_next_seq, _, _) = lstm_qnetwork_continuous_seq_forward(
    agent.critic1_target, next_obs_seq, next_act_seq, seq_len,
    Array::make(agent.hidden, 0.0F), Array::make(agent.hidden, 0.0F),
  )
  let (q2_next_seq, _, _) = lstm_qnetwork_continuous_seq_forward(
    agent.critic2_target, next_obs_seq, next_act_seq, seq_len,
    Array::make(agent.hidden, 0.0F), Array::make(agent.hidden, 0.0F),
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
/// Polyak averaging for LSTMDeterministicPolicy: target <- (1 - tau) * target +
/// tau * source. Operates on MLP weights + biases + LSTM sub-parameters
/// (w_f, w_i, w_c, w_o + b_f, b_i, b_c, b_o).
fn lstm_deterministic_policy_soft_update(
  target : LSTMDeterministicPolicy,
  source : LSTMDeterministicPolicy,
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
  for i in 0..<target.mlp_b2.length() {
    target.mlp_b2[i] = one_minus * target.mlp_b2[i] + tau * source.mlp_b2[i]
  }
  let target_lstm = target.lstm
  let source_lstm = source.lstm
  for i in 0..<target_lstm.w_f.length() {
    for k in 0..<target_lstm.w_f[i].length() {
      target_lstm.w_f[i][k] = one_minus * target_lstm.w_f[i][k] + tau * source_lstm.w_f[i][k]
      target_lstm.w_i[i][k] = one_minus * target_lstm.w_i[i][k] + tau * source_lstm.w_i[i][k]
      target_lstm.w_c[i][k] = one_minus * target_lstm.w_c[i][k] + tau * source_lstm.w_c[i][k]
      target_lstm.w_o[i][k] = one_minus * target_lstm.w_o[i][k] + tau * source_lstm.w_o[i][k]
    }
    target_lstm.b_f[i] = one_minus * target_lstm.b_f[i] + tau * source_lstm.b_f[i]
    target_lstm.b_i[i] = one_minus * target_lstm.b_i[i] + tau * source_lstm.b_i[i]
    target_lstm.b_c[i] = one_minus * target_lstm.b_c[i] + tau * source_lstm.b_c[i]
    target_lstm.b_o[i] = one_minus * target_lstm.b_o[i] + tau * source_lstm.b_o[i]
  }
}

///|
/// Polyak averaging for LSTMQNetworkContinuous. Same as policy version plus
/// scalar mlp_b2.
fn lstm_qnetwork_continuous_soft_update(
  target : LSTMQNetworkContinuous,
  source : LSTMQNetworkContinuous,
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
  let target_lstm = target.lstm
  let source_lstm = source.lstm
  for i in 0..<target_lstm.w_f.length() {
    for k in 0..<target_lstm.w_f[i].length() {
      target_lstm.w_f[i][k] = one_minus * target_lstm.w_f[i][k] + tau * source_lstm.w_f[i][k]
      target_lstm.w_i[i][k] = one_minus * target_lstm.w_i[i][k] + tau * source_lstm.w_i[i][k]
      target_lstm.w_c[i][k] = one_minus * target_lstm.w_c[i][k] + tau * source_lstm.w_c[i][k]
      target_lstm.w_o[i][k] = one_minus * target_lstm.w_o[i][k] + tau * source_lstm.w_o[i][k]
    }
    target_lstm.b_f[i] = one_minus * target_lstm.b_f[i] + tau * source_lstm.b_f[i]
    target_lstm.b_i[i] = one_minus * target_lstm.b_i[i] + tau * source_lstm.b_i[i]
    target_lstm.b_c[i] = one_minus * target_lstm.b_c[i] + tau * source_lstm.b_c[i]
    target_lstm.b_o[i] = one_minus * target_lstm.b_o[i] + tau * source_lstm.b_o[i]
  }
}

///|
/// Soft-update all 3 target networks toward source via Polyak.
pub fn lstm_ddpg_soft_update(agent : LSTM_DDPG) -> Unit {
  lstm_deterministic_policy_soft_update(agent.actor_target, agent.actor, agent.tau)
  lstm_qnetwork_continuous_soft_update(agent.critic1_target, agent.critic1, agent.tau)
  lstm_qnetwork_continuous_soft_update(agent.critic2_target, agent.critic2, agent.tau)
}