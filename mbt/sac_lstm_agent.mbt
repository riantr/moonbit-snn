// sac_lstm_agent.mbt — SAC_LSTM agent: stochastic recurrent SAC for
// partially-observable continuous-control POMDPs (v0.71.0).
//
// Parallel to v0.70.0 SAC_GRU agent but with LSTM hidden + cell
// state. Same SAC semantics (stochastic max-entropy actor + twin
// Q-critics + automatic alpha + 5 Polyak targets).

///|
/// SAC_LSTM agent: stochastic actor + twin recurrent critics + 5
/// Polyak-averaged target networks + automatic alpha.
pub(all) struct SAC_LSTM {
  actor : LSTMSACActor
  critic1 : LSTMQNetworkContinuous
  critic2 : LSTMQNetworkContinuous
  actor_target : LSTMSACActor
  critic1_target : LSTMQNetworkContinuous
  critic2_target : LSTMQNetworkContinuous
  gamma : Float
  mut tau : Float
  hidden : Int
  mut log_alpha : Float
  target_entropy : Float
}

///|
/// Build a fresh SAC_LSTM. 6 distinct seeds so Polyak targets start
/// far from source networks. `initial_alpha = 1.0` (log_alpha = 0).
pub fn SAC_LSTM::new(
  state_dim : Int,
  action_dim : Int,
  hidden : Int,
  action_low : Float,
  action_high : Float,
  gamma : Float,
  tau : Float,
  seed : UInt64,
) -> SAC_LSTM {
  let actor : LSTMSACActor = LSTMSACActor::new(
    state_dim, action_dim, hidden, action_low, action_high, seed,
  )
  let critic1 : LSTMQNetworkContinuous = LSTMQNetworkContinuous::new(
    state_dim, action_dim, hidden, seed + 1UL,
  )
  let critic2 : LSTMQNetworkContinuous = LSTMQNetworkContinuous::new(
    state_dim, action_dim, hidden, seed + 2UL,
  )
  let actor_target : LSTMSACActor = LSTMSACActor::new(
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
    hidden,
    log_alpha : 0.0F,
    target_entropy : -Float::from_int(action_dim),
  }
}

///|
/// Sample a stochastic action for the current observation +
/// recurrent (hidden, cell) state. Returns
/// `(action, new_hidden, new_cell, log_prob)`.
pub fn sac_lstm_act(
  agent : SAC_LSTM,
  obs : Array[Float],
  hidden : Array[Float],
  cell : Array[Float],
  rng : Xoshiro,
) -> (Array[Float], Array[Float], Array[Float], Float) {
  let (action, log_prob, hidden_next, cell_next) = sac_lstm_actor_step(
    agent.actor, obs, hidden, cell, rng,
  )
  (action, hidden_next, cell_next, log_prob)
}

///|
/// Compute the 1-step twin-critic TD target for a T-step sequence.
/// `next_act_seq` should be the stochastic action sample from the
/// target actor (caller computes via `sac_lstm_act` on `actor_target`).
pub fn sac_lstm_compute_td_target_seq(
  agent : SAC_LSTM,
  next_obs_seq : Array[Float],
  next_act_seq : Array[Float],
  reward_seq : Array[Float],
  done_seq : Array[Float],
  seq_len : Int,
) -> Array[Float] {
  let hidden_init : Array[Float] = Array::make(agent.hidden, 0.0F)
  let cell_init : Array[Float] = Array::make(agent.hidden, 0.0F)
  let (q1_next, _, _) = lstm_qnetwork_continuous_seq_forward(
    agent.critic1_target, next_obs_seq, next_act_seq, seq_len,
    hidden_init, cell_init,
  )
  let (q2_next, _, _) = lstm_qnetwork_continuous_seq_forward(
    agent.critic2_target, next_obs_seq, next_act_seq, seq_len,
    hidden_init, cell_init,
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
/// Polyak averaging for LSTMSACActor. target <- (1 - tau) * target +
/// tau * source. Operates on all MLP weights + biases + LSTM
/// sub-parameters (w_f, w_i, w_c, w_o + b_f, b_i, b_c, b_o).
fn sac_lstm_actor_soft_update(
  target : LSTMSACActor,
  source : LSTMSACActor,
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
  let target_lstm = target.lstm
  let source_lstm = source.lstm
  for i in 0..<target_lstm.d_h {
    let in_dim = target_lstm.w_f[i].length()
    for k in 0..<in_dim {
      target_lstm.w_f[i][k] = one_minus * target_lstm.w_f[i][k] +
        tau * source_lstm.w_f[i][k]
      target_lstm.w_i[i][k] = one_minus * target_lstm.w_i[i][k] +
        tau * source_lstm.w_i[i][k]
      target_lstm.w_c[i][k] = one_minus * target_lstm.w_c[i][k] +
        tau * source_lstm.w_c[i][k]
      target_lstm.w_o[i][k] = one_minus * target_lstm.w_o[i][k] +
        tau * source_lstm.w_o[i][k]
    }
    target_lstm.b_f[i] = one_minus * target_lstm.b_f[i] + tau * source_lstm.b_f[i]
    target_lstm.b_i[i] = one_minus * target_lstm.b_i[i] + tau * source_lstm.b_i[i]
    target_lstm.b_c[i] = one_minus * target_lstm.b_c[i] + tau * source_lstm.b_c[i]
    target_lstm.b_o[i] = one_minus * target_lstm.b_o[i] + tau * source_lstm.b_o[i]
  }
}

///|
/// Soft-update all 3 target networks via Polyak averaging.
pub fn sac_lstm_soft_update(agent : SAC_LSTM) -> Unit {
  sac_lstm_actor_soft_update(agent.actor_target, agent.actor, agent.tau)
  lstm_qnetwork_continuous_soft_update(agent.critic1_target, agent.critic1, agent.tau)
  lstm_qnetwork_continuous_soft_update(agent.critic2_target, agent.critic2, agent.tau)
}

///|
/// Auto-alpha gradient step (identical to GRU version). Updates
/// `log_alpha` toward `target_entropy` using the mean per-timestep
/// log-prob over the T-step window.
pub fn sac_lstm_update_alpha(
  agent : SAC_LSTM,
  mean_log_prob : Float,
  lr : Float,
) -> Unit {
  let target = agent.target_entropy
  let grad_log_alpha = -(mean_log_prob + target)
  agent.log_alpha = agent.log_alpha - lr * grad_log_alpha
}

///|
/// Get the current alpha = exp(log_alpha).
pub fn sac_lstm_get_alpha(agent : SAC_LSTM) -> Float {
  expf(agent.log_alpha)
}