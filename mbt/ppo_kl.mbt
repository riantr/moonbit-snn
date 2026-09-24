// ppo_kl.mbt — PPO with KL penalty (v0.35.2).
//
// KL-penalized PPO objective (without explicit clipping):
//
//   L_KL(θ) = E[ r_t(θ) · A_t ] - β · KL(π_θ(·|s_t) || π_θ_old(·|s_t))
//
// where r_t(θ) = π_θ(a_t|s_t) / π_θ_old(a_t|s_t) and the per-state
// KL is the standard forward KL.
//
// Gradient of -β·KL w.r.t. θ:
//   ∇_θ(-β·KL) = -β · Σ_a (log(π_θ(a|s) / π_old(a|s)) + 1) · ∇_θ π_θ(a|s)
//
// Combined with the surrogate gradient:
//   ∇_θ(-L_KL) = -A_t · ∇_θ r_t(θ)  -  β · (∇_θ KL term above)
//
// For LinearSoftmaxPolicy:
//   ∇_θ π_θ(a|s) = (1{i==a} - π_θ(i|s)) · x[j]  for W[i,j]

///|
/// Batch storing per-step (state, action, advantage) plus the full
/// per-state old distribution (for the KL term).
pub(all) struct PpoKlBatch {
  states : Array[Int]
  actions : Array[Int]
  advantages : Array[Float]
  old_probs : Array[Float]  // π_old(a_t|s_t)
  old_dist : Array[Array[Float]]  // π_old(·|s_t), full distribution per state
}

///|
/// Collect a batch with GAE-computed advantages. Uses a value net
/// for advantage computation but does NOT update it (this variant
/// focuses on the KL penalty term; assumes V is held fixed or
/// pre-trained).
pub fn ppo_kl_collect_batch(
  env : GridWorld,
  policy : LinearSoftmaxPolicy,
  value_net : LinearValueNet,
  n_episodes : Int,
  gamma : Float,
  max_steps : Int,
  seed : UInt64,
) -> PpoKlBatch {
  let rng = Xoshiro::from_state(seed, seed + 7UL, seed + 13UL, seed + 17UL)
  let states : Array[Int] = []
  let actions : Array[Int] = []
  let advantages : Array[Float] = []
  let old_probs : Array[Float] = []
  let old_dist : Array[Array[Float]] = []
  for _ep in 0..<n_episodes {
    // Roll out one episode recording V(s), V(s'), rewards, dones.
    let ep_states : Array[Int] = []
    let ep_actions : Array[Int] = []
    let ep_rewards : Array[Float] = []
    let ep_values : Array[Float] = []
    let ep_next_values : Array[Float] = []
    let ep_dones : Array[Bool] = []
    let mut state = env.start
    let mut done = false
    let mut t = 0
    while !done && t < max_steps {
      let x = rl_one_hot(state, env.n_states())
      let v = value_forward(value_net, x)
      let (_logits, probs) = policy_forward(policy, x)
      let (a, _lp) = sample_categorical(probs, rng)
      let (next_state, r, d) = env.step(state, a)
      let next_x = rl_one_hot(next_state, env.n_states())
      let v_next = if d { 0.0F } else { value_forward(value_net, next_x) }
      ep_states.push(state)
      ep_actions.push(a)
      ep_rewards.push(r)
      ep_values.push(v)
      ep_next_values.push(v_next)
      ep_dones.push(d)
      state = next_state
      done = d
      t = t + 1
    }
    let ep_len = ep_states.length()
    if ep_len == 0 {
      continue
    }
    // Compute returns R_t for the episode.
    let rets : Array[Float] = Array::make(ep_len, 0.0F)
    let mut running_r = 0.0F
    let mut i = ep_len - 1
    while i >= 0 {
      running_r = ep_rewards[i] + gamma * running_r
      rets[i] = running_r
      i = i - 1
    }
    // GAE advantages with λ=1 (full Monte-Carlo = G_t - V(s_t)).
    for t in 0..<ep_len {
      states.push(ep_states[t])
      actions.push(ep_actions[t])
      advantages.push(rets[t] - ep_values[t])
      let x = rl_one_hot(ep_states[t], env.n_states())
      let (_logits, probs) = policy_forward(policy, x)
      old_probs.push(probs[ep_actions[t]])
      let p_copy : Array[Float] = Array::make(probs.length(), 0.0F)
      for k in 0..<probs.length() {
        p_copy[k] = probs[k]
      }
      old_dist.push(p_copy)
    }
  }
  { states, actions, advantages, old_probs, old_dist }
}

///|
/// KL divergence KL(π_θ || π_θ_old) at a single state. Returns
/// the forward KL = Σ_a π_θ(a) · log(π_θ(a) / π_old(a)).
pub fn per_state_kl(pi_cur : Array[Float], pi_old : Array[Float]) -> Float {
  let n = pi_cur.length()
  let mut kl = 0.0F
  for i in 0..<n {
    if pi_cur[i] > 0.0F && pi_old[i] > 0.0F {
      kl = kl + pi_cur[i] * logf(pi_cur[i] / pi_old[i])
    }
  }
  kl
}

///|
/// One PPO-KL update step. Returns the mean per-state KL after the
/// update (for monitoring). Modifies `policy.w` in place.
pub fn ppo_kl_update_step(
  policy : LinearSoftmaxPolicy,
  batch : PpoKlBatch,
  kl_beta : Float,
  lr : Float,
) -> Float {
  let n = batch.states.length()
  let n_a = policy.n_actions
  let n_s = policy.n_states
  let mut total_kl = 0.0F
  for step in 0..<n {
    let a = batch.advantages[step]
    let old_p_a = batch.old_probs[step]
    if old_p_a <= 0.0F {
      continue
    }
    let old_dist = batch.old_dist[step]
    let x = rl_one_hot(batch.states[step], n_s)
    let (_logits, probs) = policy_forward(policy, x)
    let cur_p_a = probs[batch.actions[step]]
    let r = cur_p_a / old_p_a
    // Surrogate gradient contribution.
    // ∇(-r · A) = -A · ∇r = -A · r · ∇log π(a|s) (chain rule via r = π/π_old)
    let surr_scale = -a * r
    // KL gradient contribution (for each action i):
    // d_KL contribution to W[i,j] = -β · (log(π(i|s)/π_old(i)) + 1) · ∇π(i|s)
    // ∇π(i|s) for W[i,j] = (1{i==k} - π(k|s)) · x[j]
    // Combined d_W[i,j] += lr · surr_scale_term + lr · kl_term
    //   where kl_term sums over actions k:
    //     kl_term_k = -β · (log(π(k|s)/π_old(k)) + 1) · (1{i==k} - π(k|s))
    //   The sign: we want to MINIMIZE L_KL = -E[r·A] + β·KL, so we
    //   take the gradient of L_KL (positive if KL should increase).
    //   To decrease KL: d_W -= lr · β · gradient_of_KL.
    //   ∇_W[i,j] KL = Σ_k (log(π(k)/π_old(k)) + 1) · (1{i==k} - π(k)) · x[j]
    let action_idx = batch.actions[step]
    for i in 0..<n_a {
      let mut grad_row = 0.0F
      // Surrogate: d_W[i,j] += lr · (-A · r) · (1{i==action} - probs[i]) · x[j]
      let indicator_action = if i == action_idx { 1.0F } else { 0.0F }
      grad_row = grad_row + surr_scale * (indicator_action - probs[i])
      // KL penalty term: d_W[i,j] -= lr · β · Σ_k (log(π(k)/π_old(k)) + 1)
      //   · (1{i==k} - probs[k]) · x[j]
      for k in 0..<n_a {
        let indicator_k = if i == k { 1.0F } else { 0.0F }
        let lp = if probs[k] > 0.0F && old_dist[k] > 0.0F {
          logf(probs[k] / old_dist[k])
        } else {
          0.0F
        }
        grad_row = grad_row - lr * kl_beta * (lp + 1.0F) * (indicator_k - probs[k])
      }
      let row = policy.w[i]
      for j in 0..<n_s {
        row[j] = row[j] + lr * grad_row * x[j]
      }
    }
    total_kl = total_kl + per_state_kl(probs, old_dist)
  }
  if n > 0 {
    total_kl / Float::from_int(n)
  } else {
    0.0F
  }
}

///|
/// Train PPO-KL for `n_iters`. Returns the last mean episode return.
pub fn train_ppo_kl(
  env : GridWorld,
  policy : LinearSoftmaxPolicy,
  value_net : LinearValueNet,
  n_iters : Int,
  n_episodes_per_iter : Int,
  gamma : Float,
  kl_beta : Float,
  lr : Float,
  max_steps : Int,
  seed : UInt64,
) -> Float {
  let mut last_mean = 0.0F
  let mut s = seed
  for _iter in 0..<n_iters {
    let batch = ppo_kl_collect_batch(
      env, policy, value_net, n_episodes_per_iter, gamma, max_steps, s,
    )
    let _ = ppo_kl_update_step(policy, batch, kl_beta, lr)
    // Approximate mean episode return from advantages.
    let mut sum = 0.0F
    let mut count = 0
    for a in batch.advantages {
      sum = sum + a
      count = count + 1
    }
    last_mean = if count > 0 { sum / Float::from_int(count) } else { 0.0F }
    s = s + 1UL
  }
  last_mean
}