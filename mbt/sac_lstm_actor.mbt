// sac_lstm_actor.mbt — Recurrent SAC (Soft Actor-Critic) stochastic
// actor with LSTM hidden + cell state for partially-observable
// continuous-control POMDPs (v0.69.0).
//
// Parallel to v0.68.0 GRUSACActor but for the LSTM variant. Uses the
// same SAC math (mean + log_std branches, tanh squash with
// correction, Gaussian sampling).
//
// Reference: Haarnoja et al. 2018 "Soft Actor-Critic" + Haarnoja 2019.

///|
/// Recurrent SAC stochastic actor with LSTM hidden + cell state.
pub struct LSTMSACActor {
  state_dim : Int
  action_dim : Int
  lstm_hidden : Int
  mlp_w1 : Array[Array[Float]]
  mlp_b1 : Array[Float]
  lstm : LstmCellParam
  mlp_w_mean : Array[Array[Float]]
  mlp_b_mean : Array[Float]
  mlp_w_log_std : Array[Array[Float]]
  mlp_b_log_std : Array[Float]
  action_low : Float
  action_high : Float
  log_std_min : Float
  log_std_max : Float
}

///|
/// Build a fresh `LSTMSACActor`. MLP weights use Xavier-normal init;
/// LSTM cell uses its own init. log_std biases are init to 0.
pub fn LSTMSACActor::new(
  state_dim : Int,
  action_dim : Int,
  lstm_hidden : Int,
  action_low : Float,
  action_high : Float,
  seed : UInt64,
) -> LSTMSACActor {
  let rng1 = Xoshiro::from_state(seed, seed + 1UL, seed + 2UL, seed + 3UL)
  let std1 = sqrtf(2.0F / Float::from_int(state_dim))
  let mlp_w1 = xavier_normal(lstm_hidden, state_dim, std1, rng1)
  let mlp_b1 : Array[Float] = Array::make(lstm_hidden, 0.0F)
  let lstm = LstmCellParam::new(lstm_hidden, lstm_hidden, seed + 4UL)
  let rng2 = Xoshiro::from_state(seed + 5UL, seed + 6UL, seed + 7UL, seed + 8UL)
  let std2 = sqrtf(2.0F / Float::from_int(lstm_hidden))
  let mlp_w_mean = xavier_normal(action_dim, lstm_hidden, std2, rng2)
  let mlp_b_mean : Array[Float] = Array::make(action_dim, 0.0F)
  let rng3 = Xoshiro::from_state(seed + 9UL, seed + 10UL, seed + 11UL, seed + 12UL)
  let mlp_w_log_std = xavier_normal(action_dim, lstm_hidden, std2, rng3)
  let mlp_b_log_std : Array[Float] = Array::make(action_dim, 0.0F)
  {
    state_dim,
    action_dim,
    lstm_hidden,
    mlp_w1,
    mlp_b1,
    lstm,
    mlp_w_mean,
    mlp_b_mean,
    mlp_w_log_std,
    mlp_b_log_std,
    action_low,
    action_high,
    log_std_min : -20.0F,
    log_std_max : 2.0F,
  }
}

///|
/// Single-step stochastic LSTM actor forward. Returns
/// `(action, log_prob, new_hidden, new_cell)`.
pub fn sac_lstm_actor_step(
  policy : LSTMSACActor,
  state : Array[Float],
  hidden : Array[Float],
  cell : Array[Float],
  rng : Xoshiro,
) -> (Array[Float], Float, Array[Float], Array[Float]) {
  let x_proj_pre = matvec(policy.mlp_w1, policy.mlp_b1, state)
  let x_proj = relu_forward(x_proj_pre)
  let (hidden_next, cache) = lstm_cell_forward(x_proj, hidden, cell, policy.lstm)
  let cell_next = cache.c_t
  let mean_pre = matvec(policy.mlp_w_mean, policy.mlp_b_mean, hidden_next)
  let log_std_pre = matvec(policy.mlp_w_log_std, policy.mlp_b_log_std, hidden_next)
  // Clamp log_std.
  let log_std : Array[Float] = Array::make(policy.action_dim, 0.0F)
  for i in 0..<policy.action_dim {
    let v = log_std_pre[i]
    if v < policy.log_std_min {
      log_std[i] = policy.log_std_min
    } else if v > policy.log_std_max {
      log_std[i] = policy.log_std_max
    } else {
      log_std[i] = v
    }
  }
  let scale = (policy.action_high - policy.action_low) * 0.5F
  let mid = (policy.action_high + policy.action_low) * 0.5F
  let action : Array[Float] = Array::make(policy.action_dim, 0.0F)
  let mean_tanh : Array[Float] = Array::make(policy.action_dim, 0.0F)
  for i in 0..<policy.action_dim {
    let t = tanhf(mean_pre[i])
    mean_tanh[i] = t
    action[i] = mid + t * scale
  }
  let mut log_prob = 0.0F
  for i in 0..<policy.action_dim {
    let (z1, _) = box_muller(rng)
    let eps = Float::from_double(z1)
    let std_dev = expf(log_std[i])
    let a_raw = mean_tanh[i] + std_dev * eps
    let a_squash = tanhf(a_raw)
    action[i] = mid + a_squash * scale
    let inv_std = 1.0F / std_dev
    let z_mah = (a_raw - mean_tanh[i]) * inv_std
    let dim_log_prob = -0.5F *
      (z_mah * z_mah + 2.0F * log_std[i] + 0.9189385F)
    let one_minus_tanh_sq = 1.0F - a_squash * a_squash
    let safe_term = if one_minus_tanh_sq < 1.0e-6F {
      1.0e-6F
    } else {
      one_minus_tanh_sq
    }
    let squash_correction = -logf(safe_term)
    log_prob = log_prob + dim_log_prob + squash_correction
  }
  (action, log_prob, hidden_next, cell_next)
}

///|
/// Helper API: sample a stochastic action and return
/// `(action, new_hidden, new_cell, log_prob)` — DDPG-style helper
/// for the LSTM SAC actor.
pub fn sac_lstm_select_action(
  policy : LSTMSACActor,
  state : Array[Float],
  hidden : Array[Float],
  cell : Array[Float],
  rng : Xoshiro,
) -> (Array[Float], Array[Float], Array[Float], Float) {
  let (action, log_prob, hidden_next, cell_next) = sac_lstm_actor_step(
    policy, state, hidden, cell, rng,
  )
  (action, hidden_next, cell_next, log_prob)
}