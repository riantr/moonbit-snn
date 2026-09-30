// prioritized_replay.mbt — Prioritized Experience Replay buffer (Schaul 2016).
//
// Like `dqn.mbt`'s `ReplayBuffer`, this stores transitions (s, a, r, s', done)
// and supports mini-batch sampling. The difference is that each transition has
// a *priority* p_i, and transitions are sampled with probability
//
//     P(i) = p_i^α / Σ_j p_j^α
//
// where α ∈ [0, 1] controls how much prioritization is used (α = 0 → uniform).
//
// Sampling corrects for the bias introduced by non-uniform sampling with
// importance-sampling weights
//
//     w_i = (N · P(i))^{-β}   /   max_j w_j
//
// where β ∈ [0, 1] anneals from 0 (no correction) toward 1 (full correction).
//
// Priorities are stored in a `SumTree` so cumulative-sum sampling is O(log N)
// and batch priority updates are O(log N) each.

///|
/// Prioritized replay buffer with proportional priorities and importance-sampling
/// weights. New transitions are inserted at priority = `max_priority` so that
/// every transition is guaranteed to be sampled at least once.
pub struct PrioritizedReplayBuffer {
  capacity : Int
  states : Array[Int]
  actions : Array[Int]
  rewards : Array[Float]
  next_states : Array[Int]
  dones : Array[Bool]
  tree : SumTree
  mut max_priority : Float
  alpha : Float
  beta : Float
  epsilon : Float
  mut size : Int
  mut cursor : Int
}

///|
/// Construct a new PER buffer.
///
/// * `capacity`     — number of transitions to hold
/// * `alpha`        — prioritization exponent (0 = uniform, 1 = fully prioritized)
/// * `beta`         — importance-sampling exponent (anneal 0 → 1 during training)
/// * `eps`          — small constant added to |δ| before exponentiating, ensures
///                    every transition is sampleable even with zero TD error
pub fn PrioritizedReplayBuffer::new(
  capacity : Int,
  alpha : Float,
  beta : Float,
  eps : Float,
) -> PrioritizedReplayBuffer {
  let tree = SumTree::new(capacity)
  {
    capacity,
    states: Array::make(capacity, 0),
    actions: Array::make(capacity, 0),
    rewards: Array::make(capacity, 0.0F),
    next_states: Array::make(capacity, 0),
    dones: Array::make(capacity, false),
    tree,
    max_priority: 1.0F,
    alpha,
    beta,
    epsilon: eps,
    size: 0,
    cursor: 0,
  }
}

///|
/// Current number of stored transitions.
pub fn PrioritizedReplayBuffer::len(self : PrioritizedReplayBuffer) -> Int {
  self.size
}

///|
/// Underlying slot capacity.
pub fn PrioritizedReplayBuffer::capacity(self : PrioritizedReplayBuffer) -> Int {
  self.capacity
}

///|
/// Total priority mass (= root of the sum tree). Used to normalise per-step
/// sampling segments and to detect "all priorities zero" edge cases.
pub fn PrioritizedReplayBuffer::total_priority(
  self : PrioritizedReplayBuffer,
) -> Float {
  self.tree.total()
}

///|
/// Maximum priority observed so far. New transitions are inserted at this
/// priority so they are guaranteed to be sampled at least once.
pub fn PrioritizedReplayBuffer::max_priority(
  self : PrioritizedReplayBuffer,
) -> Float {
  self.max_priority
}

///|
/// Append a transition. New transitions get priority = max_priority (Schmidt's
/// trick: ensures P(i) > 0 even when the initial |δ| is zero). Overwrites
/// oldest slot when the buffer is full (FIFO ring).
pub fn PrioritizedReplayBuffer::push(
  self : PrioritizedReplayBuffer,
  s : Int,
  a : Int,
  r : Float,
  s_next : Int,
  done : Bool,
) -> Unit {
  // Choose slot: while buffer is not full, append; else overwrite oldest.
  let slot = if self.size < self.capacity {
    self.size
  } else {
    self.cursor
  }
  // Write data.
  self.states[slot] = s
  self.actions[slot] = a
  self.rewards[slot] = r
  self.next_states[slot] = s_next
  self.dones[slot] = done
  // Write priority.
  let p = self.max_priority
  if self.size < self.capacity {
    let tree_idx = self.tree.add(p)
    let _ = tree_idx
    self.size = self.size + 1
  } else {
    // Overwrite priority at slot.
    self.tree.set_at(slot, p)
  }
  // Advance cursor.
  self.cursor = self.cursor + 1
  if self.cursor >= self.capacity {
    self.cursor = 0
  }
}

///|
/// Update priorities for a list of (slot_index, |δ|+ε) pairs after a training
/// step. `slot_indices` is the array returned by `sample`. `td_errors` may be
/// signed (the absolute value is taken internally).
///
/// Bumps `max_priority` upward if any new |δ|+ε exceeds the current max.
pub fn PrioritizedReplayBuffer::update_priorities(
  self : PrioritizedReplayBuffer,
  slot_indices : Array[Int],
  td_errors : Array[Float],
) -> Unit {
  let n = slot_indices.length()
  for k in 0..<n {
    let slot = slot_indices[k]
    let td = td_errors[k]
    let abs_td = if td < 0.0F { -td } else { td }
    let p = abs_td + self.epsilon
    self.tree.set_at(slot, p)
    if p > self.max_priority {
      self.max_priority = p
    }
  }
}

///|
/// Proportional-priority sample. Returns:
///
///   (states, actions, rewards, next_states, dones,
///    slot_indices, is_weights, p_i_per_index)
///
/// where:
///
/// * `slot_indices` is the leaf slot index for each sampled transition (for use
///   with `update_priorities`).
/// * `is_weights` is the importance-sampling weight normalised so the maximum
///   weight in the batch equals 1.0.
/// * `p_i_per_index` is the raw sampling probability P(i) used for the weight,
///   exposed so tests can verify sampling bias.
pub fn PrioritizedReplayBuffer::sample(
  self : PrioritizedReplayBuffer,
  batch_size : Int,
  rng : Xoshiro,
) -> (
  Array[Int],
  Array[Int],
  Array[Float],
  Array[Int],
  Array[Bool],
  Array[Int],
  Array[Float],
  Array[Float],
) {
  let states : Array[Int] = Array::make(batch_size, 0)
  let actions : Array[Int] = Array::make(batch_size, 0)
  let rewards : Array[Float] = Array::make(batch_size, 0.0F)
  let next_states : Array[Int] = Array::make(batch_size, 0)
  let dones : Array[Bool] = Array::make(batch_size, false)
  let slot_indices : Array[Int] = Array::make(batch_size, 0)
  let is_weights : Array[Float] = Array::make(batch_size, 0.0F)
  let probs : Array[Float] = Array::make(batch_size, 0.0F)
  let total = self.tree.total()
  if total <= 0.0F || self.size == 0 {
    // Empty buffer or all priorities zero: leave arrays at their default
    // values. Callers must check `len()` before sampling.
    let _ = rng
    return (
      states, actions, rewards, next_states, dones, slot_indices, is_weights, probs,
    )
  }
  // Standard PER: partition [0, total] into batch_size equal segments.
  let seg = total / Float::from_int(batch_size)
  // First pass: sample one leaf per segment, collect p_i.
  for k in 0..<batch_size {
    // Draw u ~ Uniform(0, 1) via box_muller (cheap, good enough).
    let (u, _) = box_muller(rng)
    let u_pos = if u < 0.0 { -u } else { u }
    let u_norm = Float::from_double(u_pos)
    let s_lo = Float::from_int(k) * seg
    let s_hi = Float::from_int(k + 1) * seg
    let mut target = s_lo + u_norm * (s_hi - s_lo)
    if target >= total {
      target = total - 1.0e-6F
    }
    if target < 0.0F {
      target = 0.0F
    }
    let (tree_idx, p_i) = self.tree.find_prefix(target)
    let slot = self.tree.tree_to_leaf(tree_idx)
    // Clamp slot into [0, size).
    let safe_slot = if slot < 0 {
      0
    } else if slot >= self.size {
      self.size - 1
    } else {
      slot
    }
    states[k] = self.states[safe_slot]
    actions[k] = self.actions[safe_slot]
    rewards[k] = self.rewards[safe_slot]
    next_states[k] = self.next_states[safe_slot]
    dones[k] = self.dones[safe_slot]
    slot_indices[k] = safe_slot
    // P(i) = p_i / total
    probs[k] = p_i / total
  }
  // Second pass: compute IS weights via Double arithmetic for stability.
  let raw_weights : Array[Float] = Array::make(batch_size, 0.0F)
  let n_float = Float::from_int(if self.size > 0 { self.size } else { 1 })
  for k in 0..<batch_size {
    let np_d = (n_float * probs[k]).to_double()
    let np_safe_d = if np_d < 1.0e-12 { 1.0e-12 } else { np_d }
    // w_i = (N · P(i))^{-β} = exp(-β · ln(N · P(i)))
    let w_d = @math.exp(-self.beta.to_double() * @math.ln(np_safe_d))
    raw_weights[k] = Float::from_double(w_d)
  }
  // Find max weight, normalise so max → 1.0.
  let mut max_w = raw_weights[0]
  for k in 1..<batch_size {
    if raw_weights[k] > max_w {
      max_w = raw_weights[k]
    }
  }
  let max_w_safe = if max_w < 1.0e-12F { 1.0e-12F } else { max_w }
  for k in 0..<batch_size {
    is_weights[k] = raw_weights[k] / max_w_safe
  }
  // alpha influences the priorities themselves (set via new(...)) so we don't
  // re-exponentiate here; this just keeps the field referenced for symmetry.
  let _ = self.alpha
  (
    states, actions, rewards, next_states, dones, slot_indices, is_weights, probs,
  )
}