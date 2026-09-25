// dueling_dqn.mbt — Dueling DQN (v0.37.1).
//
// Dueling DQN (Wang et al. 2016) splits Q(s, a) into a value
// stream V(s) and an advantage stream A(s, a):
//
//   Q(s, a; θ) = V(s; θ_v) + A(s, a; θ_a) - (1/|A|) · Σ_{a'} A(s, a'; θ_a)
//
// The mean-subtraction makes the advantage stream centred: at any
// state the average advantage across actions is 0, so Q reflects
// the relative value of each action. This helps when many actions
// have similar values at a state (no need to learn the magnitude
// of V separately for each action).
//
// Reuses:
//   - `LinearQNet` from dqn.mbt (we use one net for V, one for A)
//   - `ReplayBuffer`, `qnet_copy` from dqn.mbt

///|
/// Dueling Q-network: two LinearQNets (value + advantage).
pub struct DuelingQNet {
  n_states : Int
  n_actions : Int
  value : LinearQNet
  advantage : LinearQNet
}

///|
pub fn DuelingQNet::new(
  n_states : Int,
  n_actions : Int,
  seed : UInt64,
) -> DuelingQNet {
  let value = LinearQNet::new(n_states, n_actions, seed)
  let advantage = LinearQNet::new(n_states, n_actions, seed + 17UL)
  { n_states, n_actions, value, advantage }
}

///|
/// Forward: Q(s, a) = V(s) + A(s, a) - mean_a A(s, a).
pub fn dueling_q_forward(net : DuelingQNet, x : Array[Float]) -> Array[Float] {
  let n_a = net.n_actions
  let v = q_forward(net.value, x)
  let a = q_forward(net.advantage, x)
  let v_scalar = v[0]  // value stream outputs length n_actions; we use index 0 as V(s).
  // Compute mean advantage.
  let mut mean_a = 0.0F
  for i in 0..<n_a {
    mean_a = mean_a + a[i]
  }
  mean_a = mean_a / Float::from_int(n_a)
  let q : Array[Float] = Array::make(n_a, 0.0F)
  for i in 0..<n_a {
    q[i] = v_scalar + a[i] - mean_a
  }
  q
}

///|
/// Greedy action (argmax over Q).
pub fn dueling_q_argmax(q : Array[Float]) -> Int {
  let mut best = 0
  let mut best_v = q[0]
  for i in 1..<q.length() {
    if q[i] > best_v {
      best_v = q[i]
      best = i
    }
  }
  best
}

///|
/// Mean of an array (helper).
fn array_mean(a : Array[Float]) -> Float {
  if a.length() == 0 {
    return 0.0F
  }
  let mut s = 0.0F
  for i in 0..<a.length() {
    s = s + a[i]
  }
  s / Float::from_int(a.length())
}

///|
/// Compute the analytic gradient of the squared TD error for the
/// Dueling architecture. For each (s, a) pair:
//   Q(s, a) = V(s) + A(s, a) - mean_a A(s, a)
//
// ∂Q/∂A(s, a) = 1 - 1/|A|  (for the chosen action a)
// ∂Q/∂A(s, a') = -1/|A|  (for all other actions a' ≠ a)
// ∂Q/∂V(s) = 1
//
// So for chosen action a:
//   d_A[a, j] += lr · 2·δ · (1 - 1/|A|) · x[j]
//   d_V[j]    += lr · 2·δ · x[j]
//   d_b_V     += lr · 2·δ
// For unchosen a':
//   d_A[a', j] += lr · 2·δ · (-1/|A|) · x[j]
//   d_V[j]    += lr · 2·δ · x[j]    (V gradient doesn't depend on a)
//   d_b_V     += lr · 2·δ
//
// Returns mean |δ|.
pub fn dueling_dqn_update_step(
  online : DuelingQNet,
  target : DuelingQNet,
  states : Array[Int],
  actions : Array[Int],
  rewards : Array[Float],
  next_states : Array[Int],
  dones : Array[Bool],
  gamma : Float,
  lr : Float,
) -> Float {
  let n = states.length()
  let n_a = online.n_actions
  let n_s = online.n_states
  let mut total_abs_delta = 0.0F
  for k in 0..<n {
    let s = states[k]
    let a = actions[k]
    let r = rewards[k]
    let s_next = next_states[k]
    let d_t = dones[k]
    let x = rl_one_hot(s, n_s)
    let q = dueling_q_forward(online, x)
    let q_sa = q[a]
    let target_value = if d_t {
      r
    } else {
      let x_next = rl_one_hot(s_next, n_s)
      let q_target_next = dueling_q_forward(target, x_next)
      r + gamma * array_max(q_target_next)
    }
    let delta = target_value - q_sa
    total_abs_delta = total_abs_delta + (if delta < 0.0F { -delta } else { delta })
    let scale = 2.0F * lr * delta
    let inv_n = 1.0F / Float::from_int(n_a)
    // Update value stream: V(s) is shared across actions.
    let x_arr = x
    for j in 0..<n_s {
      online.value.w[0][j] = online.value.w[0][j] + scale * x_arr[j]
    }
    online.value.b[0] = online.value.b[0] + scale
    // Update advantage stream: per-action contribution.
    for i in 0..<n_a {
      let indicator_a = if i == a { 1.0F } else { 0.0F }
      let coef = indicator_a - inv_n
      for j in 0..<n_s {
        online.advantage.w[i][j] = online.advantage.w[i][j] + scale * coef * x_arr[j]
      }
      online.advantage.b[i] = online.advantage.b[i] + scale * coef
    }
  }
  total_abs_delta / Float::from_int(n)
}

///|
/// Max of an array (helper, redefined for clarity).
fn array_max(a : Array[Float]) -> Float {
  let mut m = a[0]
  for i in 1..<a.length() {
    if a[i] > m {
      m = a[i]
    }
  }
  m
}

///|
/// Copy weights from `src` into `dst`.
pub fn dueling_qnet_copy(dst : DuelingQNet, src : DuelingQNet) -> Unit {
  qnet_copy(dst.value, src.value)
  qnet_copy(dst.advantage, src.advantage)
}

///|
/// ε-greedy action using dueling Q-net.
pub fn dueling_eps_greedy_action(
  q_net : DuelingQNet,
  state : Int,
  epsilon : Float,
  rng : Xoshiro,
) -> Int {
  if epsilon <= 0.0F {
    let x = rl_one_hot(state, q_net.n_states)
    let q = dueling_q_forward(q_net, x)
    return dueling_q_argmax(q)
  }
  let (u, _) = box_muller(rng)
  let u_pos = if u < 0.0 { -u } else { u }
  if Float::from_double(u_pos) < epsilon {
    let (u2, _) = box_muller(rng)
    let n_a = q_net.n_actions
    let idx_raw = if u2 < 0.0 { -u2 } else { u2 }
    let idx = Float::from_double(idx_raw * Double::from_int(n_a)).to_int()
    if idx < 0 {
      0
    } else if idx >= n_a {
      n_a - 1
    } else {
      idx
    }
  } else {
    let x = rl_one_hot(state, q_net.n_states)
    let q = dueling_q_forward(q_net, x)
    dueling_q_argmax(q)
  }
}

///|
/// Train Dueling DQN. Returns mean episode return.
pub fn train_dueling_dqn(
  env : GridWorld,
  q_net : DuelingQNet,
  target : DuelingQNet,
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
      let a = dueling_eps_greedy_action(q_net, state, epsilon, rng)
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
      let _ = dueling_dqn_update_step(q_net, target, ss, aa, rr, nss, dd, gamma, lr)
    }
    if (ep + 1) % sync_every == 0 {
      dueling_qnet_copy(target, q_net)
    }
  }
  if return_count > 0 {
    total_return / Float::from_int(return_count)
  } else {
    0.0F
  }
}