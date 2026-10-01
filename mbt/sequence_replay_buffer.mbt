// sequence_replay_buffer.mbt — Per-step continuous replay buffer with
// T-step rollout sampling for recurrent (LSTM/GRU) actor-critic RL.
//
// Stores per-step transitions of (s, a, r, s', done, t) in FIFO-wrapped
// flat Float arrays. On `sample_seq_batch(batch_size, seq_len, rng)`,
// picks `batch_size` starting indices, then for each start collects the
// next `seq_len` consecutive steps; if any step in the window has done=1,
// the rest of the window is zero-padded and a terminal flag is set so the
// caller can mask hidden-state bootstrapping.
//
// Layout conventions (flat row-major, project-wide):
//   states       : [capacity × state_dim]   row i at offset i*state_dim
//   actions      : [capacity × action_dim]
//   rewards      : [capacity]
//   next_states  : [capacity × state_dim]
//   dones        : [capacity]               0.0F / 1.0F
//
// Sample output shape:
//   obs_seq        : [batch_size × seq_len × state_dim]   flat row-major
//   action_seq     : [batch_size × seq_len × action_dim]
//   reward_seq     : [batch_size × seq_len]
//   next_obs_seq   : [batch_size × seq_len × state_dim]
//   done_seq       : [batch_size × seq_len]
//   terminal_mask  : [batch_size × seq_len]               1.0F if step
//                                                       follows a `done`,
//                                                       else 0.0F
//   hidden_init    : [batch_size × hidden_dim]            zeros
//
// API:
//   SequenceReplayBuffer::new(capacity, state_dim, action_dim)
//   push(state, action, reward, next_state, done)
//   len() -> Int
//   sample_seq_batch(batch_size, seq_len, rng)
//     -> (obs_seq, action_seq, reward_seq, next_obs_seq, done_seq,
//         terminal_mask, hidden_init)
//   reset() -> Unit                       (clear stored transitions)
//
// Reference: Hausknecht & Stone 2015 "Deep Recurrent Q-Learning for
// Partially Observable MDPs" (the LSTM-DQN that motivates this design).

///|
/// Per-step continuous replay buffer with sequence-rollout sampling.
pub struct SequenceReplayBuffer {
  capacity : Int
  state_dim : Int
  action_dim : Int
  states : Array[Float]
  actions : Array[Float]
  rewards : Array[Float]
  next_states : Array[Float]
  dones : Array[Float]
  mut size : Int
  mut cursor : Int
}

///|
/// Construct a new sequence replay buffer with the given capacity and
/// observation / action dimensions. All internal arrays are
/// zero-initialised; `size` and `cursor` start at 0.
pub fn SequenceReplayBuffer::new(
  capacity : Int,
  state_dim : Int,
  action_dim : Int,
) -> SequenceReplayBuffer {
  let n = capacity
  let s_len = n * state_dim
  let a_len = n * action_dim
  {
    capacity: n,
    state_dim,
    action_dim,
    states: Array::make(s_len, 0.0F),
    actions: Array::make(a_len, 0.0F),
    rewards: Array::make(n, 0.0F),
    next_states: Array::make(s_len, 0.0F),
    dones: Array::make(n, 0.0F),
    size: 0,
    cursor: 0,
  }
}

///|
/// Number of transitions currently stored (clamped to capacity).
pub fn SequenceReplayBuffer::len(self : SequenceReplayBuffer) -> Int {
  self.size
}

///|
/// Clear the buffer without releasing storage. Both `size` and
/// `cursor` reset to 0; the underlying Float arrays keep their
/// capacity so the buffer can be re-used without re-allocation.
pub fn SequenceReplayBuffer::reset(self : SequenceReplayBuffer) -> Unit {
  self.size = 0
  self.cursor = 0
}

///|
/// Append a transition. Overwrites the oldest slot when the buffer
/// is full. The `done` flag marks a terminal transition; subsequent
/// sample windows crossing a terminal will zero-pad the rest and set
/// `terminal_mask = 1.0F` so the caller can break BPTT.
pub fn SequenceReplayBuffer::push(
  self : SequenceReplayBuffer,
  state : Array[Float],
  action : Array[Float],
  reward : Float,
  next_state : Array[Float],
  done : Bool,
) -> Unit {
  let i = self.cursor
  let s_off = i * self.state_dim
  let a_off = i * self.action_dim
  for k in 0..<self.state_dim {
    self.states[s_off + k] = state[k]
    self.next_states[s_off + k] = next_state[k]
  }
  for k in 0..<self.action_dim {
    self.actions[a_off + k] = action[k]
  }
  self.rewards[i] = reward
  self.dones[i] = if done { 1.0F } else { 0.0F }
  self.cursor = self.cursor + 1
  if self.cursor >= self.capacity {
    self.cursor = 0
  }
  if self.size < self.capacity {
    self.size = self.size + 1
  }
}

///|
/// Sample a mini-batch of `batch_size` sequences, each of length
/// `seq_len`. Returns 7 parallel arrays. The starting index for each
/// sequence is drawn uniformly from `[0, size - seq_len)` (clamped).
///
/// Padding behaviour: if a sequence window crosses a `done` boundary,
/// the corresponding `obs_seq` / `action_seq` slots are zero-filled
/// and `terminal_mask[t]` is set to 1.0F. The caller is responsible
/// for resetting hidden state at terminal positions.
pub fn SequenceReplayBuffer::sample_seq_batch(
  self : SequenceReplayBuffer,
  batch_size : Int,
  seq_len : Int,
  rng : Xoshiro,
) -> (Array[Float], Array[Float], Array[Float], Array[Float], Array[Float], Array[Float], Array[Float]) {
  let max_start = self.size - seq_len
  let safe_max = if max_start < 1 {
    0
  } else {
    max_start
  }
  let obs_seq : Array[Float] = Array::make(batch_size * seq_len * self.state_dim, 0.0F)
  let action_seq : Array[Float] = Array::make(batch_size * seq_len * self.action_dim, 0.0F)
  let reward_seq : Array[Float] = Array::make(batch_size * seq_len, 0.0F)
  let next_obs_seq : Array[Float] = Array::make(batch_size * seq_len * self.state_dim, 0.0F)
  let done_seq : Array[Float] = Array::make(batch_size * seq_len, 0.0F)
  let terminal_mask : Array[Float] = Array::make(batch_size * seq_len, 0.0F)
  let hidden_init : Array[Float] = Array::make(batch_size, 0.0F)
  for k in 0..<batch_size {
    // Box-Muller-derived uniform sample to avoid modulo bias.
    // Special case: if safe_max = 0 (empty buffer or seq_len > size),
    // force start_idx = 0 so that every step in the window falls
    // past the end of the buffer and gets zero-padded.
    let mut start_idx = 0
    if safe_max > 0 {
      let (z, _) = box_muller(rng)
      let u_pos = if z < 0.0 {
        -z
      } else {
        z
      }
      start_idx = Float::from_double(u_pos * Double::from_int(safe_max)).to_int()
      if start_idx < 0 {
        start_idx = 0
      } else if start_idx > safe_max {
        start_idx = safe_max
      }
    }
    // Walk forward seq_len steps; break if any earlier step was
    // terminal (subsequent slots get zero-padded + mask set).
    let mut crossed_done = false
    for t in 0..<seq_len {
      let global_idx = start_idx + t
      let obs_off = (k * seq_len + t) * self.state_dim
      let act_off = (k * seq_len + t) * self.action_dim
      if crossed_done || global_idx >= self.size {
        // Already past the end of the buffer or terminal:
        // leave the slot at 0 (zero-init) and mark the mask.
        terminal_mask[k * seq_len + t] = 1.0F
        done_seq[k * seq_len + t] = 0.0F
      } else {
        let buf_off = global_idx * self.state_dim
        for d in 0..<self.state_dim {
          obs_seq[obs_off + d] = self.states[buf_off + d]
          next_obs_seq[obs_off + d] = self.next_states[buf_off + d]
        }
        let a_buf_off = global_idx * self.action_dim
        for d in 0..<self.action_dim {
          action_seq[act_off + d] = self.actions[a_buf_off + d]
        }
        reward_seq[k * seq_len + t] = self.rewards[global_idx]
        done_seq[k * seq_len + t] = self.dones[global_idx]
        if self.dones[global_idx] > 0.5F {
          // This slot is the terminal one. Mark mask=1 for the
          // NEXT slot (caller resets hidden state there) but keep
          // the current obs/next_obs visible.
          crossed_done = true
          if t + 1 < seq_len {
            terminal_mask[k * seq_len + t + 1] = 1.0F
          }
        }
      }
    }
  }
  (obs_seq, action_seq, reward_seq, next_obs_seq, done_seq, terminal_mask, hidden_init)
}

// ----------------------------------------------------------------------------
// Deterministic sampling helper.
//
// `sample_seq_batch_at` is identical to `sample_seq_batch` except the start
// index for each batch element is taken from the caller-supplied
// `start_indices` array (length `batch_size`) instead of being drawn from
// an RNG. This is useful for:
//   1. Reproducible / regression tests where a specific window must be
//      observed (the RNG-based path uses box-muller + |z| which produces
//      a half-normal distribution concentrated near 0, so hitting a
//      specific `start_idx` requires many seeded draws).
//   2. Callers that want bit-exact reproducibility across runs (e.g.
//      curriculum learning, evaluation rollouts).
//
// Preconditions:
//   - `start_indices.length() == batch_size`
//   - For each k: `0 <= start_indices[k] <= max_start` where
//     `max_start = size - seq_len` (clamped to 0 when buffer is empty).
//     Out-of-range values are clamped, matching `sample_seq_batch`'s
//     post-RNG clamp behaviour.
///|
/// Same as `sample_seq_batch` but takes explicit start indices (one per
/// batch element) instead of drawing from an RNG. See block comment
/// above for rationale. Returns the same 7-tuple as `sample_seq_batch`.
pub fn SequenceReplayBuffer::sample_seq_batch_at(
  self : SequenceReplayBuffer,
  batch_size : Int,
  seq_len : Int,
  start_indices : Array[Int],
) -> (Array[Float], Array[Float], Array[Float], Array[Float], Array[Float], Array[Float], Array[Float]) {
  let max_start = self.size - seq_len
  let safe_max = if max_start < 1 {
    0
  } else {
    max_start
  }
  let obs_seq : Array[Float] = Array::make(batch_size * seq_len * self.state_dim, 0.0F)
  let action_seq : Array[Float] = Array::make(batch_size * seq_len * self.action_dim, 0.0F)
  let reward_seq : Array[Float] = Array::make(batch_size * seq_len, 0.0F)
  let next_obs_seq : Array[Float] = Array::make(batch_size * seq_len * self.state_dim, 0.0F)
  let done_seq : Array[Float] = Array::make(batch_size * seq_len, 0.0F)
  let terminal_mask : Array[Float] = Array::make(batch_size * seq_len, 0.0F)
  let hidden_init : Array[Float] = Array::make(batch_size, 0.0F)
  for k in 0..<batch_size {
    // Pull the requested start_idx and clamp to valid range (mirrors
    // the post-RNG clamp in `sample_seq_batch`).
    let mut start_idx = if k < start_indices.length() {
      start_indices[k]
    } else {
      0
    }
    if safe_max <= 0 {
      start_idx = 0
    } else {
      if start_idx < 0 {
        start_idx = 0
      } else if start_idx > safe_max {
        start_idx = safe_max
      }
    }
    // Walk forward seq_len steps; break if any earlier step was
    // terminal (subsequent slots get zero-padded + mask set).
    let mut crossed_done = false
    for t in 0..<seq_len {
      let global_idx = start_idx + t
      let obs_off = (k * seq_len + t) * self.state_dim
      let act_off = (k * seq_len + t) * self.action_dim
      if crossed_done || global_idx >= self.size {
        // Already past the end of the buffer or terminal:
        // leave the slot at 0 (zero-init) and mark the mask.
        terminal_mask[k * seq_len + t] = 1.0F
        done_seq[k * seq_len + t] = 0.0F
      } else {
        let buf_off = global_idx * self.state_dim
        for d in 0..<self.state_dim {
          obs_seq[obs_off + d] = self.states[buf_off + d]
          next_obs_seq[obs_off + d] = self.next_states[buf_off + d]
        }
        let a_buf_off = global_idx * self.action_dim
        for d in 0..<self.action_dim {
          action_seq[act_off + d] = self.actions[a_buf_off + d]
        }
        reward_seq[k * seq_len + t] = self.rewards[global_idx]
        done_seq[k * seq_len + t] = self.dones[global_idx]
        if self.dones[global_idx] > 0.5F {
          // This slot is the terminal one. Mark mask=1 for the
          // NEXT slot (caller resets hidden state there) but keep
          // the current obs/next_obs visible.
          crossed_done = true
          if t + 1 < seq_len {
            terminal_mask[k * seq_len + t + 1] = 1.0F
          }
        }
      }
    }
  }
  (obs_seq, action_seq, reward_seq, next_obs_seq, done_seq, terminal_mask, hidden_init)
}
