// lstm_deterministic_policy.mbt — Recurrent deterministic policy for
// partially-observable continuous-control RL (v0.61.0).
//
// Architecture: MLP w1 (state → hidden) → ReLU → LSTM cell (over time,
// tracking both hidden state h_t and cell state c_t) → MLP w2
// (hidden → action) → tanh squash to [action_low, action_high].
//
//   state_t ∈ R^{state_dim}
//   x_proj_t = ReLU(w1 · state_t + b1)                ∈ R^{lstm_hidden}
//   (h_t, c_t) = LSTM_cell(x_proj_t, h_{t-1}, c_{t-1})
//   a_pre_t = w2 · h_t + b2                           ∈ R^{action_dim}
//   action_t = tanh(a_pre_t) * (high - low) / 2
//            + (high + low) / 2                        ∈ R^{action_dim}
//
// This is the LSTM counterpart of `GRUDeterministicPolicy` (v0.58.0)
// for environments where the observation alone is insufficient and a
// recurrent summary (with explicit memory cell) is needed. The LSTM
// maintains both a hidden state h_t and a cell state c_t (vanilla
// LSTM with 4 gates: forget / input / candidate / output).
//
// Reference: Hausknecht & Stone 2015 "Deep Recurrent Q-Learning for
// Partially Observable MDPs" (the recurrent actor pattern); the
// LSTM variant follows Gers 2000 "Learning to Forget".

///|
/// Recurrent deterministic policy. The LSTM hidden dimension equals
/// the MLP hidden dimension so the ReLU-projected state and the LSTM
/// cell input/output match.
pub struct LSTMDeterministicPolicy {
  state_dim : Int
  action_dim : Int
  lstm_hidden : Int
  mlp_w1 : Array[Array[Float]]
  mlp_b1 : Array[Float]
  lstm : LstmCellParam
  mlp_w2 : Array[Array[Float]]
  mlp_b2 : Array[Float]
  action_low : Float
  action_high : Float
}

///|
/// Build a fresh LSTMDeterministicPolicy. The MLP weights get
/// Xavier-normal init scaled by `sqrtf(2.0 / fan_in)` (He-style for
/// ReLU), and the LSTM cell reuses its own init (Xavier-normal across
/// the 4 gate matrices). Zero biases. `action_low` / `action_high`
/// define the squash range.
pub fn LSTMDeterministicPolicy::new(
  state_dim : Int,
  action_dim : Int,
  lstm_hidden : Int,
  action_low : Float,
  action_high : Float,
  seed : UInt64,
) -> LSTMDeterministicPolicy {
  let rng1 = Xoshiro::from_state(seed, seed + 1UL, seed + 2UL, seed + 3UL)
  let mlp_w1_std = sqrtf(2.0F / Float::from_int(state_dim))
  let mlp_w1 = xavier_normal(lstm_hidden, state_dim, mlp_w1_std, rng1)
  let mlp_b1 : Array[Float] = Array::make(lstm_hidden, 0.0F)
  let lstm = LstmCellParam::new(lstm_hidden, lstm_hidden, seed + 4UL)
  let rng2 = Xoshiro::from_state(seed + 5UL, seed + 6UL, seed + 7UL, seed + 8UL)
  let mlp_w2_std = sqrtf(2.0F / Float::from_int(lstm_hidden))
  let mlp_w2 = xavier_normal(action_dim, lstm_hidden, mlp_w2_std, rng2)
  let mlp_b2 : Array[Float] = Array::make(action_dim, 0.0F)
  {
    state_dim,
    action_dim,
    lstm_hidden,
    mlp_w1,
    mlp_b1,
    lstm,
    mlp_w2,
    mlp_b2,
    action_low,
    action_high,
  }
}

///|
/// Single-step forward. `obs` is the current observation (length
/// state_dim), `hidden` is the previous LSTM hidden state (length
/// lstm_hidden), `cell` is the previous LSTM cell state (length
/// lstm_hidden). Returns `(action, new_hidden, new_cell)` where
/// action is the squashed continuous action (length action_dim).
pub fn lstm_deterministic_policy_step(
  policy : LSTMDeterministicPolicy,
  obs : Array[Float],
  hidden : Array[Float],
  cell : Array[Float],
) -> (Array[Float], Array[Float], Array[Float]) {
  // x_proj = ReLU(w1 · obs + b1)
  let x_proj_pre = matvec(policy.mlp_w1, policy.mlp_b1, obs)
  let x_proj = relu_forward(x_proj_pre)
  // (hidden_next, cell_next) = LSTM_cell(x_proj, hidden, cell)
  let (hidden_next, cache) = lstm_cell_forward(
    x_proj, hidden, cell, policy.lstm,
  )
  let cell_next = cache.c_t
  // a_pre = w2 · hidden_next + b2
  let a_pre = matvec(policy.mlp_w2, policy.mlp_b2, hidden_next)
  // squash: action = tanh(a_pre) * (high - low) / 2 + (high + low) / 2
  let half_range = (policy.action_high - policy.action_low) * 0.5F
  let mid = (policy.action_high + policy.action_low) * 0.5F
  let action : Array[Float] = Array::make(policy.action_dim, 0.0F)
  for i in 0..<policy.action_dim {
    action[i] = tanhf(a_pre[i]) * half_range + mid
  }
  (action, hidden_next, cell_next)
}

///|
/// Sequence forward. `obs_seq` is flat row-major of shape
/// `[seq_len × state_dim]`. `hidden_init` and `cell_init` are the
/// initial LSTM states (length lstm_hidden each; pass zeros to start
/// fresh). Returns `(action_seq, final_hidden, final_cell)` where
/// `action_seq` is flat `[seq_len × action_dim]`.
pub fn lstm_deterministic_policy_seq_forward(
  policy : LSTMDeterministicPolicy,
  obs_seq : Array[Float],
  seq_len : Int,
  hidden_init : Array[Float],
  cell_init : Array[Float],
) -> (Array[Float], Array[Float], Array[Float]) {
  let action_seq : Array[Float] = Array::make(seq_len * policy.action_dim, 0.0F)
  let mut hidden = hidden_init
  let mut cell = cell_init
  for t in 0..<seq_len {
    let obs_off = t * policy.state_dim
    let obs : Array[Float] = Array::make(policy.state_dim, 0.0F)
    for k in 0..<policy.state_dim {
      obs[k] = obs_seq[obs_off + k]
    }
    let (action, new_hidden, new_cell) = lstm_deterministic_policy_step(
      policy, obs, hidden, cell,
    )
    let act_off = t * policy.action_dim
    for k in 0..<policy.action_dim {
      action_seq[act_off + k] = action[k]
    }
    hidden = new_hidden
    cell = new_cell
  }
  (action_seq, hidden, cell)
}