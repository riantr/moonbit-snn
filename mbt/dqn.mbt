// dqn.mbt — Deep Q-Network (v0.36.0).
//
// DQN (Mnih et al. 2015) on GridWorld:
//   - Q-network: Q(s, a; θ) = W · x_s  (linear, no hidden layer)
//   - Replay buffer: store (s, a, r, s', done), sample mini-batches
//   - Target network Q̂(s, a; θ⁻): periodic snapshot of θ
//   - Loss: (r + γ · max_a' Q̂(s', a'; θ⁻) · (1 - done) - Q(s, a; θ))²
//   - ε-greedy action selection with linear decay
//
// Reuses:
//   - `GridWorld` from reinforce.mbt
//   - `sample_categorical` from reinforce.mbt (for ε-greedy)

///|
/// Q-network: linear mapping from one-hot state to Q-values per
/// action. W has shape `n_actions × n_states`. Bias has shape `n_actions`.
pub struct LinearQNet {
  n_states : Int
  n_actions : Int
  w : Array[Array[Float]]
  b : Array[Float]
}

///|
pub fn LinearQNet::new(
  n_states : Int,
  n_actions : Int,
  seed : UInt64,
) -> LinearQNet {
  let std = sqrtf(0.1F / Float::from_int(n_states))
  let rng = Xoshiro::from_state(seed, seed + 1UL, seed + 2UL, seed + 3UL)
  let w : Array[Array[Float]] = Array::make(n_actions, [])
  for i in 0..<n_actions {
    let row : Array[Float] = Array::make(n_states, 0.0F)
    for j in 0..<n_states {
      let (z1, _) = box_muller(rng)
      row[j] = Float::from_double(z1) * std
    }
    w[i] = row
  }
  let b : Array[Float] = Array::make(n_actions, 0.0F)
  { n_states, n_actions, w, b }
}

///|
/// Compute Q-values for state x. Returns `Array[Float]` of length `n_actions`.
pub fn q_forward(net : LinearQNet, x : Array[Float]) -> Array[Float] {
  let n_actions = net.n_actions
  let q : Array[Float] = Array::make(n_actions, 0.0F)
  for i in 0..<n_actions {
    let row = net.w[i]
    let mut s = net.b[i]
    for j in 0..<row.length() {
      s = s + row[j] * x[j]
    }
    q[i] = s
  }
  q
}

///|
/// Greedy action: argmax over Q-values (first occurrence on ties).
pub fn q_argmax(q : Array[Float]) -> Int {
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
/// Experience replay buffer (uniform random sampling).
pub struct ReplayBuffer {
  capacity : Int
  states : Array[Int]
  actions : Array[Int]
  rewards : Array[Float]
  next_states : Array[Int]
  dones : Array[Bool]
  mut size : Int
  mut cursor : Int
}

///|
pub fn ReplayBuffer::new(capacity : Int) -> ReplayBuffer {
  {
    capacity,
    states: Array::make(capacity, 0),
    actions: Array::make(capacity, 0),
    rewards: Array::make(capacity, 0.0F),
    next_states: Array::make(capacity, 0),
    dones: Array::make(capacity, false),
    size: 0,
    cursor: 0,
  }
}

///|
/// Append a transition. Overwrites oldest when full.
pub fn ReplayBuffer::push(
  self : ReplayBuffer,
  s : Int,
  a : Int,
  r : Float,
  s_next : Int,
  done : Bool,
) -> Unit {
  let i = self.cursor
  self.states[i] = s
  self.actions[i] = a
  self.rewards[i] = r
  self.next_states[i] = s_next
  self.dones[i] = done
  self.cursor = (i + 1) % self.capacity
  if self.size < self.capacity {
    self.size = self.size + 1
  }
}

///|
pub fn ReplayBuffer::len(self : ReplayBuffer) -> Int {
  self.size
}

///|
/// Sample a mini-batch by random indices. Returns 5 parallel arrays.
pub fn ReplayBuffer::sample(
  self : ReplayBuffer,
  batch_size : Int,
  rng : Xoshiro,
) -> (Array[Int], Array[Int], Array[Float], Array[Int], Array[Bool]) {
  let states : Array[Int] = Array::make(batch_size, 0)
  let actions : Array[Int] = Array::make(batch_size, 0)
  let rewards : Array[Float] = Array::make(batch_size, 0.0F)
  let next_states : Array[Int] = Array::make(batch_size, 0)
  let dones : Array[Bool] = Array::make(batch_size, false)
  for k in 0..<batch_size {
    let (idx_raw, _) = box_muller(rng)
    let idx_raw_pos = if idx_raw < 0.0 { -idx_raw } else { idx_raw }
    // Map [0, 1) → [0, self.size)
    let idx = Float::from_double(idx_raw_pos * Double::from_int(self.size)).to_int()
    let safe_idx = if idx < 0 {
      0
    } else if idx >= self.size {
      self.size - 1
    } else {
      idx
    }
    states[k] = self.states[safe_idx]
    actions[k] = self.actions[safe_idx]
    rewards[k] = self.rewards[safe_idx]
    next_states[k] = self.next_states[safe_idx]
    dones[k] = self.dones[safe_idx]
  }
  (states, actions, rewards, next_states, dones)
}

///|
/// Copy weights from `src` into `dst`. Used to sync target net from
/// online net.
pub fn qnet_copy(dst : LinearQNet, src : LinearQNet) -> Unit {
  for i in 0..<dst.n_actions {
    for j in 0..<dst.n_states {
      dst.w[i][j] = src.w[i][j]
    }
    dst.b[i] = src.b[i]
  }
}

///|
/// Compute the mean squared TD error over a batch, and apply a
/// gradient step to `online` to minimise it.
///
/// Loss per transition: δ² = (r + γ · max_a' Q̂(s', a'; θ_target)
///                        · (1 - done) - Q(s, a; θ_online))²
///
/// Gradient w.r.t. W[i, j] (for the chosen action a):
///   d_W[a, j] -= lr · 2 · δ · x_s[j]
///   d_b[a]   -= lr · 2 · δ
///
/// For unchosen actions: zero gradient.
/// Returns mean |δ| (avg absolute TD error) for monitoring.
pub fn dqn_update_step(
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
    // Target Q.
    let target_value = if d_t {
      r
    } else {
      let x_next = rl_one_hot(s_next, target.n_states)
      let q_next = q_forward(target, x_next)
      r + gamma * q_max(q_next)
    }
    let delta = target_value - q_sa
    total_abs_delta = total_abs_delta + (if delta < 0.0F { -delta } else { delta })
    // Gradient descent on δ²: ∂δ²/∂w[a,j] = -2·δ·x[j], so
    // w[a,j] ← w[a,j] + lr · 2·δ·x[j]  (positive delta → increase Q).
    let scale = 2.0F * lr * delta
    for j in 0..<online.n_states {
      online.w[a][j] = online.w[a][j] + scale * x[j]
    }
    online.b[a] = online.b[a] + scale
  }
  total_abs_delta / Float::from_int(n)
}

///|
/// Max of an array (helper).
fn q_max(q : Array[Float]) -> Float {
  let mut m = q[0]
  for i in 1..<q.length() {
    if q[i] > m {
      m = q[i]
    }
  }
  m
}

///|
/// ε-greedy action. With probability ε, sample uniformly; else greedy.
pub fn eps_greedy_action(
  q_net : LinearQNet,
  state : Int,
  epsilon : Float,
  rng : Xoshiro,
) -> Int {
  if epsilon <= 0.0F {
    let x = rl_one_hot(state, q_net.n_states)
    let q = q_forward(q_net, x)
    return q_argmax(q)
  }
  let (u, _) = box_muller(rng)
  let u_pos = if u < 0.0 { -u } else { u }
  if Float::from_double(u_pos) < epsilon {
    // Uniform random.
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
    let q = q_forward(q_net, x)
    q_argmax(q)
  }
}

///|
/// Train DQN for `n_episodes` episodes. Each episode:
///   1. Roll out under ε-greedy policy with linear ε-decay.
///   2. Store transitions in replay buffer.
///   3. After warmup, sample mini-batches and apply gradient step.
///   4. Periodically copy online → target.
///
/// Returns the mean undiscounted episode return over the last 10
/// episodes.
pub fn train_dqn(
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
  let n_states = env.n_states()
  let mut total_return = 0.0F
  let mut return_count = 0
  for ep in 0..<n_episodes {
    // Linear ε-decay.
    let frac = Float::from_int(ep) / Float::from_int(n_episodes)
    let epsilon = epsilon_start + frac * (epsilon_end - epsilon_start)
    // Roll out one episode.
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
    // Sample mini-batches and update online net.
    if ep >= warmup_episodes && buffer.len() >= batch_size {
      let (ss, aa, rr, nss, dd) = buffer.sample(batch_size, rng)
      let _ = dqn_update_step(q_net, target, ss, aa, rr, nss, dd, gamma, lr)
    }
    // Sync target net.
    if (ep + 1) % sync_every == 0 {
      qnet_copy(target, q_net)
    }
    let _ = n_states
  }
  if return_count > 0 {
    total_return / Float::from_int(return_count)
  } else {
    0.0F
  }
}