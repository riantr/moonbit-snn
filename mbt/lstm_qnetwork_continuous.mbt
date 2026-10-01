// lstm_qnetwork_continuous.mbt — Recurrent continuous-action Q-network
// for partially-observable environments (v0.62.0).
//
// Architecture: MLP w1 (concat(state, action) -> hidden) -> ReLU ->
// LSTM cell (over time, tracking both hidden state h_t and cell state
// c_t) -> MLP w2 (hidden -> scalar Q).
//
//   [state_t ; action_t]    ∈ R^{state_dim + action_dim}
//   x_proj_t = ReLU(w1 · [state_t ; action_t] + b1)         ∈ R^{hidden}
//   (h_t, c_t) = LSTM_cell(x_proj_t, h_{t-1}, c_{t-1})
//   q_t = w2 · h_t + b2                                     ∈ R
//
// This is the LSTM counterpart of `GRUQNetworkContinuous` (v0.59.0) for
// the DDPG_LSTM agent. The LSTM hidden state mixes past state-action
// pairs so the critic can break the Markov assumption.

///|
/// Recurrent continuous-action Q-network. MLP hidden dim equals LSTM
/// hidden dim so the ReLU-projected [state; action] and the LSTM cell
/// input/output match. `mlp_b2` is a scalar (the critic outputs a
/// single Q-value per timestep).
pub struct LSTMQNetworkContinuous {
  state_dim : Int
  action_dim : Int
  hidden : Int
  mlp_w1 : Array[Array[Float]]
  mlp_b1 : Array[Float]
  lstm : LstmCellParam
  mlp_w2 : Array[Array[Float]]
  mut mlp_b2 : Float
}

///|
/// Build a fresh LSTMQNetworkContinuous. MLP weights use Xavier-normal
/// init scaled by sqrtf(2 / fan_in) (He-style for ReLU). LSTM cell
/// uses its own init. Zero biases.
pub fn LSTMQNetworkContinuous::new(
  state_dim : Int,
  action_dim : Int,
  hidden : Int,
  seed : UInt64,
) -> LSTMQNetworkContinuous {
  let in_dim = state_dim + action_dim
  let rng1 = Xoshiro::from_state(seed, seed + 1UL, seed + 2UL, seed + 3UL)
  let std1 = sqrtf(2.0F / Float::from_int(in_dim))
  let mlp_w1 = xavier_normal(hidden, in_dim, std1, rng1)
  let mlp_b1 : Array[Float] = Array::make(hidden, 0.0F)
  let lstm = LstmCellParam::new(hidden, hidden, seed + 4UL)
  let rng2 = Xoshiro::from_state(seed + 5UL, seed + 6UL, seed + 7UL, seed + 8UL)
  let std2 = sqrtf(2.0F / Float::from_int(hidden))
  let mlp_w2 = xavier_normal(1, hidden, std2, rng2)
  { state_dim, action_dim, hidden, mlp_w1, mlp_b1, lstm, mlp_w2, mlp_b2: 0.0F }
}

///|
/// Single-step forward. Returns the scalar Q-value for the given
/// (state, action) pair mixed with the previous hidden + cell state,
/// plus the new hidden + cell state for the next step.
pub fn lstm_qnetwork_continuous_step(
  qnet : LSTMQNetworkContinuous,
  state : Array[Float],
  action : Array[Float],
  hidden : Array[Float],
  cell : Array[Float],
) -> (Float, Array[Float], Array[Float]) {
  // sa = concat(state, action)
  let sa = vec_concat(state, action)
  // x_proj = ReLU(w1 · sa + b1)
  let x_proj_pre = matvec(qnet.mlp_w1, qnet.mlp_b1, sa)
  let x_proj = relu_forward(x_proj_pre)
  // (hidden_next, cell_next) = LSTM_cell(x_proj, hidden, cell)
  let (hidden_next, cache) = lstm_cell_forward(x_proj, hidden, cell, qnet.lstm)
  let cell_next = cache.c_t
  // q = w2 · hidden_next + b2
  let q_pre = matvec(qnet.mlp_w2, [qnet.mlp_b2], hidden_next)
  (q_pre[0], hidden_next, cell_next)
}

///|
/// Sequence forward. `state_seq` is flat row-major `[seq_len × state_dim]`,
/// `action_seq` is flat row-major `[seq_len × action_dim]`. Returns
/// `(q_seq, final_hidden, final_cell)` where `q_seq` is flat `[seq_len]`.
pub fn lstm_qnetwork_continuous_seq_forward(
  qnet : LSTMQNetworkContinuous,
  state_seq : Array[Float],
  action_seq : Array[Float],
  seq_len : Int,
  hidden_init : Array[Float],
  cell_init : Array[Float],
) -> (Array[Float], Array[Float], Array[Float]) {
  let q_seq : Array[Float] = Array::make(seq_len, 0.0F)
  let mut hidden = hidden_init
  let mut cell = cell_init
  for t in 0..<seq_len {
    let st_off = t * qnet.state_dim
    let at_off = t * qnet.action_dim
    let state : Array[Float] = Array::make(qnet.state_dim, 0.0F)
    let action : Array[Float] = Array::make(qnet.action_dim, 0.0F)
    for k in 0..<qnet.state_dim {
      state[k] = state_seq[st_off + k]
    }
    for k in 0..<qnet.action_dim {
      action[k] = action_seq[at_off + k]
    }
    let (q, new_hidden, new_cell) = lstm_qnetwork_continuous_step(
      qnet, state, action, hidden, cell,
    )
    q_seq[t] = q
    hidden = new_hidden
    cell = new_cell
  }
  (q_seq, hidden, cell)
}