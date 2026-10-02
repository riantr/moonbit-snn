// trajectory_buffer.mbt — TrajectoryBuffer: offline RL replay storing
// full (state, action, reward, done) trajectories with returns-to-go
// computation and window batching (v0.78.0).
//
// Reference: Chen et al. 2021 "Decision Transformer" offline-RL
// pipeline. The DT trainer (v0.79.0) consumes this buffer's
// `sample_trajectory_window` output and feeds it into `dt_forward`.
//
// Layout:
//   Each trajectory is a row-major flat Float buffer plus an Int length.
//   The buffer holds up to `capacity` trajectories, each of arbitrary
//   length (different trajectories can have different lengths).
//
//   - state_seq[t * state_dim + k]    = state value
//   - action_seq[t * action_dim + k]  = action value
//   - rewards[t]                      = scalar reward
//   - dones[t]                        = 0.0F or 1.0F
//   - rtg_seq[t]                      = returns-to-go at t = sum_{t'=t}^{T-1} γ^{t'-t} r_{t'}
//   - timesteps[t]                    = absolute step index in trajectory (for
//                                       DT timestep embedding)
//
// API:
//   TrajectoryBuffer::new(capacity, state_dim, action_dim, gamma)
//   load_trajectory(state_seq, action_seq, reward_seq, done_seq, traj_len)
//   num_trajectories() -> Int
//   sample_trajectory_window(batch_size, seq_len, rng)
//     -> (rtg_batch, state_batch, action_batch, timesteps_batch, seq_len)
//        shapes:
//          rtg_batch       [batch_size × seq_len]
//          state_batch     [batch_size × seq_len × state_dim]
//          action_batch    [batch_size × seq_len × action_dim]
//          timesteps_batch [batch_size × seq_len]   (Int)
//   compute_returns_to_go(reward_seq, done_seq, traj_len) -> rtg_seq
//   reset() -> Unit

///|
/// One trajectory: a flat row-major Float buffer plus its length.
pub struct Trajectory {
  state_dim : Int
  action_dim : Int
  state_seq : Array[Float]
  action_seq : Array[Float]
  reward_seq : Array[Float]
  done_seq : Array[Float]
  rtg_seq : Array[Float]
  length : Int
}

///|
/// Offline trajectory buffer. Holds up to `capacity` trajectories,
/// each with its own length. Returns-to-go are computed once per
/// trajectory at load time.
pub(all) struct TrajectoryBuffer {
  capacity : Int
  state_dim : Int
  action_dim : Int
  gamma : Float
  mut trajectories : Array[Trajectory]
  mut num_loaded : Int
}

///|
/// Construct a fresh TrajectoryBuffer. `gamma` is the discount used in
/// returns-to-go (typically 1.0 for DT, since DT does not bootstrap
/// over its own value function — the returns themselves encode the
/// horizon).
pub fn TrajectoryBuffer::new(
  capacity : Int,
  state_dim : Int,
  action_dim : Int,
  gamma : Float,
) -> TrajectoryBuffer {
  { capacity, state_dim, action_dim, gamma, trajectories: [], num_loaded: 0 }
}

///|
/// Compute returns-to-go for a single trajectory:
///   R_t = r_t + γ · R_{t+1}     (if t < T - 1)
///   R_{T-1} = r_{T-1}            (terminal)
/// For trajectory boundaries with done=1, the next-step return is zero
/// (the episode ended — no further rewards). Returns an Array[Float]
/// of length `traj_len`.
pub fn compute_returns_to_go(
  reward_seq : Array[Float],
  done_seq : Array[Float],
  traj_len : Int,
  gamma : Float,
) -> Array[Float] {
  let rtg : Array[Float] = Array::make(traj_len, 0.0F)
  if traj_len == 0 {
    return rtg
  }
  // Walk from end to start.
  let mut running = 0.0F
  for t_rev in 0..<traj_len {
    let t = traj_len - 1 - t_rev
    let r = reward_seq[t]
    let d = done_seq[t]
    if d > 0.5F {
      // Terminal: bootstrap from zero (episode ended).
      running = r
    } else {
      running = r + gamma * running
    }
    rtg[t] = running
  }
  rtg
}

///|
/// Load a single trajectory into the buffer. Computes returns-to-go
/// at load time (caller passes the raw rewards). If the buffer is full,
/// the trajectory is silently dropped (returns false).
pub fn load_trajectory(
  buf : TrajectoryBuffer,
  state_seq : Array[Float],
  action_seq : Array[Float],
  reward_seq : Array[Float],
  done_seq : Array[Float],
  traj_len : Int,
) -> Bool {
  if buf.num_loaded >= buf.capacity {
    return false
  }
  let rtg = compute_returns_to_go(reward_seq, done_seq, traj_len, buf.gamma)
  let traj : Trajectory = {
    state_dim: buf.state_dim,
    action_dim: buf.action_dim,
    state_seq,
    action_seq,
    reward_seq,
    done_seq,
    rtg_seq: rtg,
    length: traj_len,
  }
  buf.trajectories.push(traj)
  buf.num_loaded = buf.num_loaded + 1
  true
}

///|
/// Number of trajectories currently loaded.
pub fn trajectory_buffer_num_loaded(buf : TrajectoryBuffer) -> Int {
  buf.num_loaded
}

///|
/// Sample a batch of trajectory windows. Each sample is a contiguous
/// `seq_len` slice starting at a random position within a random trajectory.
/// If the chosen trajectory is shorter than seq_len, the window is
/// zero-padded on the right (the timesteps reflect the actual position).
/// Returns:
///   - rtg_batch:       [batch_size × seq_len]
///   - state_batch:     [batch_size × seq_len × state_dim]
///   - action_batch:    [batch_size × seq_len × action_dim]
///   - timesteps_batch: [batch_size × seq_len]   (Int)
/// All four are newly-allocated Float / Int arrays.
pub fn sample_trajectory_window(
  buf : TrajectoryBuffer,
  batch_size : Int,
  seq_len : Int,
  rng : Xoshiro,
) -> (Array[Float], Array[Float], Array[Float], Array[Int]) {
  let rtg_batch : Array[Float] = Array::make(batch_size * seq_len, 0.0F)
  let state_batch : Array[Float] = Array::make(
    batch_size * seq_len * buf.state_dim, 0.0F,
  )
  let action_batch : Array[Float] = Array::make(
    batch_size * seq_len * buf.action_dim, 0.0F,
  )
  let timesteps_batch : Array[Int] = Array::make(batch_size * seq_len, 0)
  for b in 0..<batch_size {
    if buf.num_loaded == 0 {
      // No trajectories — return zero-padded window.
      continue
    }
    // Pick a trajectory uniformly at random.
    let traj_idx = (next_u64(rng) % buf.num_loaded.to_uint64()).to_int()
    let traj = buf.trajectories[traj_idx]
    if traj.length == 0 {
      continue
    }
    // Pick a starting position in [0, max(0, traj.length - 1)].
    let max_start = if traj.length - 1 > 0 {
      traj.length - 1
    } else {
      0
    }
    let start = if max_start == 0 {
      0
    } else {
      (next_u64(rng) % (max_start + 1).to_uint64()).to_int()
    }
    // Copy seq_len steps; timesteps are absolute indices (used by DT
    // timestep embedding).
    for t in 0..<seq_len {
      let src_t = start + t
      let b_off = b * seq_len + t
      if src_t < traj.length {
        rtg_batch[b_off] = traj.rtg_seq[src_t]
        timesteps_batch[b_off] = src_t
        let s_off = src_t * traj.state_dim
        let a_off = src_t * traj.action_dim
        let s_dst = b * seq_len * traj.state_dim + t * traj.state_dim
        let a_dst = b * seq_len * traj.action_dim + t * traj.action_dim
        for k in 0..<traj.state_dim {
          state_batch[s_dst + k] = traj.state_seq[s_off + k]
        }
        for k in 0..<traj.action_dim {
          action_batch[a_dst + k] = traj.action_seq[a_off + k]
        }
      } else {
        // Zero-pad. rtg_batch entry stays 0.0F (already zero from
        // Array::make). timesteps entry stays 0 (already zero).
        rtg_batch[b_off] = 0.0F
        timesteps_batch[b_off] = 0
      }
    }
  }
  (rtg_batch, state_batch, action_batch, timesteps_batch)
}

///|
/// Clear all loaded trajectories.
pub fn trajectory_buffer_reset(buf : TrajectoryBuffer) -> Unit {
  buf.trajectories = []
  buf.num_loaded = 0
}