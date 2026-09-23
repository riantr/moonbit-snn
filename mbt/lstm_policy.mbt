// lstm_policy.mbt — REINFORCE with LSTM policy on POMDP corridor (v0.33.2).
//
// POMDP corridor:
//   - N cells in a line. At t=0 the agent sees an indicator
//     (`[1, 0]` for "goal on left" or `[0, 1]` for "goal on right").
//     At later steps only the position is observable.
//   - 2 actions: 0=left, 1=right.
//   - Reward: +1 at goal, -0.1 per step.
//
// LSTM policy:
//   - Observation = concat(indicator_2, position_one_hot_N) =
//     length 2 + N.
//   - LSTM cell processes the sequence.
//   - Output projection: logits = W_out · h_t + b_out (n_actions × d_h).
//   - Sample action from softmax(logits).
//
// REINFORCE with LSTM:
//   - Per-step gradient: d_logits[i] = (1{i == a_t} - π(a|h_t)) · G_t
//   - Output layer: d_W_out += d_logits ⊗ h_t, d_b_out += d_logits
//   - LSTM backward: feed d_h_t into lstm_cell_backward at each
//     timestep, accumulating parameter gradients.

///|
/// Corridor POMDP: indicator at t=0 says which side has the goal.
pub(all) struct CorridorEnv {
  n_cells : Int
  goal_side : Int  // 0 = left (cell 0), 1 = right (cell n_cells-1)
  step_penalty : Float
  goal_reward : Float
  max_steps : Int
}

///|
/// Build a corridor with the goal on the right by default.
pub fn CorridorEnv::new(n_cells : Int, max_steps : Int) -> CorridorEnv {
  { n_cells, goal_side: 1, step_penalty: -0.1F, goal_reward: 1.0F, max_steps }
}

///|
/// Goal cell index (0 or n_cells-1).
pub fn CorridorEnv::goal_cell(self : CorridorEnv) -> Int {
  if self.goal_side == 0 {
    0
  } else {
    self.n_cells - 1
  }
}

///|
/// Build the observation vector at time t. Concat of
/// (indicator, position_one_hot).
pub fn corridor_observation(
  env : CorridorEnv,
  position : Int,
) -> Array[Float] {
  let obs : Array[Float] = Array::make(2 + env.n_cells, 0.0F)
  if env.goal_side == 0 {
    obs[0] = 1.0F
  } else {
    obs[1] = 1.0F
  }
  if position >= 0 && position < env.n_cells {
    obs[2 + position] = 1.0F
  }
  obs
}

///|
/// Step the corridor. pos = current position, action = 0 (left)
/// or 1 (right). Going off the grid keeps the agent in place.
pub fn CorridorEnv::step(self : CorridorEnv, pos : Int, action : Int) -> (Int, Float, Bool) {
  let mut new_pos = pos
  if action == 0 {
    if pos > 0 {
      new_pos = pos - 1
    }
  } else if action == 1 {
    if pos < self.n_cells - 1 {
      new_pos = pos + 1
    }
  }
  let goal = self.goal_cell()
  if new_pos == goal {
    (new_pos, self.goal_reward, true)
  } else {
    (new_pos, self.step_penalty, false)
  }
}

///|
/// LSTM policy: cell + output projection.
pub struct LstmPolicy {
  cell : LstmCellParam
  w_out : Array[Array[Float]]  // n_actions × d_h
  b_out : Array[Float]
}

///|
/// Build an LSTM policy. d_x = 2 + n_cells, d_h = hidden_dim,
/// n_actions = 2 (left/right).
pub fn LstmPolicy::new(
  n_cells : Int,
  d_h : Int,
  seed : UInt64,
) -> LstmPolicy {
  let d_x = 2 + n_cells
  let cell = LstmCellParam::new(d_x, d_h, seed)
  let n_actions = 2
  let std = sqrtf(1.0F / Float::from_int(d_h))
  let rng = Xoshiro::from_state(seed + 1UL, seed + 2UL, seed + 3UL, seed + 4UL)
  let w_out = xavier_normal(n_actions, d_h, std, rng)
  let b_out : Array[Float] = Array::make(n_actions, 0.0F)
  { cell, w_out, b_out }
}

///|
/// Forward cache: per-step cell caches + per-step projected info.
pub struct LstmPolicyCache {
  cell_caches : Array[LstmCellCache]
  probs : Array[Array[Float]]
  hs : Array[Array[Float]]
}

///|
/// Run the LSTM policy forward. Returns (action_seq, cache).
pub fn lstm_policy_forward(
  policy : LstmPolicy,
  obs_seq : Array[Array[Float]],
  h0 : Array[Float],
  c0 : Array[Float],
  rng : Xoshiro,
) -> (Array[Int], LstmPolicyCache) {
  let n = obs_seq.length()
  let cell_caches : Array[LstmCellCache] = []
  let probs : Array[Array[Float]] = []
  let hs : Array[Array[Float]] = []
  let actions : Array[Int] = []
  let mut cur_h = h0
  let mut cur_c = c0
  for t in 0..<n {
    let (h_t, cache_t) = lstm_cell_forward(obs_seq[t], cur_h, cur_c, policy.cell)
    let n_actions = policy.w_out.length()
    let logits : Array[Float] = Array::make(n_actions, 0.0F)
    for i in 0..<n_actions {
      let row = policy.w_out[i]
      let mut s = policy.b_out[i]
      for j in 0..<row.length() {
        s = s + row[j] * h_t[j]
      }
      logits[i] = s
    }
    let mut max_l = logits[0]
    for i in 1..<n_actions {
      if logits[i] > max_l {
        max_l = logits[i]
      }
    }
    let ps : Array[Float] = Array::make(n_actions, 0.0F)
    let mut sum_e = 0.0F
    for i in 0..<n_actions {
      let e = expf(logits[i] - max_l)
      ps[i] = e
      sum_e = sum_e + e
    }
    let inv_sum = 1.0F / sum_e
    for i in 0..<n_actions {
      ps[i] = ps[i] * inv_sum
    }
    let (a, _lp) = sample_categorical(ps, rng)
    actions.push(a)
    cell_caches.push(cache_t)
    probs.push(ps)
    hs.push(h_t)
    cur_h = h_t
    cur_c = cache_t.c_t
  }
  let cache : LstmPolicyCache = { cell_caches, probs, hs }
  (actions, cache)
}

///|
/// REINFORCE-style policy gradient update with LSTM.
pub fn lstm_policy_gradient_update(
  policy : LstmPolicy,
  actions : Array[Int],
  returns : Array[Float],
  cache : LstmPolicyCache,
  lr : Float,
) -> Unit {
  // Use the min length so we don't read past the end of `returns`
  // when the env terminates before `max_steps`.
  let actions_n = actions.length()
  let returns_n = returns.length()
  let n = if actions_n < returns_n { actions_n } else { returns_n }
  let n_actions = policy.w_out.length()
  let d_h = policy.cell.d_h
  let d_w_out : Array[Array[Float]] = Array::make(n_actions, [])
  for i in 0..<n_actions {
    d_w_out[i] = Array::make(d_h, 0.0F)
  }
  let d_b_out : Array[Float] = Array::make(n_actions, 0.0F)
  // d_h_per_t[t]: gradient w.r.t. h_t from output projection.
  let d_h_per_t : Array[Array[Float]] = Array::make(n, [])
  for t in 0..<n {
    d_h_per_t[t] = Array::make(d_h, 0.0F)
  }
  let d_h_prev_global : Array[Float] = Array::make(d_h, 0.0F)
  let d_c : Array[Float] = Array::make(d_h, 0.0F)
  let cell_grad = LstmCellGrad::zero(policy.cell)
  for t_step in 0..<n {
    let t = n - 1 - t_step
    let g = returns[t]
    if g == 0.0F {
      continue
    }
    let ps = cache.probs[t]
    let a = actions[t]
    let d_logits : Array[Float] = Array::make(n_actions, 0.0F)
    for i in 0..<n_actions {
      let indicator = if i == a { 1.0F } else { 0.0F }
      d_logits[i] = (indicator - ps[i]) * g
      d_b_out[i] = d_b_out[i] + d_logits[i]
      for j in 0..<d_h {
        d_w_out[i][j] = d_w_out[i][j] + d_logits[i] * cache.hs[t][j]
        d_h_per_t[t][j] = d_h_per_t[t][j] + policy.w_out[i][j] * d_logits[i]
      }
    }
    if t < n - 1 {
      for j in 0..<d_h {
        d_h_per_t[t][j] = d_h_per_t[t][j] + d_h_prev_global[j]
      }
    }
    let (d_h_prev, _d_x, d_c_prev) = lstm_cell_backward(
      cache.cell_caches[t], d_h_per_t[t], d_c, policy.cell, cell_grad,
    )
    for j in 0..<d_h {
      d_h_prev_global[j] = d_h_prev[j]
      d_c[j] = d_c_prev[j]
    }
  }
  // Apply output projection gradients.
  for i in 0..<n_actions {
    for j in 0..<d_h {
      policy.w_out[i][j] = policy.w_out[i][j] + lr * d_w_out[i][j]
    }
    policy.b_out[i] = policy.b_out[i] + lr * d_b_out[i]
  }
  // Apply LSTM cell gradients.
  apply_lstm_grad(policy, cell_grad, lr)
}

///|
/// Apply accumulated LSTM cell gradients via simple SGD step.
fn apply_lstm_grad(
  policy : LstmPolicy,
  grad : LstmCellGrad,
  lr : Float,
) -> Unit {
  for i in 0..<policy.cell.d_h {
    let in_dim = policy.cell.w_f[i].length()
    for j in 0..<in_dim {
      policy.cell.w_f[i][j] = policy.cell.w_f[i][j] + lr * grad.d_w_f[i][j]
      policy.cell.w_i[i][j] = policy.cell.w_i[i][j] + lr * grad.d_w_i[i][j]
      policy.cell.w_c[i][j] = policy.cell.w_c[i][j] + lr * grad.d_w_c[i][j]
      policy.cell.w_o[i][j] = policy.cell.w_o[i][j] + lr * grad.d_w_o[i][j]
      grad.d_w_f[i][j] = 0.0F
      grad.d_w_i[i][j] = 0.0F
      grad.d_w_c[i][j] = 0.0F
      grad.d_w_o[i][j] = 0.0F
    }
    policy.cell.b_f[i] = policy.cell.b_f[i] + lr * grad.d_b_f[i]
    policy.cell.b_i[i] = policy.cell.b_i[i] + lr * grad.d_b_i[i]
    policy.cell.b_c[i] = policy.cell.b_c[i] + lr * grad.d_b_c[i]
    policy.cell.b_o[i] = policy.cell.b_o[i] + lr * grad.d_b_o[i]
    grad.d_b_f[i] = 0.0F
    grad.d_b_i[i] = 0.0F
    grad.d_b_c[i] = 0.0F
    grad.d_b_o[i] = 0.0F
  }
}

///|
/// Train REINFORCE-LSTM on the corridor POMDP for `n_episodes`.
/// Each episode randomly sets `goal_side` to 0 or 1, and starts the
/// agent at the centre cell. The observation sequence uses a fixed
/// length (max_steps); all observations use the same starting
/// position but include the indicator at every step, so the LSTM
/// can attend to it across the sequence.
/// Returns the mean episode return.
pub fn train_lstm_reinforce(
  env_n_cells : Int,
  policy : LstmPolicy,
  n_episodes : Int,
  gamma : Float,
  lr : Float,
  max_steps : Int,
  seed : UInt64,
) -> Float {
  let rng = Xoshiro::from_state(seed, seed + 7UL, seed + 13UL, seed + 17UL)
  let mut total_return = 0.0F
  let start = env_n_cells / 2
  for _ep in 0..<n_episodes {
    let (u_raw, _) = box_muller(rng)
    let side = if u_raw > 0.0 { 1 } else { 0 }
    let env : CorridorEnv = CorridorEnv::{
      n_cells: env_n_cells,
      goal_side: side,
      step_penalty: -0.1F,
      goal_reward: 1.0F,
      max_steps,
    }
    // Build a fixed-length obs_seq: indicator is always present,
    // position is the start (so the LSTM must rely on the
    // indicator alone to decide direction).
    let obs_seq : Array[Array[Float]] = []
    for _t in 0..<max_steps {
      obs_seq.push(corridor_observation(env, start))
    }
    // Run policy forward.
    let h0 : Array[Float] = Array::make(policy.cell.d_h, 0.0F)
    let c0 : Array[Float] = Array::make(policy.cell.d_h, 0.0F)
    let (actions, cache) = lstm_policy_forward(policy, obs_seq, h0, c0, rng)
    // Apply actions in env, collecting rewards up to termination.
    let actual_rewards : Array[Float] = []
    let mut pos = start
    let mut done = false
    let mut k = 0
    while !done && k < actions.length() {
      let (np, r, d) = env.step(pos, actions[k])
      actual_rewards.push(r)
      pos = np
      done = d
      k = k + 1
    }
    total_return = total_return + lstm_episode_return(actual_rewards)
    let returns = compute_returns(actual_rewards, gamma)
    lstm_policy_gradient_update(policy, actions, returns, cache, lr)
  }
  total_return / Float::from_int(n_episodes)
}

///|
/// Total episode return (sum of rewards).
pub fn lstm_episode_return(rewards : Array[Float]) -> Float {
  let mut s = 0.0F
  for r in rewards {
    s = s + r
  }
  s
}