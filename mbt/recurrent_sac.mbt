// recurrent_sac.mbt — SAC with LSTM actor + LSTM critic (v0.38.2).
//
// Extension of v0.38.0 (vanilla SAC) and v0.38.1 (auto-tuned α) for
// partial observability. The POMDP corridor is the test bed: the
// agent only sees an indicator at t=0 (which side has the goal) and
// its current position; it must remember the indicator across the
// episode via the LSTM.
//
// Architecture:
//   - Actor  : LSTM cell → w_out_actor → softmax(logits) = π(a|h_t)
//   - Critic : LSTM cell → w_out_q → Q(h_t, a)  (one Q value per action)
//   - Twin Q + target Q with Polyak averaging (same as v0.38.0)
//   - Auto-tuned α from v0.38.1
//
// Forward path (per episode):
//   obs_seq = [obs_0, ..., obs_{T-1}]
//   h0, c0 = 0
//   for t in 0..T:
//     h_t = LSTM(obs_t, h_{t-1}, c_{t-1})
//     π_t = softmax(w_a · h_t + b_a)
//     Q1_t[i] = w_q1[i] · h_t + b_q1[i]   (for all i ∈ actions)
//     Q2_t[i] = w_q2[i] · h_t + b_q2[i]
//
// Soft target (per time step, for the critic update):
//   target_t = r_t + γ · (1 − done_t) · Σ_{a'} π_{t+1}(a'|s_{t+1}) · [Q̂_min(s_{t+1}, a') − α · log π_{t+1}(a'|s_{t+1})]
// where Q̂_min = min(Q̂1_target, Q̂2_target).
//
// Note: recurrent SAC uses BPTT through the whole episode for actor
// and critic updates — no replay buffer (per-agent-state). This is
// the standard on-policy recurrent RL formulation.

///|
/// LSTM Q-net: cell + linear head producing one Q value per action.
pub struct LstmQNet {
  cell : LstmCellParam
  w_out : Array[Array[Float]
]  // n_actions × d_h
  b_out : Array[Float]
}

///|
pub fn LstmQNet::new(
  d_x : Int,
  d_h : Int,
  n_actions : Int,
  seed : UInt64,
) -> LstmQNet {
  let cell = LstmCellParam::new(d_x, d_h, seed)
  let std = sqrtf(1.0F / Float::from_int(d_h))
  let rng = Xoshiro::from_state(seed + 11UL, seed + 12UL, seed + 13UL, seed + 14UL)
  let w_out = xavier_normal(n_actions, d_h, std, rng)
  let b_out : Array[Float] = Array::make(n_actions, 0.0F)
  { cell, w_out, b_out }
}

///|
/// Cache from one forward pass through an LstmQNet.
pub struct LstmQNetCache {
  cell_caches : Array[LstmCellCache]
  qs : Array[Array[Float]]
  hs : Array[Array[Float]]
}

///|
/// Forward pass through the Q-net over a sequence of observations.
/// Returns the Q-values per time step + a cache for BPTT.
pub fn lstm_qnet_forward(
  qnet : LstmQNet,
  obs_seq : Array[Array[Float]],
  h0 : Array[Float],
  c0 : Array[Float],
) -> (Array[Array[Float]], LstmQNetCache) {
  let n = obs_seq.length()
  let n_actions = qnet.w_out.length()
  let cell_caches : Array[LstmCellCache] = []
  let qs : Array[Array[Float]] = []
  let hs : Array[Array[Float]] = []
  let mut cur_h = h0
  let mut cur_c = c0
  for t in 0..<n {
    let (h_t, cache_t) = lstm_cell_forward(obs_seq[t], cur_h, cur_c, qnet.cell)
    let q_t : Array[Float] = Array::make(n_actions, 0.0F)
    for i in 0..<n_actions {
      let row = qnet.w_out[i]
      let mut s = qnet.b_out[i]
      for j in 0..<row.length() {
        s = s + row[j] * h_t[j]
      }
      q_t[i] = s
    }
    cell_caches.push(cache_t)
    qs.push(q_t)
    hs.push(h_t)
    cur_h = h_t
    cur_c = cache_t.c_t
  }
  (qs, { cell_caches, qs, hs })
}

///|
/// Recurrent SAC agent.
pub struct RecurrentSac {
  policy : LstmPolicy
  q1 : LstmQNet
  q2 : LstmQNet
  q1_target : LstmQNet
  q2_target : LstmQNet
  /// log of entropy temperature; α = exp(log_alpha).
  mut log_alpha : Float
  /// Target for E[Σ_a π(a|s) log π(a|s)]; typically negative.
  target_entropy : Float
  /// Learning rate for the log_alpha update.
  alpha_lr : Float
}

///|
pub fn RecurrentSac::new(
  n_cells : Int,
  d_h : Int,
  log_alpha_init : Float,
  target_entropy : Float,
  alpha_lr : Float,
  seed : UInt64,
) -> RecurrentSac {
  let n_actions = 2
  let policy = LstmPolicy::new(n_cells, d_h, seed)
  let d_x = 2 + n_cells
  let q1 = LstmQNet::new(d_x, d_h, n_actions, seed + 1UL)
  let q2 = LstmQNet::new(d_x, d_h, n_actions, seed + 2UL)
  let q1_target = LstmQNet::new(d_x, d_h, n_actions, seed + 3UL)
  let q2_target = LstmQNet::new(d_x, d_h, n_actions, seed + 4UL)
  // Sync target = online initially.
  copy_lstm_qnet(q1_target, q1)
  copy_lstm_qnet(q2_target, q2)
  { policy, q1, q2, q1_target, q2_target, log_alpha: log_alpha_init, target_entropy, alpha_lr }
}

///|
/// Current α = exp(log_alpha).
pub fn recurrent_sac_get_alpha(sac : RecurrentSac) -> Float {
  expf(sac.log_alpha)
}

///|
/// Episode trace for recurrent SAC. Stores obs_seq, next_obs_seq,
/// actions, rewards, dones for both actor and critic updates.
pub(all) struct RecurrentSacEpisode {
  obs_seq : Array[Array[Float]]
  next_obs_seq : Array[Array[Float]]
  actions : Array[Int]
  rewards : Array[Float]
  dones : Array[Bool]
  initial_pos : Int
  goal_side : Int
}

///|
/// Convenience constructor for synthetic episodes (tests).
pub fn RecurrentSacEpisode::new(
  obs_seq : Array[Array[Float]],
  next_obs_seq : Array[Array[Float]],
  actions : Array[Int],
  rewards : Array[Float],
  dones : Array[Bool],
  initial_pos : Int,
  goal_side : Int,
) -> RecurrentSacEpisode {
  { obs_seq, next_obs_seq, actions, rewards, dones, initial_pos, goal_side }
}

///|
/// Roll out one episode on the corridor POMDP, returning the trace.
pub fn recurrent_sac_rollout_episode(
  env_n_cells : Int,
  sac : RecurrentSac,
  max_steps : Int,
  rng : Xoshiro,
) -> RecurrentSacEpisode {
  let (u_raw, _) = box_muller(rng)
  let side = if u_raw > 0.0 { 1 } else { 0 }
  let env : CorridorEnv = CorridorEnv::{
    n_cells: env_n_cells,
    goal_side: side,
    step_penalty: -0.1F,
    goal_reward: 1.0F,
    max_steps,
  }
  let start = env_n_cells / 2
  let obs_seq : Array[Array[Float]] = []
  let next_obs_seq : Array[Array[Float]] = []
  let actions : Array[Int] = []
  let rewards : Array[Float] = []
  let dones : Array[Bool] = []
  let mut pos = start
  let mut done = false
  let mut t = 0
  // Push first obs.
  obs_seq.push(corridor_observation(env, pos))
  while !done && t < max_steps {
    // Sample action from current policy using running hidden state.
    let h_t : Array[Float] = Array::make(sac.policy.cell.d_h, 0.0F)
    let c_t : Array[Float] = Array::make(sac.policy.cell.d_h, 0.0F)
    // Re-run policy from start up to t to get current hidden state.
    // (Cheaper: maintain hidden state across the loop.)
    let _ = h_t
    let _ = c_t
    // Re-derive by re-running forward from t=0..t.
    let partial_obs : Array[Array[Float]] = []
    for k in 0..<obs_seq.length() {
      partial_obs.push(obs_seq[k])
    }
    let h0 : Array[Float] = Array::make(sac.policy.cell.d_h, 0.0F)
    let c0 : Array[Float] = Array::make(sac.policy.cell.d_h, 0.0F)
    let (acts, cache) = lstm_policy_forward(sac.policy, partial_obs, h0, c0, rng)
    let _ = cache
    let a = acts[acts.length() - 1]
    let (new_pos, r, d) = env.step(pos, a)
    actions.push(a)
    rewards.push(r)
    dones.push(d)
    if !d && t + 1 < max_steps {
      next_obs_seq.push(corridor_observation(env, new_pos))
    } else {
      // For terminal state, push a zero observation (not used in target
      // since done=True short-circuits).
      let z : Array[Float] = Array::make(2 + env_n_cells, 0.0F)
      next_obs_seq.push(z)
    }
    pos = new_pos
    done = d
    t = t + 1
    if !done && t < max_steps {
      obs_seq.push(corridor_observation(env, pos))
    }
  }
  { obs_seq, next_obs_seq, actions, rewards, dones, initial_pos: start, goal_side: side }
}

///|
/// Compute the soft Bellman target sequence for an episode.
/// For terminal states, target = r. For non-terminal, uses
/// π_{t+1} from policy forward over next_obs_seq and Q̂_min from
/// target critics.
pub fn recurrent_sac_soft_target_seq(
  sac : RecurrentSac,
  ep : RecurrentSacEpisode,
  gamma : Float,
  rng : Xoshiro,
) -> Array[Float] {
  let n = ep.actions.length()
  let n_a = 2
  let targets : Array[Float] = Array::make(n, 0.0F)
  // Forward pass policy on next_obs_seq to get π_{t+1}.
  let h0 : Array[Float] = Array::make(sac.policy.cell.d_h, 0.0F)
  let c0 : Array[Float] = Array::make(sac.policy.cell.d_h, 0.0F)
  let (_, policy_cache) = lstm_policy_forward(sac.policy, ep.next_obs_seq, h0, c0, rng)
  // Forward pass target critics on next_obs_seq.
  let (_, q1_target_cache) = lstm_qnet_forward(sac.q1_target, ep.next_obs_seq, h0, c0)
  let (_, q2_target_cache) = lstm_qnet_forward(sac.q2_target, ep.next_obs_seq, h0, c0)
  let alpha = recurrent_sac_get_alpha(sac)
  for t in 0..<n {
    if ep.dones[t] {
      targets[t] = ep.rewards[t]
    } else {
      let pi_t = policy_cache.probs[t]
      let q1t = q1_target_cache.qs[t]
      let q2t = q2_target_cache.qs[t]
      let mut expected = 0.0F
      for i in 0..<n_a {
        let q_min = if q1t[i] < q2t[i] { q1t[i] } else { q2t[i] }
        let lp = if pi_t[i] > 0.0F { logf(pi_t[i]) } else { -20.0F }
        expected = expected + pi_t[i] * (q_min - alpha * lp)
      }
      targets[t] = ep.rewards[t] + gamma * expected
    }
  }
  targets
}

///|
/// Critic MSE update via BPTT. Applies SGD on q1 and q2 (online
/// critics only; target critics updated separately via Polyak).
/// Returns mean squared TD error.
pub fn recurrent_sac_critic_update(
  sac : RecurrentSac,
  ep : RecurrentSacEpisode,
  gamma : Float,
  lr : Float,
  rng : Xoshiro,
) -> Float {
  let targets = recurrent_sac_soft_target_seq(sac, ep, gamma, rng)
  let h0 : Array[Float] = Array::make(sac.q1.cell.d_h, 0.0F)
  let c0 : Array[Float] = Array::make(sac.q1.cell.d_h, 0.0F)
  let (q1_pred, q1_cache) = lstm_qnet_forward(sac.q1, ep.obs_seq, h0, c0)
  let (q2_pred, q2_cache) = lstm_qnet_forward(sac.q2, ep.obs_seq, h0, c0)
  let n = ep.actions.length()
  let mut total_loss = 0.0F
  for t in 0..<n {
    let a = ep.actions[t]
    let target = targets[t]
    let err1 = q1_pred[t][a] - target
    let err2 = q2_pred[t][a] - target
    total_loss = total_loss + err1 * err1 + err2 * err2
  }
  // Now do BPTT for q1 and q2.
  let n_actions = sac.q1.w_out.length()
  let d_h = sac.q1.cell.d_h
  // Compute per-step d_logits (only action a is non-zero).
  let d_q1_logits_per_t : Array[Array[Float]] = Array::make(n, [])
  let d_q2_logits_per_t : Array[Array[Float]] = Array::make(n, [])
  for t in 0..<n {
    let a = ep.actions[t]
    let target = targets[t]
    let err1 = q1_pred[t][a] - target
    let err2 = q2_pred[t][a] - target
    let d1 : Array[Float] = Array::make(n_actions, 0.0F)
    let d2 : Array[Float] = Array::make(n_actions, 0.0F)
    d1[a] = 2.0F * err1
    d2[a] = 2.0F * err2
    d_q1_logits_per_t[t] = d1
    d_q2_logits_per_t[t] = d2
  }
  // Apply gradients to q1.
  apply_lstm_qnet_grad(
    sac.q1, q1_cache, d_q1_logits_per_t, lr,
  )
  // Apply gradients to q2.
  apply_lstm_qnet_grad(
    sac.q2, q2_cache, d_q2_logits_per_t, lr,
  )
  let _ = d_h
  if n > 0 {
    total_loss / Float::from_int(n)
  } else {
    0.0F
  }
}

///|
/// Apply accumulated gradients to an LstmQNet (output projection
/// + LSTM cell weights/biases) via SGD step. Mirrors the helper
/// used by `lstm_policy_gradient_update` for the actor.
fn apply_lstm_qnet_grad(
  qnet : LstmQNet,
  cache : LstmQNetCache,
  d_logits_per_t : Array[Array[Float]],
  lr : Float,
) -> Unit {
  let n = cache.hs.length()
  let n_actions = qnet.w_out.length()
  let d_h = qnet.cell.d_h
  // 1) Output projection gradients.
  for t in 0..<n {
    let d_logits = d_logits_per_t[t]
    for i in 0..<n_actions {
      let row = qnet.w_out[i]
      for j in 0..<d_h {
        row[j] = row[j] - lr * d_logits[i] * cache.hs[t][j]
      }
      qnet.b_out[i] = qnet.b_out[i] - lr * d_logits[i]
    }
  }
  // 2) LSTM cell BPTT.
  // d_h_per_t[t] = contribution from output projection.
  let d_h_per_t : Array[Array[Float]] = Array::make(n, [])
  for t in 0..<n {
    d_h_per_t[t] = Array::make(d_h, 0.0F)
    let d_logits = d_logits_per_t[t]
    for i in 0..<n_actions {
      for j in 0..<d_h {
        d_h_per_t[t][j] = d_h_per_t[t][j] + qnet.w_out[i][j] * d_logits[i]
      }
    }
  }
  let d_h_prev_global : Array[Float] = Array::make(d_h, 0.0F)
  let d_c : Array[Float] = Array::make(d_h, 0.0F)
  let cell_grad = LstmCellGrad::zero(qnet.cell)
  for t_step in 0..<n {
    let t = n - 1 - t_step
    if t < n - 1 {
      for j in 0..<d_h {
        d_h_per_t[t][j] = d_h_per_t[t][j] + d_h_prev_global[j]
      }
    }
    let (d_h_prev, _d_x, d_c_prev) = lstm_cell_backward(
      cache.cell_caches[t], d_h_per_t[t], d_c, qnet.cell, cell_grad,
    )
    for j in 0..<d_h {
      d_h_prev_global[j] = d_h_prev[j]
      d_c[j] = d_c_prev[j]
    }
  }
  // 3) Apply LSTM cell gradients.
  for i in 0..<qnet.cell.d_h {
    let in_dim = qnet.cell.w_f[i].length()
    for j in 0..<in_dim {
      qnet.cell.w_f[i][j] = qnet.cell.w_f[i][j] + lr * cell_grad.d_w_f[i][j]
      qnet.cell.w_i[i][j] = qnet.cell.w_i[i][j] + lr * cell_grad.d_w_i[i][j]
      qnet.cell.w_c[i][j] = qnet.cell.w_c[i][j] + lr * cell_grad.d_w_c[i][j]
      qnet.cell.w_o[i][j] = qnet.cell.w_o[i][j] + lr * cell_grad.d_w_o[i][j]
      cell_grad.d_w_f[i][j] = 0.0F
      cell_grad.d_w_i[i][j] = 0.0F
      cell_grad.d_w_c[i][j] = 0.0F
      cell_grad.d_w_o[i][j] = 0.0F
    }
    qnet.cell.b_f[i] = qnet.cell.b_f[i] + lr * cell_grad.d_b_f[i]
    qnet.cell.b_i[i] = qnet.cell.b_i[i] + lr * cell_grad.d_b_i[i]
    qnet.cell.b_c[i] = qnet.cell.b_c[i] + lr * cell_grad.d_b_c[i]
    qnet.cell.b_o[i] = qnet.cell.b_o[i] + lr * cell_grad.d_b_o[i]
    cell_grad.d_b_f[i] = 0.0F
    cell_grad.d_b_i[i] = 0.0F
    cell_grad.d_b_c[i] = 0.0F
    cell_grad.d_b_o[i] = 0.0F
  }
}

///|
/// Actor update via BPTT. Computes the analytic gradient of
///   L_actor = Σ_t Σ_a π_t(a) · [α · log π_t(a) − Q1(s_t, a)]
/// and applies SGD to the actor's w_out + LSTM cell.
pub fn recurrent_sac_actor_update(
  sac : RecurrentSac,
  ep : RecurrentSacEpisode,
  lr : Float,
  rng : Xoshiro,
) -> Unit {
  let n = ep.actions.length()
  let n_actions = sac.policy.w_out.length()
  let d_h = sac.policy.cell.d_h
  let h0 : Array[Float] = Array::make(sac.policy.cell.d_h, 0.0F)
  let c0 : Array[Float] = Array::make(sac.policy.cell.d_h, 0.0F)
  // Forward pass policy and q1 (critic) over obs_seq.
  let (_, policy_cache) = lstm_policy_forward(sac.policy, ep.obs_seq, h0, c0, rng)
  let (_, q1_cache) = lstm_qnet_forward(sac.q1, ep.obs_seq, h0, c0)
  let alpha = recurrent_sac_get_alpha(sac)
  // Per-step d_logits for actor (gradient of -Q + α·H wrt logits).
  // For each time step, for each action a:
  //   d_logits[i] = Σ_a π(a) · (indicator_{i=a} − π(i)) · (−Q[i] + α · (log π(i) + 1))
  // = −(Q(i) − Σ_a Q(a)π(a)) · (1 − π(i)) + Σ_{a≠i} ... actually simpler form:
  //   d_logits[i] = Σ_a π(a) · (1{i=a} − π(i)) · (−Q(a) + α·(log π(i) + 1))
  // Let v_i = Q(i) − α·(log π(i) + 1). Then d_logits[i] = Σ_a π(a) · (1{i=a} − π(i)) · (−v_a)
  //                                  = −Σ_a π(a) · 1{i=a} · v_a + Σ_a π(a)² · v_a
  //                                  = −π(i) · v_i + π(i) · Σ_a π(a) · v_a
  //                                  = π(i) · (Σ_a π(a) · v_a − v_i)
  let d_logits_per_t : Array[Array[Float]] = Array::make(n, [])
  for t in 0..<n {
    let pi = policy_cache.probs[t]
    let q = q1_cache.qs[t]
    let mut v_mean = 0.0F
    let v_arr : Array[Float] = Array::make(n_actions, 0.0F)
    for i in 0..<n_actions {
      let lp = if pi[i] > 0.0F { logf(pi[i]) } else { -20.0F }
      v_arr[i] = q[i] - alpha * (lp + 1.0F)
      v_mean = v_mean + pi[i] * v_arr[i]
    }
    let d_logits : Array[Float] = Array::make(n_actions, 0.0F)
    for i in 0..<n_actions {
      d_logits[i] = pi[i] * (v_mean - v_arr[i])
    }
    d_logits_per_t[t] = d_logits
  }
  // Apply gradients to w_out + LSTM cell.
  for t in 0..<n {
    let d_logits = d_logits_per_t[t]
    for i in 0..<n_actions {
      let row = sac.policy.w_out[i]
      for j in 0..<d_h {
        row[j] = row[j] - lr * d_logits[i] * policy_cache.hs[t][j]
      }
      sac.policy.b_out[i] = sac.policy.b_out[i] - lr * d_logits[i]
    }
  }
  // BPTT through LSTM cell.
  let d_h_per_t : Array[Array[Float]] = Array::make(n, [])
  for t in 0..<n {
    d_h_per_t[t] = Array::make(d_h, 0.0F)
    let d_logits = d_logits_per_t[t]
    for i in 0..<n_actions {
      for j in 0..<d_h {
        d_h_per_t[t][j] = d_h_per_t[t][j] + sac.policy.w_out[i][j] * d_logits[i]
      }
    }
  }
  let d_h_prev_global : Array[Float] = Array::make(d_h, 0.0F)
  let d_c : Array[Float] = Array::make(d_h, 0.0F)
  let cell_grad = LstmCellGrad::zero(sac.policy.cell)
  for t_step in 0..<n {
    let t = n - 1 - t_step
    if t < n - 1 {
      for j in 0..<d_h {
        d_h_per_t[t][j] = d_h_per_t[t][j] + d_h_prev_global[j]
      }
    }
    let (d_h_prev, _d_x, d_c_prev) = lstm_cell_backward(
      policy_cache.cell_caches[t], d_h_per_t[t], d_c, sac.policy.cell, cell_grad,
    )
    for j in 0..<d_h {
      d_h_prev_global[j] = d_h_prev[j]
      d_c[j] = d_c_prev[j]
    }
  }
  for i in 0..<sac.policy.cell.d_h {
    let in_dim = sac.policy.cell.w_f[i].length()
    for j in 0..<in_dim {
      sac.policy.cell.w_f[i][j] = sac.policy.cell.w_f[i][j] + lr * cell_grad.d_w_f[i][j]
      sac.policy.cell.w_i[i][j] = sac.policy.cell.w_i[i][j] + lr * cell_grad.d_w_i[i][j]
      sac.policy.cell.w_c[i][j] = sac.policy.cell.w_c[i][j] + lr * cell_grad.d_w_c[i][j]
      sac.policy.cell.w_o[i][j] = sac.policy.cell.w_o[i][j] + lr * cell_grad.d_w_o[i][j]
      cell_grad.d_w_f[i][j] = 0.0F
      cell_grad.d_w_i[i][j] = 0.0F
      cell_grad.d_w_c[i][j] = 0.0F
      cell_grad.d_w_o[i][j] = 0.0F
    }
    sac.policy.cell.b_f[i] = sac.policy.cell.b_f[i] + lr * cell_grad.d_b_f[i]
    sac.policy.cell.b_i[i] = sac.policy.cell.b_i[i] + lr * cell_grad.d_b_i[i]
    sac.policy.cell.b_c[i] = sac.policy.cell.b_c[i] + lr * cell_grad.d_b_c[i]
    sac.policy.cell.b_o[i] = sac.policy.cell.b_o[i] + lr * cell_grad.d_b_o[i]
    cell_grad.d_b_f[i] = 0.0F
    cell_grad.d_b_i[i] = 0.0F
    cell_grad.d_b_c[i] = 0.0F
    cell_grad.d_b_o[i] = 0.0F
  }
  let _ = d_h
}

///|
/// Update log_alpha using the episode's empirical mean negative entropy.
///   mean_neg_entropy = (1/T) Σ_t Σ_a π_t(a) · log π_t(a)
///   delta = mean_neg_entropy − target_entropy
///   log_alpha ← log_alpha + alpha_lr · delta
pub fn recurrent_sac_update_alpha(
  sac : RecurrentSac,
  ep : RecurrentSacEpisode,
  rng : Xoshiro,
) -> Float {
  let h0 : Array[Float] = Array::make(sac.policy.cell.d_h, 0.0F)
  let c0 : Array[Float] = Array::make(sac.policy.cell.d_h, 0.0F)
  let (_, policy_cache) = lstm_policy_forward(sac.policy, ep.obs_seq, h0, c0, rng)
  let n_a = 2
  let mut sum_neg_ent = 0.0F
  let n = ep.actions.length()
  for t in 0..<n {
    let pi = policy_cache.probs[t]
    let mut neg_ent = 0.0F
    for i in 0..<n_a {
      let lp = if pi[i] > 0.0F { logf(pi[i]) } else { -20.0F }
      neg_ent = neg_ent + pi[i] * lp
    }
    sum_neg_ent = sum_neg_ent + neg_ent
  }
  let mean_neg_ent = if n > 0 { sum_neg_ent / Float::from_int(n) } else { 0.0F }
  let delta = mean_neg_ent - sac.target_entropy
  sac.log_alpha = sac.log_alpha + sac.alpha_lr * delta
  delta
}

///|
/// Polyak soft target update for both critics.
pub fn recurrent_sac_soft_update(sac : RecurrentSac, tau : Float) -> Unit {
  copy_lstm_qnet_polyak(sac.q1_target, sac.q1, tau)
  copy_lstm_qnet_polyak(sac.q2_target, sac.q2, tau)
}

///|
/// Copy online Q-net into target Q-net (initial sync).
fn copy_lstm_qnet(target : LstmQNet, source : LstmQNet) -> Unit {
  // Copy w_out / b_out.
  for i in 0..<source.w_out.length() {
    for j in 0..<source.w_out[i].length() {
      target.w_out[i][j] = source.w_out[i][j]
    }
    target.b_out[i] = source.b_out[i]
  }
  // Copy LSTM cell weights/biases.
  for i in 0..<source.cell.d_h {
    for j in 0..<source.cell.w_f[i].length() {
      target.cell.w_f[i][j] = source.cell.w_f[i][j]
      target.cell.w_i[i][j] = source.cell.w_i[i][j]
      target.cell.w_c[i][j] = source.cell.w_c[i][j]
      target.cell.w_o[i][j] = source.cell.w_o[i][j]
    }
    target.cell.b_f[i] = source.cell.b_f[i]
    target.cell.b_i[i] = source.cell.b_i[i]
    target.cell.b_c[i] = source.cell.b_c[i]
    target.cell.b_o[i] = source.cell.b_o[i]
  }
}

///|
/// Polyak soft update: target ← τ · source + (1 − τ) · target.
fn copy_lstm_qnet_polyak(
  target : LstmQNet,
  source : LstmQNet,
  tau : Float,
) -> Unit {
  for i in 0..<source.w_out.length() {
    for j in 0..<source.w_out[i].length() {
      target.w_out[i][j] = tau * source.w_out[i][j] +
        (1.0F - tau) * target.w_out[i][j]
    }
    target.b_out[i] = tau * source.b_out[i] + (1.0F - tau) * target.b_out[i]
  }
  for i in 0..<source.cell.d_h {
    for j in 0..<source.cell.w_f[i].length() {
      target.cell.w_f[i][j] = tau * source.cell.w_f[i][j] +
        (1.0F - tau) * target.cell.w_f[i][j]
      target.cell.w_i[i][j] = tau * source.cell.w_i[i][j] +
        (1.0F - tau) * target.cell.w_i[i][j]
      target.cell.w_c[i][j] = tau * source.cell.w_c[i][j] +
        (1.0F - tau) * target.cell.w_c[i][j]
      target.cell.w_o[i][j] = tau * source.cell.w_o[i][j] +
        (1.0F - tau) * target.cell.w_o[i][j]
    }
    target.cell.b_f[i] = tau * source.cell.b_f[i] +
      (1.0F - tau) * target.cell.b_f[i]
    target.cell.b_i[i] = tau * source.cell.b_i[i] +
      (1.0F - tau) * target.cell.b_i[i]
    target.cell.b_c[i] = tau * source.cell.b_c[i] +
      (1.0F - tau) * target.cell.b_c[i]
    target.cell.b_o[i] = tau * source.cell.b_o[i] +
      (1.0F - tau) * target.cell.b_o[i]
  }
}

///|
/// Full training loop. Runs `n_episodes` episodes on the corridor POMDP.
/// After each episode, applies critic, actor, α, and target critic updates.
/// Returns the mean episode return.
pub fn train_recurrent_sac(
  env_n_cells : Int,
  sac : RecurrentSac,
  n_episodes : Int,
  gamma : Float,
  lr_critic : Float,
  lr_actor : Float,
  tau : Float,
  max_steps : Int,
  seed : UInt64,
) -> Float {
  let rng = Xoshiro::from_state(seed, seed + 7UL, seed + 13UL, seed + 17UL)
  let mut total_return = 0.0F
  for _ep in 0..<n_episodes {
    let episode = recurrent_sac_rollout_episode(env_n_cells, sac, max_steps, rng)
    let mut ep_return = 0.0F
    for r in episode.rewards {
      ep_return = ep_return + r
    }
    total_return = total_return + ep_return
    let _ = recurrent_sac_critic_update(sac, episode, gamma, lr_critic, rng)
    recurrent_sac_actor_update(sac, episode, lr_actor, rng)
    let _ = recurrent_sac_update_alpha(sac, episode, rng)
    recurrent_sac_soft_update(sac, tau)
  }
  if n_episodes > 0 {
    total_return / Float::from_int(n_episodes)
  } else {
    0.0F
  }
}