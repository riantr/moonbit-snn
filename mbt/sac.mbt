// sac.mbt — Soft Actor-Critic (v0.38.0).
//
// SAC (Haarnoja et al. 2018) for discrete actions on GridWorld.
// Stochastic softmax policy + twin Q-networks + entropy bonus.
//
// Key components:
//   - SoftmaxPolicy (linear)         — π(a|s) = softmax(W · x)
//   - Q-net (linear)                  — Q(s, a) = W_q · x + b_q
//   - Twin Q-nets + target Q-nets (Polyak averaged)
//   - ReplayBuffer (reused from dqn.mbt)
//
// Soft Bellman target (discrete):
//   Q_target(s, a) = r + γ · Σ_{a'} π(a'|s') · [Q̂_min(s', a') - α · log π(a'|s')]
// where Q̂_min = min(Q̂1, Q̂2).
//
// Actor loss: maximise Σ_a π(a|s) · α · log π(a|s) - Q(s, a)
// (equivalently minimise the negative).
//
// Critic loss: MSE(Q(s,a), Q_target(s,a)) for both Q1 and Q2.

///|
/// SAC policy + critic + target critics + temperature α.
pub struct Sac {
  policy : LinearSoftmaxPolicy
  q1 : LinearQNet
  q2 : LinearQNet
  q1_target : LinearQNet
  q2_target : LinearQNet
  /// Entropy temperature. Higher = more exploration.
  alpha : Float
}

///|
pub fn Sac::new(
  n_states : Int,
  n_actions : Int,
  alpha : Float,
  seed : UInt64,
) -> Sac {
  let policy = LinearSoftmaxPolicy::new(n_states, n_actions, seed)
  let q1 = LinearQNet::new(n_states, n_actions, seed + 1UL)
  let q2 = LinearQNet::new(n_states, n_actions, seed + 2UL)
  let q1_target = LinearQNet::new(n_states, n_actions, seed + 3UL)
  let q2_target = LinearQNet::new(n_states, n_actions, seed + 4UL)
  // Sync target = online initially.
  qnet_copy(q1_target, q1)
  qnet_copy(q2_target, q2)
  { policy, q1, q2, q1_target, q2_target, alpha }
}

///|
/// Sample action from the softmax policy.
pub fn sac_sample_action(policy : LinearSoftmaxPolicy, state : Int, rng : Xoshiro) -> Int {
  let x = rl_one_hot(state, policy.n_states)
  let (_l, probs) = policy_forward(policy, x)
  let (a, _) = sample_categorical(probs, rng)
  a
}

///|
/// Compute the SAC soft Bellman target for a single transition.
/// Q_target(s, a) = r + γ · (1 - done) · Σ_a' π(a'|s') · [Q̂_min(s', a') - α · log π(a'|s')]
pub fn sac_soft_target(
  sac : Sac,
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
  let mut expected = 0.0F
  let n_a = sac.policy.n_actions
  for i in 0..<n_a {
    let q_min = if q1_t[i] < q2_t[i] { q1_t[i] } else { q2_t[i] }
    let lp = if pi[i] > 0.0F { logf(pi[i]) } else { -20.0F }
    expected = expected + pi[i] * (q_min - sac.alpha * lp)
  }
  reward + gamma * expected
}

///|
/// Update the SAC critics for one batch. Returns mean squared TD error.
pub fn sac_critic_update(
  sac : Sac,
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
    let target = sac_soft_target(sac, s_next, r, d, gamma)
    let err1 = q1[a] - target
    let err2 = q2[a] - target
    total_loss = total_loss + err1 * err1 + err2 * err2
    // MSE gradient: ∂L/∂Q(a) = 2·err
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
/// Update the SAC actor for one batch (gradient ascent on
/// α·log π - Q). Computes the analytic gradient of
///   L_actor = Σ_a π(a|s) · [α · log π(a|s) - Q(s, a)]
/// Note: signs — we want to MINIMISE -Q + α·log π, so we
/// ASCEND on -Q + α·log π (or equivalently DESCEND on
/// Q - α·log π).
///
/// For each transition:
///   ∇_W[i, j] of (Σ_a π(a|s) · Q(s, a))
///     = Σ_a Q(s, a) · ∇_W[i, j] π(a|s) = Σ_a Q(s, a) · (1{i==a} - π(a|s)) · x[j]
///
///   ∇_W[i, j] of (-α · Σ_a π(a|s) · log π(a|s))
///     = -α · Σ_a [(log π(a|s) + 1) · ∇_W[i, j] π(a|s)]
///     = -α · Σ_a [(log π(a|s) + 1) · (1{i==a} - π(a|s)) · x[j]]
pub fn sac_actor_update(
  sac : Sac,
  states : Array[Int],
  lr : Float,
) -> Unit {
  let n = states.length()
  let n_a = sac.policy.n_actions
  let n_s = sac.policy.n_states
  for k in 0..<n {
    let s = states[k]
    let x = rl_one_hot(s, n_s)
    let q = q_forward(sac.q1, x)
    let (_l, pi) = policy_forward(sac.policy, x)
    // For each weight (i, j), accumulate gradient.
    for i in 0..<n_a {
      // -Q contribution: -Σ_a Q(a) · (1{i==a} - π(a)) · x[j]
      // = -(Q(i) - Σ_a Q(a)·π(a)) · x[j]
      //   for the chosen i=action; for unchosen i, contribution is
      //   +Σ_a Q(a)·π(a) · x[j] (because 1{i==a}=0).
      // -α·H contribution: -α·Σ_a (log π(a) + 1)·(1{i==a} - π(a)) · x[j]
      //   = -α·[(log π(i) + 1)·(1 - π(i)) - Σ_a (log π(a) + 1)·π(a)] · x[j]
      // Combine into single gradient row.
      let mut q_contrib = 0.0F
      let mut h_contrib = 0.0F
      for a in 0..<n_a {
        let indicator = if i == a { 1.0F } else { 0.0F }
        // Q contribution: -(indicator - pi[a]) * q[a] = pi[a]*q[a] - indicator*q[a]
        q_contrib = q_contrib + (pi[a] * q[a] - indicator * q[a])
        // Entropy contribution: -α * (log pi[a] + 1) * (indicator - pi[a])
        let lp = if pi[a] > 0.0F { logf(pi[a]) } else { -20.0F }
        h_contrib = h_contrib + (lp + 1.0F) * (indicator - pi[a])
      }
      // Apply descent on Q - α·H (negative sign from log-of-policy gradient).
      let grad_row = lr * (q_contrib + sac.alpha * h_contrib)
      let row = sac.policy.w[i]
      for j in 0..<n_s {
        row[j] = row[j] - grad_row * x[j]
      }
    }
  }
}

///|
/// Soft (Polyak) update of target Q-nets:
///   θ_target ← τ · θ + (1 - τ) · θ_target
pub fn sac_soft_update(sac : Sac, tau : Float) -> Unit {
  for i in 0..<sac.q1.n_actions {
    for j in 0..<sac.q1.n_states {
      sac.q1_target.w[i][j] = tau * sac.q1.w[i][j] + (1.0F - tau) * sac.q1_target.w[i][j]
      sac.q2_target.w[i][j] = tau * sac.q2.w[i][j] + (1.0F - tau) * sac.q2_target.w[i][j]
    }
    sac.q1_target.b[i] = tau * sac.q1.b[i] + (1.0F - tau) * sac.q1_target.b[i]
    sac.q2_target.b[i] = tau * sac.q2.b[i] + (1.0F - tau) * sac.q2_target.b[i]
  }
}

///|
/// Train SAC for `n_episodes` episodes. Returns the mean episode
/// return over the last `n_episodes` runs.
pub fn train_sac(
  env : GridWorld,
  sac : Sac,
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
      let a = sac_sample_action(sac.policy, state, rng)
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
      let _ = sac_critic_update(sac, ss, aa, rr, nss, dd, gamma, lr_critic)
      let _ = sac_actor_update(sac, ss, lr_actor)
      sac_soft_update(sac, tau)
    }
  }
  if return_count > 0 {
    total_return / Float::from_int(return_count)
  } else {
    0.0F
  }
}