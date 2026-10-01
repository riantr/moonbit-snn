// gru_qnetwork_continuous.mbt — Recurrent continuous-action Q-network
// for partially-observable environments (v0.59.0).
//
// Architecture: MLP w1 (concat(state, action) -> hidden) -> ReLU ->
// GRU cell (over time) -> MLP w2 (hidden -> scalar Q).
//
//   [state_t ; action_t]    ∈ R^{state_dim + action_dim}
//   x_proj_t = ReLU(w1 · [state_t ; action_t] + b1)         ∈ R^{hidden}
//   hidden_t = GRU_cell(x_proj_t, hidden_{t-1})              ∈ R^{hidden}
//   q_t = w2 · hidden_t + b2                                ∈ R
//
// This is the recurrent counterpart of `QNetworkContinuous` (v0.54.0)
// for the DDPG_GRU agent. The GRU hidden state mixes past state-action
// pairs so the critic can break the Markov assumption.

///|
/// Recurrent continuous-action Q-network. MLP hidden dim equals GRU
/// hidden dim so the ReLU-projected [state; action] and the GRU cell
/// input/output match. `mlp_b2` is a scalar (the critic outputs a
/// single Q-value per timestep).
pub struct GRUQNetworkContinuous {
  state_dim : Int
  action_dim : Int
  hidden : Int
  mlp_w1 : Array[Array[Float]]
  mlp_b1 : Array[Float]
  gru : GruCellParam
  mlp_w2 : Array[Array[Float]]
  mlp_b2 : Float
}

///|
/// Build a fresh GRUQNetworkContinuous. MLP weights use Xavier-normal
/// init scaled by sqrtf(2 / fan_in) (He-style for ReLU). GRU cell
/// uses its own init. Zero biases.
pub fn GRUQNetworkContinuous::new(
  state_dim : Int,
  action_dim : Int,
  hidden : Int,
  seed : UInt64,
) -> GRUQNetworkContinuous {
  let in_dim = state_dim + action_dim
  let rng1 = Xoshiro::from_state(seed, seed + 1UL, seed + 2UL, seed + 3UL)
  let std1 = sqrtf(2.0F / Float::from_int(in_dim))
  let mlp_w1 = xavier_normal(hidden, in_dim, std1, rng1)
  let mlp_b1 : Array[Float] = Array::make(hidden, 0.0F)
  let gru = GruCellParam::new(hidden, hidden, seed + 4UL)
  let rng2 = Xoshiro::from_state(seed + 5UL, seed + 6UL, seed + 7UL, seed + 8UL)
  let std2 = sqrtf(2.0F / Float::from_int(hidden))
  let mlp_w2 = xavier_normal(1, hidden, std2, rng2)
  { state_dim, action_dim, hidden, mlp_w1, mlp_b1, gru, mlp_w2, mlp_b2: 0.0F }
}

///|
/// Single-step forward. Returns the scalar Q-value for the given
/// (state, action) pair mixed with the previous hidden state, plus
/// the new hidden state for the next step.
pub fn gru_qnetwork_continuous_step(
  qnet : GRUQNetworkContinuous,
  state : Array[Float],
  action : Array[Float],
  hidden : Array[Float],
) -> (Float, Array[Float]) {
  // sa = concat(state, action)
  let sa = vec_concat(state, action)
  // x_proj = ReLU(w1 · sa + b1)
  let x_proj_pre = matvec(qnet.mlp_w1, qnet.mlp_b1, sa)
  let x_proj = relu_forward(x_proj_pre)
  // hidden_next = GRU_cell(x_proj, hidden)
  let (hidden_next, _cache) = gru_cell_forward(x_proj, hidden, qnet.gru)
  // q = w2 · hidden_next + b2
  let q_pre = matvec(qnet.mlp_w2, [qnet.mlp_b2], hidden_next)
  (q_pre[0], hidden_next)
}

///|
/// Sequence forward. `state_seq` is flat row-major `[seq_len × state_dim]`,
/// `action_seq` is flat row-major `[seq_len × action_dim]`. Returns
/// `(q_seq, final_hidden)` where `q_seq` is flat `[seq_len]`.
pub fn gru_qnetwork_continuous_seq_forward(
  qnet : GRUQNetworkContinuous,
  state_seq : Array[Float],
  action_seq : Array[Float],
  seq_len : Int,
  hidden_init : Array[Float],
) -> (Array[Float], Array[Float]) {
  let q_seq : Array[Float] = Array::make(seq_len, 0.0F)
  let mut hidden = hidden_init
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
    let (q, new_hidden) = gru_qnetwork_continuous_step(qnet, state, action, hidden)
    q_seq[t] = q
    hidden = new_hidden
  }
  (q_seq, hidden)
}
