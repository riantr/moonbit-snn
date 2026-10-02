// lstm_ddpg_update.mbt — LSTM_DDPG BPTT-driven critic parameter update (v0.66.0).
//
// Parallel to gru_ddpg_update_critic_seq (v0.64.0) but for the LSTM
// variant. Closes the BPTT-driven parameter update half of v0.63.0
// LSTM_DDPG.
//
// Pipeline for one critic update (T-step BPTT):
//   1. T-step forward through the LSTM critic, storing per-step
//      (x_proj, hidden, cell, cache_lstm) for BPTT.
//   2. Compute per-step TD targets via `lstm_ddpg_compute_td_target_seq`.
//   3. T-step BPTT backward through critic1:
//        d_q_t = 2 * (q_t - q_target_t)
//        d_hidden_t = d_hidden_{t+1} + W2^T · d_q_t
//        d_cell_t = d_cell_{t+1} + (cell-state gradient from output gate)
//        d_x_proj_t = ReLU'(x_proj_t) ⊙ d_hidden_t
//        accumulate d_mlp_w1 / d_mlp_b1 / d_mlp_w2 / d_mlp_b2
//        lstm_cell_backward propagates through 4 gates + cell state,
//        accumulating d_w_f / d_w_i / d_w_c / d_w_o + biases.
//   4. SGD step on critic params + accumulated LSTM grads.
//
// Reuses `lstm_cell_backward` (lstm_cell.mbt v0.29.0) and
// `LstmCellGrad::zero` for the LSTM gradient buffer.

///|
/// Per-critic gradient buffer matching the shape of an
/// `LSTMQNetworkContinuous`.
pub struct LSTMDDPGCriticGrad {
  mlp_w1 : Array[Array[Float]]
  mlp_b1 : Array[Float]
  mlp_w2 : Array[Array[Float]]
  mut mlp_b2 : Float
  lstm_grad : LstmCellGrad
}

///|
/// Build a fresh `LSTMDDPGCriticGrad` matching the shape of the
/// supplied `LSTMQNetworkContinuous`. Caller is responsible for
/// zero-initialising once per training step.
pub fn LSTMDDPGCriticGrad::zero(
  qnet : LSTMQNetworkContinuous,
) -> LSTMDDPGCriticGrad {
  let s_dim = qnet.state_dim
  let a_dim = qnet.action_dim
  let h_dim = qnet.hidden
  let in_dim = s_dim + a_dim
  let zw1 : Array[Array[Float]] = Array::make(h_dim, [])
  let zb1 : Array[Float] = Array::make(h_dim, 0.0F)
  let zw2 : Array[Array[Float]] = Array::make(1, [])
  zw2[0] = Array::make(h_dim, 0.0F)
  for i in 0..<h_dim {
    zw1[i] = Array::make(in_dim, 0.0F)
  }
  let lstm_grad = LstmCellGrad::zero(qnet.lstm)
  { mlp_w1: zw1, mlp_b1: zb1, mlp_w2: zw2, mlp_b2: 0.0F, lstm_grad }
}

///|
/// Update critic1 of an LSTM_DDPG agent via T-step BPTT on a sampled
/// (state_seq, action_seq) window. Returns the mean absolute TD error
/// across the T slots. Updates critic1 in place via simple SGD.
///
/// `td_seq` provides the per-step TD targets (length T). Caller
/// computes `td_seq` via `lstm_ddpg_compute_td_target_seq` from the
/// agent's twin target networks.
pub fn lstm_ddpg_update_critic_seq(
  agent : LSTM_DDPG,
  state_seq : Array[Float],
  action_seq : Array[Float],
  hidden_init : Array[Float],
  cell_init : Array[Float],
  td_seq : Array[Float],
  lr : Float,
) -> Float {
  let seq_len = action_seq.length()
  let s_dim = agent.actor.state_dim
  let a_dim = agent.actor.action_dim
  let h_dim = agent.actor.lstm_hidden
  // Allocate per-step cache storage.
  let x_proj_seq : Array[Float] = Array::make(seq_len * h_dim, 0.0F)
  let hidden_seq : Array[Float] = Array::make(seq_len * h_dim, 0.0F)
  let cell_seq : Array[Float] = Array::make(seq_len * h_dim, 0.0F)
  let lstm_cache_seq : Array[LstmCellCache] = Array::make(seq_len, {
    x: Array::make(h_dim, 0.0F), h_prev: Array::make(h_dim, 0.0F),
    c_prev: Array::make(h_dim, 0.0F), f: Array::make(h_dim, 0.0F),
    ig: Array::make(h_dim, 0.0F), c_tilde: Array::make(h_dim, 0.0F),
    o: Array::make(h_dim, 0.0F), c_t: Array::make(h_dim, 0.0F),
    tanh_c_t: Array::make(h_dim, 0.0F), h_t: Array::make(h_dim, 0.0F),
  })
  // Step 1: forward through critic1 (source).
  let q_seq : Array[Float] = Array::make(seq_len, 0.0F)
  let mut hidden = hidden_init
  let mut cell = cell_init
  for t in 0..<seq_len {
    let st_off = t * s_dim
    let at_off = t * a_dim
    let state : Array[Float] = Array::make(s_dim, 0.0F)
    let action : Array[Float] = Array::make(a_dim, 0.0F)
    for k in 0..<s_dim {
      state[k] = state_seq[st_off + k]
    }
    for k in 0..<a_dim {
      action[k] = action_seq[at_off + k]
    }
    let (q_val, x_proj, cache) = lstm_critic_step_with_cache(
      agent.critic1, state, action, hidden, cell,
    )
    q_seq[t] = q_val
    let xp_off = t * h_dim
    for k in 0..<h_dim {
      x_proj_seq[xp_off + k] = x_proj[k]
      hidden_seq[xp_off + k] = cache.h_t[k]
      cell_seq[xp_off + k] = cache.c_t[k]
    }
    lstm_cache_seq[t] = cache
    hidden = cache.h_t
    cell = cache.c_t
  }
  // Step 2 + 3: T-step BPTT backward through critic1.
  let mut d_h : Array[Float] = Array::make(h_dim, 0.0F)
  let mut d_c : Array[Float] = Array::make(h_dim, 0.0F)
  let mut total_abs_td = 0.0F
  let grad = LSTMDDPGCriticGrad::zero(agent.critic1)
  for t = seq_len - 1; t >= 0; t = t - 1 {
    let td_err = q_seq[t] - td_seq[t]
    total_abs_td = total_abs_td + (if td_err < 0.0F { -td_err } else { td_err })
    let d_q = 2.0F * td_err
    let d_hidden_from_q : Array[Float] = Array::make(h_dim, 0.0F)
    for k in 0..<h_dim {
      d_hidden_from_q[k] = d_h[k] + agent.critic1.mlp_w2[0][k] * d_q
    }
    let d_x_proj : Array[Float] = Array::make(h_dim, 0.0F)
    let xp_off = t * h_dim
    for k in 0..<h_dim {
      let gate = if x_proj_seq[xp_off + k] > 0.0F { 1.0F } else { 0.0F }
      d_x_proj[k] = d_hidden_from_q[k] * gate
    }
    let st_off = t * s_dim
    let at_off = t * a_dim
    for h_idx in 0..<h_dim {
      grad.mlp_b1[h_idx] = grad.mlp_b1[h_idx] + d_x_proj[h_idx]
      grad.mlp_w2[0][h_idx] = grad.mlp_w2[0][h_idx] + d_q * x_proj_seq[xp_off + h_idx]
      for j in 0..<s_dim {
        grad.mlp_w1[h_idx][j] = grad.mlp_w1[h_idx][j] +
          d_x_proj[h_idx] * state_seq[st_off + j]
      }
      for j in 0..<a_dim {
        grad.mlp_w1[h_idx][s_dim + j] = grad.mlp_w1[h_idx][s_dim + j] +
          d_x_proj[h_idx] * action_seq[at_off + j]
      }
    }
    grad.mlp_b2 = grad.mlp_b2 + d_q
    // Backward through LSTM cell. d_c initially carries the cell-state
    // gradient from the next timestep; here d_c is 0 at the start of
    // each backward step (no cell-state gradient from this layer).
    let cache = lstm_cache_seq[t]
    let (d_h_prev, d_c_prev, _d_x) = lstm_cell_backward(
      cache, d_hidden_from_q, d_c, agent.critic1.lstm, grad.lstm_grad,
    )
    let _ = _d_x
    d_h = d_h_prev
    d_c = d_c_prev
  }
  // Step 4: SGD step on critic params + LSTM grads.
  lstm_critic_apply_sgd(agent.critic1, grad, lr)
  lstm_cell_sgd_step(agent.critic1.lstm, grad.lstm_grad, lr)
  total_abs_td / Float::from_int(seq_len)
}

///|
/// LSTM critic forward step that exposes (q_val, x_proj, cache).
fn lstm_critic_step_with_cache(
  qnet : LSTMQNetworkContinuous,
  state : Array[Float],
  action : Array[Float],
  hidden : Array[Float],
  cell : Array[Float],
) -> (Float, Array[Float], LstmCellCache) {
  let sa = vec_concat(state, action)
  let x_proj_pre = matvec(qnet.mlp_w1, qnet.mlp_b1, sa)
  let x_proj = relu_forward(x_proj_pre)
  let (hidden_next, cache) = lstm_cell_forward(x_proj, hidden, cell, qnet.lstm)
  let q_pre = matvec(qnet.mlp_w2, [qnet.mlp_b2], hidden_next)
  (q_pre[0], x_proj, cache)
}

///|
/// SGD step on a critic's parameters using accumulated BPTT gradients.
fn lstm_critic_apply_sgd(
  qnet : LSTMQNetworkContinuous,
  grad : LSTMDDPGCriticGrad,
  lr : Float,
) -> Unit {
  let h_dim = qnet.hidden
  for h_idx in 0..<h_dim {
    qnet.mlp_b1[h_idx] = qnet.mlp_b1[h_idx] - lr * grad.mlp_b1[h_idx]
    for j in 0..<qnet.mlp_w1[h_idx].length() {
      qnet.mlp_w1[h_idx][j] = qnet.mlp_w1[h_idx][j] - lr * grad.mlp_w1[h_idx][j]
    }
    grad.mlp_w1[h_idx] = Array::make(grad.mlp_w1[h_idx].length(), 0.0F)
    qnet.mlp_w2[0][h_idx] = qnet.mlp_w2[0][h_idx] - lr * grad.mlp_w2[0][h_idx]
    grad.mlp_w2[0][h_idx] = 0.0F
    grad.mlp_b1[h_idx] = 0.0F
  }
  qnet.mlp_b2 = qnet.mlp_b2 - lr * grad.mlp_b2
  grad.mlp_b2 = 0.0F
}