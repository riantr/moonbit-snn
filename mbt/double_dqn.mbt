// double_dqn.mbt — Double DQN (v0.37.0).
//
// Double DQN (van Hasselt et al. 2016) decouples action selection
// from action evaluation to reduce Q-value overestimation bias:
//
//   Vanilla:  target = r + γ · max_a' Q̂(s', a'; θ_target)
//   Double:   a*      = argmax_a' Q(s', a'; θ_online)   (online net)
//             target  = r + γ · Q̂(s', a*; θ_target)       (target net)
//
// The two nets have the same architecture but may be at different
// optimisation stages. Using the online net to pick the action
// (less biased than the target net, which has accumulated over-
// estimation error) and the target net to evaluate it gives a
// lower-variance TD target.
//
// Reuses:
//   - `LinearQNet`, `ReplayBuffer`, `qnet_copy`, `q_forward`,
//     `q_argmax`, `q_max` from dqn.mbt

///|
/// Double DQN update step. Same signature as `dqn_update_step`
/// but uses the online net for argmax selection.
///
/// Returns mean |δ| for monitoring.
pub fn double_dqn_update_step(
  online : LinearQNet,
  target : LinearQNet,
  states : Array[Int],
  actions : Array[Int],
  rewards : Array[Float],
  next_states : Array[Int],
  dones : Array[Bool],
  gamma : Float,
  lr : Float,
) -> Float {
  let n = states.length()
  let mut total_abs_delta = 0.0F
  for k in 0..<n {
    let s = states[k]
    let a = actions[k]
    let r = rewards[k]
    let s_next = next_states[k]
    let d_t = dones[k]
    let x = rl_one_hot(s, online.n_states)
    let q = q_forward(online, x)
    let q_sa = q[a]
    // Compute target using Double DQN rule.
    let target_value = if d_t {
      r
    } else {
      let x_next = rl_one_hot(s_next, online.n_states)
      let q_online_next = q_forward(online, x_next)
      let a_star = q_argmax(q_online_next)
      let q_target_next = q_forward(target, x_next)
      r + gamma * q_target_next[a_star]
    }
    let delta = target_value - q_sa
    total_abs_delta = total_abs_delta + (if delta < 0.0F { -delta } else { delta })
    // Gradient step on online (same as vanilla DQN).
    let scale = 2.0F * lr * delta
    for j in 0..<online.n_states {
      online.w[a][j] = online.w[a][j] + scale * x[j]
    }
    online.b[a] = online.b[a] + scale
  }
  total_abs_delta / Float::from_int(n)
}

///|
/// Train Double DQN for `n_episodes`. Same loop structure as
/// `train_dqn` but uses Double DQN update rule. Returns mean
/// episode return over all episodes.
pub fn train_double_dqn(
  env : GridWorld,
  q_net : LinearQNet,
  target : LinearQNet,
  n_episodes : Int,
  gamma : Float,
  lr : Float,
  epsilon_start : Float,
  epsilon_end : Float,
  max_steps : Int,
  buffer_capacity : Int,
  warmup_episodes : Int,
  sync_every : Int,
  batch_size : Int,
  seed : UInt64,
) -> Float {
  let rng = Xoshiro::from_state(seed, seed + 7UL, seed + 13UL, seed + 17UL)
  let buffer = ReplayBuffer::new(buffer_capacity)
  let mut total_return = 0.0F
  let mut return_count = 0
  for ep in 0..<n_episodes {
    let frac = Float::from_int(ep) / Float::from_int(n_episodes)
    let epsilon = epsilon_start + frac * (epsilon_end - epsilon_start)
    let mut state = env.start
    let mut done = false
    let mut t = 0
    let mut ep_return = 0.0F
    while !done && t < max_steps {
      let a = eps_greedy_action(q_net, state, epsilon, rng)
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
      let _ = double_dqn_update_step(q_net, target, ss, aa, rr, nss, dd, gamma, lr)
    }
    if (ep + 1) % sync_every == 0 {
      qnet_copy(target, q_net)
    }
  }
  if return_count > 0 {
    total_return / Float::from_int(return_count)
  } else {
    0.0F
  }
}

///|
/// Compute the absolute difference between Double DQN and vanilla
/// DQN target values for a batch. Useful as a sanity check that the
/// decoupled rule produces different gradients than the coupled rule
/// (which it should whenever online != target).
pub fn double_dqn_vs_vanilla_diff(
  online : LinearQNet,
  target : LinearQNet,
  next_states : Array[Int],
  gamma : Float,
) -> Float {
  let n = next_states.length()
  let mut total_diff = 0.0F
  for k in 0..<n {
    let s_next = next_states[k]
    let x_next = rl_one_hot(s_next, online.n_states)
    let q_online_next = q_forward(online, x_next)
    let q_target_next = q_forward(target, x_next)
    let a_star = q_argmax(q_online_next)
    let double_q = q_target_next[a_star]
    let vanilla_q = q_max(q_target_next)
    let diff = double_q - vanilla_q
    total_diff = total_diff + (if diff < 0.0F { -diff } else { diff })
  }
  total_diff / Float::from_int(n)
}