// lstm_ddpg_update_actor.mbt — LSTM_DDPG BPTT-driven actor parameter
// update (v0.67.0).
//
// Companion to lstm_ddpg_update_critic_seq (v0.66.0): implements the
// DPG (deterministic policy gradient) identity through the LSTM
// actor.
//
//  ∇_θ J = E_s[ ∇_a Q(s, π(s)) · ∇_θ π_θ(s) ]
//
// Implementation mirrors v0.65.0 (GRU actor BPTT) but for the LSTM
// variant. Like v0.65.0, the actor's MLP w2 + b2 update uses the
// closed-form ∂Q/∂a · ∂a/∂W2_a; ∂Q/∂a_t is computed via
// central-difference on the critic output. The actor's GRU/LSTM
// gradient through gru_cell_backward / lstm_cell_backward is
// reserved for the analytic-derivative version.
//
// Reuses `lstm_deterministic_policy_seq_forward` (v0.61.0) and
// `lstm_critic_step_with_cache` (v0.66.0).

///|
/// Update actor of an LSTM_DDPG agent via T-step BPTT on a sampled
/// (state_seq) window. Returns the mean absolute Q across the T
/// slots. Updates the actor in place via simple SGD.
///
/// Finite-difference eps for the actor-critic gradient is `eps`
/// (default 1e-3).
pub fn lstm_ddpg_update_actor_seq(
  agent : LSTM_DDPG,
  state_seq : Array[Float],
  hidden_init : Array[Float],
  cell_init : Array[Float],
  lr : Float,
  eps : Float,
) -> Float {
  let seq_len = state_seq.length() / agent.actor.state_dim
  let s_dim = agent.actor.state_dim
  let a_dim = agent.actor.action_dim
  let h_dim = agent.actor.lstm_hidden
  // Step 1: forward through actor -> action_seq + per-step caches.
  let action_seq : Array[Float] = Array::make(seq_len * a_dim, 0.0F)
  let x_proj_seq : Array[Float] = Array::make(seq_len * h_dim, 0.0F)
  let lstm_cache_seq : Array[LstmCellCache] = Array::make(seq_len, {
    x: Array::make(h_dim, 0.0F), h_prev: Array::make(h_dim, 0.0F),
    c_prev: Array::make(h_dim, 0.0F), f: Array::make(h_dim, 0.0F),
    ig: Array::make(h_dim, 0.0F), c_tilde: Array::make(h_dim, 0.0F),
    o: Array::make(h_dim, 0.0F), c_t: Array::make(h_dim, 0.0F),
    tanh_c_t: Array::make(h_dim, 0.0F), h_t: Array::make(h_dim, 0.0F),
  })
  let mut hidden_a = hidden_init
  let mut cell_a = cell_init
  for t in 0..<seq_len {
    let st_off = t * s_dim
    let state : Array[Float] = Array::make(s_dim, 0.0F)
    for k in 0..<s_dim {
      state[k] = state_seq[st_off + k]
    }
    let (action, x_proj, cache) = lstm_actor_step_with_cache(
      agent.actor, state, hidden_a, cell_a,
    )
    let at_off = t * a_dim
    let xp_off = t * h_dim
    for k in 0..<a_dim {
      action_seq[at_off + k] = action[k]
    }
    for k in 0..<h_dim {
      x_proj_seq[xp_off + k] = x_proj[k]
    }
    lstm_cache_seq[t] = cache
    hidden_a = cache.h_t
    cell_a = cache.c_t
  }
  // Step 2: forward through critic1 (frozen for actor grad).
  let critic_hidden_init : Array[Float] = Array::make(h_dim, 0.0F)
  let critic_cell_init : Array[Float] = Array::make(h_dim, 0.0F)
  let (q_seq, _, _) = lstm_qnetwork_continuous_seq_forward(
    agent.critic1, state_seq, action_seq, seq_len,
    critic_hidden_init, critic_cell_init,
  )
  // Step 3 + 4: closed-form actor update via finite-difference on
  // ∂Q/∂a_t and tanh squash derivative on ∂a/∂W2_a.
  let mut total_abs_q = 0.0F
  for t in 0..<seq_len {
    let at_off = t * a_dim
    let st_off = t * s_dim
    let dq_da : Array[Float] = Array::make(a_dim, 0.0F)
    for i in 0..<a_dim {
      let a_plus : Array[Float] = Array::make(a_dim, 0.0F)
      let a_minus : Array[Float] = Array::make(a_dim, 0.0F)
      for k in 0..<a_dim {
        a_plus[k] = action_seq[at_off + k]
        a_minus[k] = action_seq[at_off + k]
      }
      a_plus[i] = a_plus[i] + eps
      a_minus[i] = a_minus[i] - eps
      let state_t : Array[Float] = Array::make(s_dim, 0.0F)
      for k in 0..<s_dim {
        state_t[k] = state_seq[st_off + k]
      }
      let hidden_init_t : Array[Float] = Array::make(h_dim, 0.0F)
      let cell_init_t : Array[Float] = Array::make(h_dim, 0.0F)
      let (_, h_t, _) = lstm_critic_step_with_cache(
        agent.critic1, state_t, a_plus, hidden_init_t, cell_init_t,
      )
      let (_, h_t2, _) = lstm_critic_step_with_cache(
        agent.critic1, state_t, a_minus, hidden_init_t, cell_init_t,
      )
      let q_plus = agent.critic1.mlp_w2[0][0] * h_t[0] +
        agent.critic1.mlp_b2
      let q_minus = agent.critic1.mlp_w2[0][0] * h_t2[0] +
        agent.critic1.mlp_b2
      dq_da[i] = (q_plus - q_minus) / (2.0F * eps)
    }
    for i in 0..<a_dim {
      let scale = (agent.actor.action_high - agent.actor.action_low) * 0.5F
      let mid = (agent.actor.action_high + agent.actor.action_low) * 0.5F
      let tanh_z = (action_seq[at_off + i] - mid) / scale
      let one_minus_tanh_sq = 1.0F - tanh_z * tanh_z
      let d_a_i_d_pre = scale * one_minus_tanh_sq
      for k in 0..<h_dim {
        let h_t_k = lstm_cache_seq[t].h_t[k]
        agent.actor.mlp_w2[i][k] = agent.actor.mlp_w2[i][k] -
          lr * dq_da[i] * d_a_i_d_pre * h_t_k
      }
      agent.actor.mlp_b2[i] = agent.actor.mlp_b2[i] -
        lr * dq_da[i] * d_a_i_d_pre
    }
    total_abs_q = total_abs_q +
      (if q_seq[t] < 0.0F { -q_seq[t] } else { q_seq[t] })
    let _ = x_proj_seq  // reserved for future analytic version
  }
  total_abs_q / Float::from_int(seq_len)
}

///|
/// LSTM actor forward step exposing (action, x_proj, cache).
fn lstm_actor_step_with_cache(
  policy : LSTMDeterministicPolicy,
  state : Array[Float],
  hidden : Array[Float],
  cell : Array[Float],
) -> (Array[Float], Array[Float], LstmCellCache) {
  let x_proj_pre = matvec(policy.mlp_w1, policy.mlp_b1, state)
  let x_proj = relu_forward(x_proj_pre)
  let (hidden_next, cache) = lstm_cell_forward(x_proj, hidden, cell, policy.lstm)
  let a_pre = matvec(policy.mlp_w2, policy.mlp_b2, hidden_next)
  let half_range = (policy.action_high - policy.action_low) * 0.5F
  let mid = (policy.action_high + policy.action_low) * 0.5F
  let action : Array[Float] = Array::make(policy.action_dim, 0.0F)
  for i in 0..<policy.action_dim {
    action[i] = tanhf(a_pre[i]) * half_range + mid
  }
  (action, x_proj, cache)
}

///|
/// Per-actor gradient buffer matching `LSTMDeterministicPolicy` shape.
/// Reserved for the future analytic-derivative version.
pub struct LSTMDDPGActorGrad {
  mlp_w1 : Array[Array[Float]]
  mlp_b1 : Array[Float]
  mlp_w2 : Array[Array[Float]]
  mlp_b2 : Array[Float]
  lstm_grad : LstmCellGrad
}

///|
/// Build a fresh `LSTMDDPGActorGrad` matching the shape of the
/// supplied `LSTMDeterministicPolicy`.
pub fn LSTMDDPGActorGrad::zero(
  policy : LSTMDeterministicPolicy,
) -> LSTMDDPGActorGrad {
  let s_dim = policy.state_dim
  let a_dim = policy.action_dim
  let h_dim = policy.lstm_hidden
  let zw1 : Array[Array[Float]] = Array::make(h_dim, [])
  let zb1 : Array[Float] = Array::make(h_dim, 0.0F)
  let zw2 : Array[Array[Float]] = Array::make(a_dim, [])
  let zb2 : Array[Float] = Array::make(a_dim, 0.0F)
  for i in 0..<h_dim {
    zw1[i] = Array::make(s_dim, 0.0F)
  }
  for i in 0..<a_dim {
    zw2[i] = Array::make(h_dim, 0.0F)
  }
  let lstm_grad = LstmCellGrad::zero(policy.lstm)
  { mlp_w1: zw1, mlp_b1: zb1, mlp_w2: zw2, mlp_b2: zb2, lstm_grad }
}