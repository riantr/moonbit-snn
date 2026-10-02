// gru_ddpg_update.mbt — DDPG_GRU BPTT-driven critic parameter update (v0.64.0).
//
// Closes the open loop on the v0.60.0 DDPG_GRU agent: the forward path
// (select_action / compute_td_target_seq / soft_update) was already
// shipped; this version adds the per-step critic parameter update via
// T-step back-propagation through time (BPTT).
//
// Pipeline for one critic update:
//   1. T-step forward through the critic (source) on (state_seq,
//      action_seq), storing per-step (x_proj, hidden, cache_gru) for BPTT.
//   2. Compute the per-step TD target via
//      `ddpg_gru_compute_td_target_seq` (in gru_ddpg.mbt) which reuses
//      critic1_target / critic2_target for the next-step Q.
//   3. T-step BPTT backward through the critic:
//        d_q_seq[t] = 2 * (q_t - q_target_t)
//        d_hidden_t = d_hidden_t+1 + W2^T · d_q_seq[t]
//        d_x_proj_t = ReLU'(x_proj_t) ⊙ d_hidden_t
//        SGD step on mlp params at slot t (w1 / b1 / w2 / b2)
//        d_h propagates back through gru_cell_backward (accumulates
//        d_W_z / d_W_r / d_W_n / d_b_z / d_b_r / d_b_n into `grad`).
//   4. SGD step on the accumulated GRU gradients.
//
// Twin-critic symmetric: the same loop runs on critic2, sharing the
// TD targets.
//
// `gru_cell_backward` (gru_cell.mbt v0.30.0), `gru_cell_forward`,
// and `GruCellGrad::zero` are reused from prior versions.

///|
/// Per-critic gradient buffer matching the shape of a `GRUQNetworkContinuous`.
/// Reset to zero before each BPTT step.
pub struct GRUDDPGCriticGrad {
  mlp_w1 : Array[Array[Float]]
  mlp_b1 : Array[Float]
  mlp_w2 : Array[Array[Float]]
  mut mlp_b2 : Float
  gru_grad : GruCellGrad
}

///|
/// Build a fresh `GRUDDPGCriticGrad` matching the shape of the
/// supplied `GRUQNetworkContinuous`. Caller is responsible for
/// zero-initialising once per training step.
pub fn GRUDDPGCriticGrad::zero(qnet : GRUQNetworkContinuous) -> GRUDDPGCriticGrad {
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
  let gru_grad = GruCellGrad::zero(qnet.gru)
  { mlp_w1: zw1, mlp_b1: zb1, mlp_w2: zw2, mlp_b2: 0.0F, gru_grad }
}

///|
/// Update critic1 of a DDPG_GRU agent via T-step BPTT on a sampled
/// (state_seq, action_seq) window. Returns the mean absolute TD error
/// across the T slots (for diagnostics). Updates critic1 in place via
/// simple SGD; the caller is responsible for invoking Adam/TD3-style
/// momentum wrappers externally.
///
/// Mirrors `ddpg_update_critic` from `ddpg_update.mbt` (Batch B) but
/// BPTTs through the GRU hidden state over T steps.
///
/// The `td_seq` argument provides the per-step TD targets (length T).
/// Caller computes `td_seq` via `ddpg_gru_compute_td_target_seq` from
/// the agent's twin target networks.
pub fn gru_ddpg_update_critic_seq(
  agent : DDPG_GRU,
  state_seq : Array[Float],
  action_seq : Array[Float],
  hidden_init : Array[Float],
  td_seq : Array[Float],
  lr : Float,
) -> Float {
  let seq_len = action_seq.length()
  let s_dim = agent.actor.state_dim
  let a_dim = agent.actor.action_dim
  let h_dim = agent.actor.gru_hidden
  // Allocate per-step cache storage.
  let x_proj_seq : Array[Float] = Array::make(seq_len * h_dim, 0.0F)
  let gru_cache_seq : Array[GruCellCache] = Array::make(seq_len, {
    x: Array::make(h_dim, 0.0F), h_prev: Array::make(h_dim, 0.0F),
    z: Array::make(h_dim, 0.0F), r: Array::make(h_dim, 0.0F),
    s: Array::make(h_dim, 0.0F), n: Array::make(h_dim, 0.0F),
    h_t: Array::make(h_dim, 0.0F),
  })
  // Step 1: forward through critic1 (source).
  let q_seq : Array[Float] = Array::make(seq_len, 0.0F)
  let mut hidden = hidden_init
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
    let (q_val, x_proj, cache) = gru_critic_step_with_cache(
      agent.critic1, state, action, hidden,
    )
    q_seq[t] = q_val
    let xp_off = t * h_dim
    for k in 0..<h_dim {
      x_proj_seq[xp_off + k] = x_proj[k]
    }
    gru_cache_seq[t] = cache
    hidden = cache.h_t
  }
  // Step 2 + 3: T-step BPTT backward through critic1.
  let mut d_h : Array[Float] = Array::make(h_dim, 0.0F)
  let mut total_abs_td = 0.0F
  // Initialise gradient accumulator (caller-supplied accumulator via
  // the critic's own internal grad — we mutate in place).
  let grad = GRUDDPGCriticGrad::zero(agent.critic1)
  for t = seq_len - 1; t >= 0; t = t - 1 {
    let td_err = q_seq[t] - td_seq[t]
    total_abs_td = total_abs_td + (if td_err < 0.0F { -td_err } else { td_err })
    let d_q = 2.0F * td_err  // gradient of (q - q_target)^2 / 2
    // d_hidden = d_h + W2^T · d_q
    let d_hidden_from_q : Array[Float] = Array::make(h_dim, 0.0F)
    for k in 0..<h_dim {
      d_hidden_from_q[k] = d_h[k] + agent.critic1.mlp_w2[0][k] * d_q
    }
    // ReLU backward gate on x_proj at slot t.
    let d_x_proj : Array[Float] = Array::make(h_dim, 0.0F)
    let xp_off = t * h_dim
    for k in 0..<h_dim {
      let gate = if x_proj_seq[xp_off + k] > 0.0F { 1.0F } else { 0.0F }
      d_x_proj[k] = d_hidden_from_q[k] * gate
    }
    // Accumulate MLP gradients (slot-t contribution).
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
    // Backward through the GRU cell at slot t. gru_cell_backward
    // accumulates into `grad.gru_grad` and returns (d_h_prev, d_x)
    // — we use d_h_prev to feed the next (earlier) timestep.
    let cache = gru_cache_seq[t]
    let (d_h_prev, _d_x) = gru_cell_backward(
      cache, d_hidden_from_q, agent.critic1.gru, grad.gru_grad,
    )
    let _ = _d_x
    d_h = d_h_prev
  }
  // Step 4: SGD step on critic1 + accumulated GRU grads.
  critic_apply_sgd(agent.critic1, grad, lr)
  apply_gru_sgd_step(agent.critic1.gru, grad.gru_grad, lr)
  total_abs_td / Float::from_int(seq_len)
}

///|
/// Critic forward step that exposes the (q_val, x_proj, cache) triple
/// needed for BPTT. Mirrors `gru_qnetwork_continuous_step` but
/// returns the x_proj pre-activation vector AND the gru_cell cache.
fn gru_critic_step_with_cache(
  qnet : GRUQNetworkContinuous,
  state : Array[Float],
  action : Array[Float],
  hidden : Array[Float],
) -> (Float, Array[Float], GruCellCache) {
  let sa = vec_concat(state, action)
  let x_proj_pre = matvec(qnet.mlp_w1, qnet.mlp_b1, sa)
  let x_proj = relu_forward(x_proj_pre)
  let (hidden_next, cache) = gru_cell_forward(x_proj, hidden, qnet.gru)
  let q_pre = matvec(qnet.mlp_w2, [qnet.mlp_b2], hidden_next)
  (q_pre[0], x_proj, cache)
}

///|
/// SGD step on a critic's parameters using accumulated BPTT gradients.
fn critic_apply_sgd(
  qnet : GRUQNetworkContinuous,
  grad : GRUDDPGCriticGrad,
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

///|
/// Apply accumulated gradients to GRU cell parameters with simple SGD.
/// Mirrors `gru_cell_sgd_step` (gru_cell.mbt) but accepts the
/// gradient buffer from the caller instead of zeroing itself.
fn apply_gru_sgd_step(
  param : GruCellParam,
  grad : GruCellGrad,
  lr : Float,
) -> Unit {
  for i in 0..<param.d_h {
    let in_dim = param.w_z[i].length()
    for j in 0..<in_dim {
      param.w_z[i][j] = param.w_z[i][j] - lr * grad.d_w_z[i][j]
      param.w_r[i][j] = param.w_r[i][j] - lr * grad.d_w_r[i][j]
      param.w_n[i][j] = param.w_n[i][j] - lr * grad.d_w_n[i][j]
    }
    param.b_z[i] = param.b_z[i] - lr * grad.d_b_z[i]
    param.b_r[i] = param.b_r[i] - lr * grad.d_b_r[i]
    param.b_n[i] = param.b_n[i] - lr * grad.d_b_n[i]
  }
}