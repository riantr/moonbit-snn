// gru_policy.mbt — REINFORCE with GRU policy on POMDP corridor (v0.34.0).
//
// Same setup as `lstm_policy.mbt` but with a GRU cell instead of
// LSTM. GRU has no cell state — only the hidden state evolves:
//   z_t = σ(W_z · [h_{t-1}; x_t] + b_z)
//   r_t = σ(W_r · [h_{t-1}; x_t] + b_r)
//   n_t = tanh(W_n · [r_t ⊙ h_{t-1}; x_t] + b_n)
//   h_t = (1 - z_t) ⊙ h_{t-1} + z_t ⊙ n_t
//
// The output projection W_out · h_t + b_out is identical to the
// LSTM policy. REINFORCE updates accumulate per-step d_logits
// gradients (using the TD-style ∇log π gradient), then backprop
// through the GRU via `gru_cell_backward`.

///|
/// GRU policy: cell + output projection.
pub struct GruPolicy {
  cell : GruCellParam
  w_out : Array[Array[Float]]  // n_actions × d_h
  b_out : Array[Float]
}

///|
/// Build a GRU policy. d_x = 2 + n_cells, d_h = hidden_dim,
/// n_actions = 2 (left/right).
pub fn GruPolicy::new(
  n_cells : Int,
  d_h : Int,
  seed : UInt64,
) -> GruPolicy {
  let d_x = 2 + n_cells
  let cell = GruCellParam::new(d_x, d_h, seed)
  let n_actions = 2
  let std = sqrtf(1.0F / Float::from_int(d_h))
  let rng = Xoshiro::from_state(seed + 1UL, seed + 2UL, seed + 3UL, seed + 4UL)
  let w_out = xavier_normal(n_actions, d_h, std, rng)
  let b_out : Array[Float] = Array::make(n_actions, 0.0F)
  { cell, w_out, b_out }
}

///|
/// Forward cache for the GRU policy.
pub struct GruPolicyCache {
  cell_caches : Array[GruCellCache]
  probs : Array[Array[Float]]
  hs : Array[Array[Float]]
}

///|
/// Run the GRU policy forward. Returns (action_seq, cache).
pub fn gru_policy_forward(
  policy : GruPolicy,
  obs_seq : Array[Array[Float]],
  h0 : Array[Float],
  rng : Xoshiro,
) -> (Array[Int], GruPolicyCache) {
  let n = obs_seq.length()
  let cell_caches : Array[GruCellCache] = []
  let probs : Array[Array[Float]] = []
  let hs : Array[Array[Float]] = []
  let actions : Array[Int] = []
  let mut cur_h = h0
  for t in 0..<n {
    let (h_t, cache_t) = gru_cell_forward(obs_seq[t], cur_h, policy.cell)
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
  }
  let cache : GruPolicyCache = { cell_caches, probs, hs }
  (actions, cache)
}

///|
/// REINFORCE-style policy gradient update with GRU.
pub fn gru_policy_gradient_update(
  policy : GruPolicy,
  actions : Array[Int],
  returns : Array[Float],
  cache : GruPolicyCache,
  lr : Float,
) -> Unit {
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
  let d_h_per_t : Array[Array[Float]] = Array::make(n, [])
  for t in 0..<n {
    d_h_per_t[t] = Array::make(d_h, 0.0F)
  }
  let d_h_prev_global : Array[Float] = Array::make(d_h, 0.0F)
  let cell_grad = GruCellGrad::zero(policy.cell)
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
    let (d_h_prev, _d_x) = gru_cell_backward(
      cache.cell_caches[t], d_h_per_t[t], policy.cell, cell_grad,
    )
    for j in 0..<d_h {
      d_h_prev_global[j] = d_h_prev[j]
    }
  }
  // Apply output projection gradients.
  for i in 0..<n_actions {
    for j in 0..<d_h {
      policy.w_out[i][j] = policy.w_out[i][j] + lr * d_w_out[i][j]
    }
    policy.b_out[i] = policy.b_out[i] + lr * d_b_out[i]
  }
  // Apply GRU cell gradients.
  apply_gru_grad(policy, cell_grad, lr)
}

///|
/// Apply accumulated GRU cell gradients via simple SGD step.
fn apply_gru_grad(
  policy : GruPolicy,
  grad : GruCellGrad,
  lr : Float,
) -> Unit {
  for i in 0..<policy.cell.d_h {
    let in_dim = policy.cell.w_z[i].length()
    for j in 0..<in_dim {
      policy.cell.w_z[i][j] = policy.cell.w_z[i][j] + lr * grad.d_w_z[i][j]
      policy.cell.w_r[i][j] = policy.cell.w_r[i][j] + lr * grad.d_w_r[i][j]
      policy.cell.w_n[i][j] = policy.cell.w_n[i][j] + lr * grad.d_w_n[i][j]
      grad.d_w_z[i][j] = 0.0F
      grad.d_w_r[i][j] = 0.0F
      grad.d_w_n[i][j] = 0.0F
    }
    policy.cell.b_z[i] = policy.cell.b_z[i] + lr * grad.d_b_z[i]
    policy.cell.b_r[i] = policy.cell.b_r[i] + lr * grad.d_b_r[i]
    policy.cell.b_n[i] = policy.cell.b_n[i] + lr * grad.d_b_n[i]
    grad.d_b_z[i] = 0.0F
    grad.d_b_r[i] = 0.0F
    grad.d_b_n[i] = 0.0F
  }
}

///|
/// Train REINFORCE-GRU on the corridor POMDP for `n_episodes`.
pub fn train_gru_reinforce(
  env_n_cells : Int,
  policy : GruPolicy,
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
    let obs_seq : Array[Array[Float]] = []
    for _t in 0..<max_steps {
      obs_seq.push(corridor_observation(env, start))
    }
    let h0 : Array[Float] = Array::make(policy.cell.d_h, 0.0F)
    let (actions, cache) = gru_policy_forward(policy, obs_seq, h0, rng)
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
    gru_policy_gradient_update(policy, actions, returns, cache, lr)
  }
  total_return / Float::from_int(n_episodes)
}