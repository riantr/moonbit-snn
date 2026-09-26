// sac_auto_alpha.mbt — SAC with auto-tuned entropy temperature (v0.38.1).
//
// Extension of v0.38.0 SAC where α is itself a learnable parameter.
//
// Algorithm (Haarnoja et al. 2018, Appendix):
//   target_entropy H̄ ∈ ℝ (typically negative; e.g. −|A| for discrete actions)
//   α parameterised as α = exp(log_alpha) ∈ (0, ∞)
//
//   For each batch of states:
//     neg_entropy_s = Σ_a π(a|s) · log π(a|s)   (≤ 0)
//     mean_neg_entropy = E_s[neg_entropy_s]
//     delta = mean_neg_entropy − target_entropy
//     log_alpha ← log_alpha + alpha_lr · delta
//
//   Sign interpretation:
//     delta < 0 → policy is "too random" (mean_neg_entropy below target) → shrink α
//     delta > 0 → policy is "too deterministic" → grow α
//     delta = 0 → equilibrium
//
// The actor and critic use α = exp(log_alpha) in place of the fixed
// `sac.alpha` from v0.38.0.

///|
/// SAC agent with auto-tuned entropy temperature.
pub struct SacAutoAlpha {
  policy : LinearSoftmaxPolicy
  q1 : LinearQNet
  q2 : LinearQNet
  q1_target : LinearQNet
  q2_target : LinearQNet
  /// log of entropy temperature; α = exp(log_alpha).
  mut log_alpha : Float
  /// Target for E[Σ_a π(a|s) log π(a|s)]. Negative scalar.
  /// Typical choice: −|A| for discrete action spaces.
  target_entropy : Float
  /// Learning rate for the log_alpha update.
  alpha_lr : Float
}

///|
pub fn SacAutoAlpha::new(
  n_states : Int,
  n_actions : Int,
  log_alpha_init : Float,
  target_entropy : Float,
  alpha_lr : Float,
  seed : UInt64,
) -> SacAutoAlpha {
  let policy = LinearSoftmaxPolicy::new(n_states, n_actions, seed)
  let q1 = LinearQNet::new(n_states, n_actions, seed + 1UL)
  let q2 = LinearQNet::new(n_states, n_actions, seed + 2UL)
  let q1_target = LinearQNet::new(n_states, n_actions, seed + 3UL)
  let q2_target = LinearQNet::new(n_states, n_actions, seed + 4UL)
  qnet_copy(q1_target, q1)
  qnet_copy(q2_target, q2)
  { policy, q1, q2, q1_target, q2_target, log_alpha: log_alpha_init, target_entropy, alpha_lr }
}

///|
/// Current α = exp(log_alpha).
pub fn sac_auto_alpha_get(sac : SacAutoAlpha) -> Float {
  expf(sac.log_alpha)
}

///|
/// Sample action from the softmax policy.
pub fn sac_auto_alpha_sample_action(
  policy : LinearSoftmaxPolicy,
  state : Int,
  rng : Xoshiro,
) -> Int {
  let x = rl_one_hot(state, policy.n_states)
  let (_l, probs) = policy_forward(policy, x)
  let (a, _) = sample_categorical(probs, rng)
  a
}

///|
/// Soft Bellman target (discrete). Identical to v0.38.0 except α is
/// computed from `log_alpha` instead of the fixed `alpha` field.
pub fn sac_auto_alpha_soft_target(
  sac : SacAutoAlpha,
  next_state : Int,
  reward : Float,
  done : Bool,
  gamma : Float,
) -> Float {
  if done {
    return reward
  }
  let x_next = rl_one_hot(next_state, sac.policy.n_states)
  let (_l, pi) = policy_forward(sac.policy, x_next)
  let q1_t = q_forward(sac.q1_target, x_next)
  let q2_t = q_forward(sac.q2_target, x_next)
  let alpha = sac_auto_alpha_get(sac)
  let mut expected = 0.0F
  let n_a = sac.policy.n_actions
  for i in 0..<n_a {
    let q_min = if q1_t[i] < q2_t[i] { q1_t[i] } else { q2_t[i] }
    let lp = if pi[i] > 0.0F { logf(pi[i]) } else { -20.0F }
    expected = expected + pi[i] * (q_min - alpha * lp)
  }
  reward + gamma * expected
}

///|
/// Critic MSE update (same as v0.38.0).
pub fn sac_auto_alpha_critic_update(
  sac : SacAutoAlpha,
  states : Array[Int],
  actions : Array[Int],
  rewards : Array[Float],
  next_states : Array[Int],
  dones : Array[Bool],
  gamma : Float,
  lr : Float,
) -> Float {
  let n = states.length()
  let mut total_loss = 0.0F
  for k in 0..<n {
    let s = states[k]
    let a = actions[k]
    let r = rewards[k]
    let s_next = next_states[k]
    let d = dones[k]
    let x = rl_one_hot(s, sac.policy.n_states)
    let q1 = q_forward(sac.q1, x)
    let q2 = q_forward(sac.q2, x)
    let target = sac_auto_alpha_soft_target(sac, s_next, r, d, gamma)
    let err1 = q1[a] - target
    let err2 = q2[a] - target
    total_loss = total_loss + err1 * err1 + err2 * err2
    let g1 = 2.0F * lr * err1
    let g2 = 2.0F * lr * err2
    for j in 0..<sac.policy.n_states {
      sac.q1.w[a][j] = sac.q1.w[a][j] - g1 * x[j]
      sac.q2.w[a][j] = sac.q2.w[a][j] - g2 * x[j]
    }
    sac.q1.b[a] = sac.q1.b[a] - g1
    sac.q2.b[a] = sac.q2.b[a] - g2
  }
  total_loss / Float::from_int(n)
}

///|
/// Actor update using current α = exp(log_alpha). Same structure as
/// v0.38.0 `sac_actor_update`, but α is read live.
pub fn sac_auto_alpha_actor_update(
  sac : SacAutoAlpha,
  states : Array[Int],
  lr : Float,
) -> Unit {
  let n = states.length()
  let n_a = sac.policy.n_actions
  let n_s = sac.policy.n_states
  let alpha = sac_auto_alpha_get(sac)
  for k in 0..<n {
    let s = states[k]
    let x = rl_one_hot(s, n_s)
    let q = q_forward(sac.q1, x)
    let (_l, pi) = policy_forward(sac.policy, x)
    for i in 0..<n_a {
      let mut q_contrib = 0.0F
      let mut h_contrib = 0.0F
      for a in 0..<n_a {
        let indicator = if i == a { 1.0F } else { 0.0F }
        q_contrib = q_contrib + (pi[a] * q[a] - indicator * q[a])
        let lp = if pi[a] > 0.0F { logf(pi[a]) } else { -20.0F }
        h_contrib = h_contrib + (lp + 1.0F) * (indicator - pi[a])
      }
      let grad_row = lr * (q_contrib + alpha * h_contrib)
      let row = sac.policy.w[i]
      for j in 0..<n_s {
        row[j] = row[j] - grad_row * x[j]
      }
    }
  }
}

///|
/// Update log_alpha using the batch's empirical mean negative entropy.
/// Returns the delta applied (= mean_neg_entropy − target_entropy).
///
/// The new α reflects how far the current policy is from the target:
///   delta > 0  →  policy too deterministic  →  α grows
///   delta < 0  →  policy too random          →  α shrinks
pub fn sac_auto_alpha_update_alpha(
  sac : SacAutoAlpha,
  states : Array[Int],
) -> Float {
  let n = states.length()
  let n_a = sac.policy.n_actions
  let n_s = sac.policy.n_states
  let mut sum_neg_ent = 0.0F
  for k in 0..<n {
    let s = states[k]
    let x = rl_one_hot(s, n_s)
    let (_l, pi) = policy_forward(sac.policy, x)
    let mut neg_ent = 0.0F
    for a in 0..<n_a {
      let lp = if pi[a] > 0.0F { logf(pi[a]) } else { -20.0F }
      neg_ent = neg_ent + pi[a] * lp
    }
    sum_neg_ent = sum_neg_ent + neg_ent
  }
  let mean_neg_ent = sum_neg_ent / Float::from_int(n)
  let delta = mean_neg_ent - sac.target_entropy
  sac.log_alpha = sac.log_alpha + sac.alpha_lr * delta
  delta
}

///|
/// Polyak soft target update (identical to v0.38.0).
pub fn sac_auto_alpha_soft_update(sac : SacAutoAlpha, tau : Float) -> Unit {
  for i in 0..<sac.q1.n_actions {
    for j in 0..<sac.q1.n_states {
      sac.q1_target.w[i][j] = tau * sac.q1.w[i][j] +
        (1.0F - tau) * sac.q1_target.w[i][j]
      sac.q2_target.w[i][j] = tau * sac.q2.w[i][j] +
        (1.0F - tau) * sac.q2_target.w[i][j]
    }
    sac.q1_target.b[i] = tau * sac.q1.b[i] +
      (1.0F - tau) * sac.q1_target.b[i]
    sac.q2_target.b[i] = tau * sac.q2.b[i] +
      (1.0F - tau) * sac.q2_target.b[i]
  }
}

///|
/// Full training loop. Same structure as v0.38.0 `train_sac`, with the
/// addition of `sac_auto_alpha_update_alpha(...)` after each actor step.
pub fn train_sac_auto_alpha(
  env : GridWorld,
  sac : SacAutoAlpha,
  n_episodes : Int,
  gamma : Float,
  lr_critic : Float,
  lr_actor : Float,
  tau : Float,
  max_steps : Int,
  buffer_capacity : Int,
  warmup_episodes : Int,
  batch_size : Int,
  seed : UInt64,
) -> Float {
  let rng = Xoshiro::from_state(seed, seed + 7UL, seed + 13UL, seed + 17UL)
  let buffer = ReplayBuffer::new(buffer_capacity)
  let mut total_return = 0.0F
  let mut return_count = 0
  for ep in 0..<n_episodes {
    let mut state = env.start
    let mut done = false
    let mut t = 0
    let mut ep_return = 0.0F
    while !done && t < max_steps {
      let a = sac_auto_alpha_sample_action(sac.policy, state, rng)
      let (next_state, r, d) = env.step(state, a)
      buffer.push(state, a, r, next_state, d)
      ep_return = ep_return + r
      state = next_state
      done = d
      t = t + 1
    }
    total_return = total_return + ep_return
    return_count = return_count + 1
    if ep >= warmup_episodes && buffer.len() >= batch_size {
      let (ss, aa, rr, nss, dd) = buffer.sample(batch_size, rng)
      let _ = sac_auto_alpha_critic_update(sac, ss, aa, rr, nss, dd, gamma, lr_critic)
      sac_auto_alpha_actor_update(sac, ss, lr_actor)
      let _ = sac_auto_alpha_update_alpha(sac, ss)
      sac_auto_alpha_soft_update(sac, tau)
    }
  }
  if return_count > 0 {
    total_return / Float::from_int(return_count)
  } else {
    0.0F
  }
}