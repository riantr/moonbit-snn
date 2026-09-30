// dqn_n_step.mbt — N-step DQN (Sutton 1988; Mnih et al. 2016 §4.2).
//
// 1-step TD target:    y_t = r_t + γ · max_a' Q̂(s_{t+1}, a')
// n-step TD target:    y_t = Σ_{k=0}^{n-1} γ^k · r_{t+k} + γ^n · max_a' Q̂(s_{t+n}, a')
//
// With truncation: if any transition r_{t+k} is terminal, the sum stops at k
// and γ^n · max ... is replaced by 0 (no bootstrap from a terminal future).
//
// We combine this with the standard (uniform) `ReplayBuffer` from `dqn.mbt`.
// Pairing with `PrioritizedReplayBuffer` is also possible — the priority can
// be the magnitude of the n-step TD error.
//
// Per-step pipeline (during a rollout):
//   1. nstep.push(s, a, r, s', done)
//   2. If nstep.len() == n OR done:
//        extract n-step transition G, h, s_root, s_lookahead
//        if (h == n) or (s_lookahead corresponds to truncated terminal):
//          replay.push(s_root, a_root, G, s_lookahead, done_flag)
//        if done: nstep.reset()
//   3. If replay.len() >= batch_size and ep >= warmup:
//        sample mini-batch
//        dqn_update_step_n_step(...)
//   4. Periodically copy online → target.

///|
/// N-step variant of `dqn_update_step`. Each row in the batch is an n-step
/// transition:
///
///   (s_root, a_root, G_n, s_lookahead, h_eff, use_bootstrap)
///
/// where:
///
/// * `G_n`            — discounted reward sum Σ γ^k r_{root+k}
/// * `s_lookahead`    — the s_{root + h_eff} used for bootstrap
/// * `h_eff`          — effective horizon (number of rewards summed, 1..=n)
/// * `use_bootstrap`  — true if `h_eff == n` and the n-step window is non-terminal
///
/// Loss for a row (with bootstrap):
///
///     δ = G_n + γ^h_eff · max_a' Q̂(s_lookahead, a') - Q(s_root, a_root)
///
/// Without bootstrap (terminal reached inside window):
///
///     δ = G_n - Q(s_root, a_root)
///
/// Returns mean |δ| over the batch.
pub fn dqn_update_step_n_step(
  online : LinearQNet,
  target : LinearQNet,
  states : Array[Int],
  actions : Array[Int],
  rewards : Array[Float], // G_n
  next_states : Array[Int], // s_lookahead
  h_effs : Array[Int], // effective horizons
  use_bootstraps : Array[Bool],
  gamma : Float,
  lr : Float,
) -> Float {
  let n = states.length()
  let mut total_abs_delta = 0.0F
  for k in 0..<n {
    let s = states[k]
    let a = actions[k]
    let g_n = rewards[k]
    let s_look = next_states[k]
    let h = h_effs[k]
    let use_b = use_bootstraps[k]
    let x = rl_one_hot(s, online.n_states)
    let q = q_forward(online, x)
    let q_sa = q[a]
    // Bootstrap term: γ^h · max_a' Q̂(s_lookahead, a')
    let target_value = if use_b {
      let x_next = rl_one_hot(s_look, target.n_states)
      let q_next = q_forward(target, x_next)
      let mut disc = 1.0F
      for _i in 0..<h {
        disc = disc * gamma
      }
      g_n + disc * q_max(q_next)
    } else {
      g_n
    }
    let delta = target_value - q_sa
    total_abs_delta = total_abs_delta + (if delta < 0.0F { -delta } else { delta })
    // Gradient: ∂δ²/∂w[a,j] = -2·δ·x[j]
    let scale = 2.0F * lr * delta
    for j in 0..<online.n_states {
      online.w[a][j] = online.w[a][j] + scale * x[j]
    }
    online.b[a] = online.b[a] + scale
  }
  total_abs_delta / Float::from_int(n)
}

///|
/// N-step variant of `train_dqn`. Maintains an `NStepBuffer` of the last `n`
/// transitions; whenever the buffer is full (or the episode ends), the
/// corresponding n-step transition is flushed into the replay buffer.
///
/// Returns the mean undiscounted episode return over the last 10 episodes.
pub fn train_dqn_n_step(
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
  n_step : Int,
  seed : UInt64,
) -> Float {
  let rng = Xoshiro::from_state(seed, seed + 7UL, seed + 13UL, seed + 17UL)
  let buffer = ReplayBuffer::new(buffer_capacity)
  let nstep = NStepBuffer::new(n_step)
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
      ep_return = ep_return + r
      // Push into n-step buffer; extract a full transition if window ready.
      let full = nstep.push(state, a, r, next_state, d)
      if full {
        let (g_n, h_eff, s_root, s_look, trunc) = nstep.n_step_return(0, gamma)
        // s_root and a_root are the (s, a) at index 0 of the buffer.
        let root_rec = nstep.at(0)
        let use_bootstrap = !trunc
        let _ = use_bootstrap
        buffer.push(s_root, root_rec.a, g_n, s_look, trunc)
        let _ = h_eff
      }
      // If episode ends before window fills, flush whatever we have.
      if d {
        // Flush all k in [0, nstep.len() - 1] as shorter-horizon transitions.
        let mut k = 0
        while k < nstep.len() {
          let (g_short, h_eff, s_root, s_look, trunc) = nstep.n_step_return(k, gamma)
          let rec = nstep.at(k)
          let use_bootstrap = !trunc
          let _ = use_bootstrap
          buffer.push(s_root, rec.a, g_short, s_look, trunc)
          let _ = h_eff
          k = k + 1
        }
        nstep.reset()
      }
      state = next_state
      done = d
      t = t + 1
    }
    total_return = total_return + ep_return
    return_count = return_count + 1
    // Train.
    if ep >= warmup_episodes && buffer.len() >= batch_size {
      let (ss, aa, rr, nss, dd) = buffer.sample(batch_size, rng)
      let _ = dqn_update_step_n_step_via_replay(q_net, target, ss, aa, rr, nss, dd, gamma, lr, n_step)
    }
    // Sync target.
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
/// Adapter: replay buffer rows don't carry h_eff or use_bootstrap, but we can
/// reconstruct them when n == h_eff and the row's `done` flag is false. For
/// rows with done == true, h_eff is 1 and bootstrap is disabled.
///
/// This adapter assumes the training pipeline produced rows with h_eff = n_step
/// (no early truncation). The flag `done == true` is interpreted as
/// "do not bootstrap".
fn dqn_update_step_n_step_via_replay(
  online : LinearQNet,
  target : LinearQNet,
  states : Array[Int],
  actions : Array[Int],
  rewards : Array[Float],
  next_states : Array[Int],
  dones : Array[Bool],
  gamma : Float,
  lr : Float,
  n_step : Int,
) -> Float {
  let n = states.length()
  let h_arr : Array[Int] = Array::make(n, n_step)
  let use_arr : Array[Bool] = Array::make(n, true)
  for k in 0..<n {
    if dones[k] {
      use_arr[k] = false
    }
  }
  dqn_update_step_n_step(
    online, target, states, actions, rewards, next_states, h_arr, use_arr, gamma, lr,
  )
}