// rainbow_lite.mbt — Rainbow lite (v0.37.2).
//
// Rainbow (Hessel et al. 2017) is a combination of 6 DQN improvements:
//   - Double DQN  (decoupled action selection/evaluation)
//   - Dueling     (V(s) + A(s,a) - mean_a A(s,a))
//   - Prioritized replay (sample proportional to |δ|^α)
//   - Multi-step  (n-step returns instead of 1-step)
//   - Distributional (full Q distribution; we skip for simplicity)
//   - Noisy Nets  (parameter noise; we skip for simplicity)
//
// This lite version combines: Double + Dueling + Prioritized +
// Multi-step (the four most impactful components).

///|
/// Prioritized replay buffer. Each transition has an associated
/// priority (|TD error|^α + ε). Sampling is done with probability
/// p_i ∝ priority_i. Importance-sampling weights correct for the
/// bias.
pub struct PrioritizedBuffer {
  capacity : Int
  states : Array[Int]
  actions : Array[Int]
  rewards : Array[Float]
  next_states : Array[Int]
  dones : Array[Bool]
  /// Per-step priority (|δ|^α + ε). Higher → more likely to be sampled.
  priorities : Array[Float]
  /// Running sum of priorities (for cumulative sampling).
  alpha : Float
  /// Importance-sampling exponent (β → 0 = no correction, β = 1 = full).
  beta : Float
  /// Epsilon to keep priority > 0 even after perfect prediction.
  epsilon : Float
  mut size : Int
  mut cursor : Int
  mut total_priority : Float
}

///|
pub fn PrioritizedBuffer::new(
  capacity : Int,
  alpha : Float,
  beta : Float,
  epsilon : Float,
) -> PrioritizedBuffer {
  {
    capacity,
    states: Array::make(capacity, 0),
    actions: Array::make(capacity, 0),
    rewards: Array::make(capacity, 0.0F),
    next_states: Array::make(capacity, 0),
    dones: Array::make(capacity, false),
    priorities: Array::make(capacity, 1.0F),
    alpha,
    beta,
    epsilon,
    size: 0,
    cursor: 0,
    total_priority: 0.0F,
  }
}

///|
/// Push a new transition with its initial priority. The caller
/// should later call `update_priority(i, new_priority)` to refresh
/// after computing TD errors.
pub fn PrioritizedBuffer::push(
  self : PrioritizedBuffer,
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
  self.priorities[i] = 1.0F
  self.total_priority = self.total_priority + 1.0F
  self.cursor = (i + 1) % self.capacity
  if self.size < self.capacity {
    self.size = self.size + 1
  }
}

///|
pub fn PrioritizedBuffer::len(self : PrioritizedBuffer) -> Int {
  self.size
}

///|
/// Update priority for transition at index `i`.
pub fn PrioritizedBuffer::update_priority(
  self : PrioritizedBuffer,
  i : Int,
  abs_delta : Float,
) -> Unit {
  let new_p = pow_fast(abs_delta, self.alpha) + self.epsilon
  let old_p = self.priorities[i]
  self.priorities[i] = new_p
  self.total_priority = self.total_priority + new_p - old_p
}

///|
/// Approximate x^y via expf(y * logf(x)) for x ≥ 0. (Used for
/// priority^(alpha).)
pub fn pow_fast(x : Float, y : Float) -> Float {
  if x <= 0.0F {
    return 0.0F
  }
  expf(y * logf(x))
}

///|
/// Sample a mini-batch with probability ∝ priority^alpha. Returns
/// indices, transitions, and importance-sampling weights.
pub fn PrioritizedBuffer::sample(
  self : PrioritizedBuffer,
  batch_size : Int,
  rng : Xoshiro,
) -> (Array[Int], Array[Int], Array[Int], Array[Float], Array[Int], Array[Bool], Array[Float]) {
  let indices : Array[Int] = Array::make(batch_size, 0)
  let states : Array[Int] = Array::make(batch_size, 0)
  let actions : Array[Int] = Array::make(batch_size, 0)
  let rewards : Array[Float] = Array::make(batch_size, 0.0F)
  let next_states : Array[Int] = Array::make(batch_size, 0)
  let dones : Array[Bool] = Array::make(batch_size, false)
  let is_weights : Array[Float] = Array::make(batch_size, 0.0F)
  if self.size == 0 || self.total_priority <= 0.0F {
    return (indices, states, actions, rewards, next_states, dones, is_weights)
  }
  for k in 0..<batch_size {
    let (u_raw, _) = box_muller(rng)
    let u = if u_raw < 0.0 { -u_raw } else { u_raw }
    // Map [0, 1) → cumulative priority range.
    let target = Float::from_double(u) * self.total_priority
    let mut acc = 0.0F
    let mut found_idx = self.size - 1
    let mut i = 0
    while i < self.size {
      acc = acc + self.priorities[i]
      if acc >= target {
        found_idx = i
        break
      }
      i = i + 1
    }
    indices[k] = found_idx
    states[k] = self.states[found_idx]
    actions[k] = self.actions[found_idx]
    rewards[k] = self.rewards[found_idx]
    next_states[k] = self.next_states[found_idx]
    dones[k] = self.dones[found_idx]
    // Importance-sampling weight: w_i = (N · P(i))^(-β), normalised by max.
    // P(i) = priority_i / total_priority
    let p_i = self.priorities[found_idx] / self.total_priority
    let n_over_p = Float::from_int(self.size) / p_i
    let w_raw = pow_fast(n_over_p, -self.beta)
    is_weights[k] = w_raw
  }
  // Normalise IS weights by max.
  let mut max_w = is_weights[0]
  for k in 1..<batch_size {
    if is_weights[k] > max_w {
      max_w = is_weights[k]
    }
  }
  if max_w > 0.0F {
    for k in 0..<batch_size {
      is_weights[k] = is_weights[k] / max_w
    }
  }
  (indices, states, actions, rewards, next_states, dones, is_weights)
}

///|
/// Compute the n-step return from a contiguous subsequence of
/// transitions starting at index `t`:
///   G_t = r_t + γ·r_{t+1} + γ²·r_{t+2} + ... + γ^(n-1)·r_{t+n-1}
///        + γ^n · max_a' Q̂(s_{t+n}, a')
///
/// If `done[k]` is true at any intermediate step, the n-step return
/// truncates cleanly: g = 0 (no future rewards, no bootstrap).
pub fn multi_step_return(
  rewards : Array[Float],
  dones : Array[Bool],
  q_next_max : Float,
  gamma : Float,
  n_steps : Int,
  t : Int,
) -> Float {
  // Bootstrap: γ^n_steps · max_Q
  let mut g = q_next_max
  let mut i = 0
  while i < n_steps {
    g = g * gamma
    i = i + 1
  }
  // Accumulate γ^k · r[t+k] for k = 0..n_steps-1
  let mut discount = 1.0F
  let mut k = 0
  while k < n_steps {
    let idx = t + k
    if idx >= rewards.length() {
      break
    }
    if dones[idx] {
      // Truncate cleanly: zero g, stop.
      g = 0.0F
      break
    }
    g = g + rewards[idx] * discount
    discount = discount * gamma
    k = k + 1
  }
  g
}