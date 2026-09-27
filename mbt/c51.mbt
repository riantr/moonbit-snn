// c51.mbt — Categorical DQN (Bellemare et al. 2017) — v0.39.0.
//
// Distributional RL: instead of predicting a scalar Q-value, predict
// a probability distribution over a discrete support of "atoms".
//
//   Support: z_i = V_min + i · Δz,    i ∈ {0, 1, ..., N-1}, N = 51
//   Δz = (V_max - V_min) / (N - 1)
//
// Q-recovery:
//   Q(s, a) = Σ_i p(s, a, i) · z_i
//
// Per-transition projected target distribution (the key step):
//   For atom j of next-state distribution, compute
//     Tz_j = r + γ · (1 − done) · z_j
//   Clamp to [V_min, V_max], then map to support:
//     b_j = (Tz_j − V_min) / Δz
//   Project probability mass from j to atoms ⌊b_j⌋ and ⌈b_j⌉:
//     m[⌊b_j⌋] += p(s', a*, j) · (⌈b_j⌉ − b_j)
//     m[⌈b_j⌉] += p(s', a*, j) · (b_j − ⌊b_j⌋)
//   where a* = argmax_a Σ_i p(s', a, i) · z_i.
//
// Loss: cross-entropy between projected target m and predicted p(s, a).
//   L = −Σ_i m[i] · log p(s, a, i)
// Updates: SGD on the (n_states, n_atoms) logits for the chosen (s, a).

///|
/// Xavier-normal init for a 3D weight tensor (rows × cols × depth).
/// All entries are independent N(0, std²).
fn xavier_normal_3d(
  rows : Int,
  cols : Int,
  depth : Int,
  std : Float,
  rng : Xoshiro,
) -> Array[Array[Array[Float]]] {
  let w : Array[Array[Array[Float]]] = Array::make(rows, [])
  for i in 0..<rows {
    let row : Array[Array[Float]] = Array::make(cols, [])
    for j in 0..<cols {
      let plane : Array[Float] = Array::make(depth, 0.0F)
      for k in 0..<depth {
        let (z1, z2) = box_muller(rng)
        let z = if z1 != 0.0 { z1 } else { z2 }
        plane[k] = Float::from_double(z) * std
      }
      row[j] = plane
    }
    w[i] = row
  }
  w
}

///|
/// Linear net producing `n_atoms` logits per action. Forward on a
/// discrete state (one-hot) yields `n_actions × n_atoms` logits.
pub struct C51Net {
  /// logits: [a][s][i]  (n_actions × n_states × n_atoms)
  w : Array[Array[Array[Float]]]
  /// bias: [a][i]
  b : Array[Array[Float]]
  n_states : Int
  n_actions : Int
  n_atoms : Int
  v_min : Float
  v_max : Float
}

///|
pub fn C51Net::new(
  n_states : Int,
  n_actions : Int,
  n_atoms : Int,
  v_min : Float,
  v_max : Float,
  seed : UInt64,
) -> C51Net {
  let std = sqrtf(1.0F / Float::from_int(n_states * n_atoms))
  let rng = Xoshiro::from_state(seed, seed + 1UL, seed + 2UL, seed + 3UL)
  let w = xavier_normal_3d(n_actions, n_states, n_atoms, std, rng)
  let b : Array[Array[Float]] = Array::make(n_actions, [])
  for a in 0..<n_actions {
    b[a] = Array::make(n_atoms, 0.0F)
  }
  { w, b, n_states, n_actions, n_atoms, v_min, v_max }
}

///|
/// Forward pass: returns logits[a][i] for the one-hot state `s`.
pub fn c51_net_logits(net : C51Net, state : Int) -> Array[Array[Float]] {
  let n_a = net.n_actions
  let n_i = net.n_atoms
  let logits : Array[Array[Float]] = Array::make(n_a, [])
  for a in 0..<n_a {
    let row : Array[Float] = Array::make(n_i, net.b[a][0])
    if state >= 0 && state < net.n_states {
      for i in 0..<n_i {
        row[i] = net.b[a][i] + net.w[a][state][i]
      }
    }
    logits[a] = row
  }
  logits
}

///|
/// Softmax over the atom dimension (last axis).
pub fn c51_softmax_atoms(logits : Array[Float]) -> Array[Float] {
  let n = logits.length()
  let mut max_l = logits[0]
  for i in 1..<n {
    if logits[i] > max_l {
      max_l = logits[i]
    }
  }
  let mut sum_e = 0.0F
  let exps : Array[Float] = Array::make(n, 0.0F)
  for i in 0..<n {
    let e = expf(logits[i] - max_l)
    exps[i] = e
    sum_e = sum_e + e
  }
  let inv = 1.0F / sum_e
  for i in 0..<n {
    exps[i] = exps[i] * inv
  }
  exps
}

///|
/// Forward: returns probs[a][i] (n_actions × n_atoms).
pub fn c51_forward(net : C51Net, state : Int) -> Array[Array[Float]] {
  let logits = c51_net_logits(net, state)
  let probs : Array[Array[Float]] = Array::make(net.n_actions, [])
  for a in 0..<net.n_actions {
    probs[a] = c51_softmax_atoms(logits[a])
  }
  probs
}

///|
/// Recover Q(s, a) from the distribution.
pub fn c51_q_value(probs : Array[Float], support : Array[Float]) -> Float {
  let n = probs.length()
  let mut s = 0.0F
  for i in 0..<n {
    s = s + probs[i] * support[i]
  }
  s
}

///|
/// Build the support array (V_min + i · Δz).
pub fn c51_support(v_min : Float, v_max : Float, n_atoms : Int) -> Array[Float] {
  let support : Array[Float] = Array::make(n_atoms, 0.0F)
  if n_atoms > 1 {
    let delta_z = (v_max - v_min) / Float::from_int(n_atoms - 1)
    for i in 0..<n_atoms {
      support[i] = v_min + Float::from_int(i) * delta_z
    }
  } else {
    support[0] = v_min
  }
  support
}

///|
/// C51 agent: online net + target net + hyper-parameters.
pub struct C51 {
  net : C51Net
  net_target : C51Net
  gamma : Float
  lr : Float
  n_atoms : Int
  v_min : Float
  v_max : Float
  /// Cached support array, length n_atoms.
  support : Array[Float]
  /// Cached Δz = (v_max − v_min) / (n_atoms − 1).
  delta_z : Float
}

///|
pub fn C51::new(
  n_states : Int,
  n_actions : Int,
  n_atoms : Int,
  v_min : Float,
  v_max : Float,
  gamma : Float,
  lr : Float,
  seed : UInt64,
) -> C51 {
  let net = C51Net::new(n_states, n_actions, n_atoms, v_min, v_max, seed)
  let net_target = C51Net::new(
    n_states, n_actions, n_atoms, v_min, v_max, seed + 100UL,
  )
  c51_copy(net_target, net)
  let support = c51_support(v_min, v_max, n_atoms)
  let delta_z = if n_atoms > 1 {
    (v_max - v_min) / Float::from_int(n_atoms - 1)
  } else {
    1.0F
  }
  { net, net_target, gamma, lr, n_atoms, v_min, v_max, support, delta_z }
}

///|
/// Compute the projected target distribution for one transition.
/// Returns m[i] (n_atoms) — a probability distribution over atoms.
///
/// The "greedy" next action is a* = argmax_a Σ_i p(s', a, i) · z_i
/// (Double DQN-style online-net evaluation is not implemented here —
/// we use the target net for both selection and evaluation, which is
/// the original Bellemare 2017 formulation).
pub fn c51_projected_target(
  c51 : C51,
  reward : Float,
  next_state : Int,
  done : Bool,
) -> Array[Float] {
  let n = c51.n_atoms
  let m : Array[Float] = Array::make(n, 0.0F)
  if done {
    // Target = reward projected onto a single atom (delta distribution).
    let tz = if reward < c51.v_min {
      c51.v_min
    } else if reward > c51.v_max {
      c51.v_max
    } else {
      reward
    }
    let b = (tz - c51.v_min) / c51.delta_z
    let lo = Float::to_int(b)
    let lo_clamped = if lo < 0 {
      0
    } else if lo >= n - 1 {
      n - 2
    } else {
      lo
    }
    let hi = lo_clamped + 1
    let frac = b - Float::from_int(lo_clamped)
    m[lo_clamped] = 1.0F - frac
    m[hi] = frac
    return m
  }
  // 1) Greedy next-action a* via target net.
  let probs_next = c51_forward(c51.net_target, next_state)
  let mut best_a = 0
  let mut best_q = c51_q_value(probs_next[0], c51.support)
  for a in 1..<c51.net_target.n_actions {
    let q = c51_q_value(probs_next[a], c51.support)
    if q > best_q {
      best_q = q
      best_a = a
    }
  }
  // 2) For each atom j, project Tz_j = r + γ · z_j into support.
  let p_next = probs_next[best_a]
  for j in 0..<n {
    let tz = reward + c51.gamma * c51.support[j]
    let tz_clamped = if tz < c51.v_min {
      c51.v_min
    } else if tz > c51.v_max {
      c51.v_max
    } else {
      tz
    }
    let b = (tz_clamped - c51.v_min) / c51.delta_z
    let lo = Float::to_int(b)
    let lo_clamped = if lo < 0 {
      0
    } else if lo >= n - 1 {
      n - 2
    } else {
      lo
    }
    let hi = lo_clamped + 1
    let frac = b - Float::from_int(lo_clamped)
    m[lo_clamped] = m[lo_clamped] + p_next[j] * (1.0F - frac)
    m[hi] = m[hi] + p_next[j] * frac
  }
  m
}

///|
/// Greedy action for state `s` based on the target net.
/// a* = argmax_a Σ_i p(s, a, i) · z_i.
pub fn c51_greedy_action(c51 : C51, state : Int) -> Int {
  let probs = c51_forward(c51.net_target, state)
  let mut best_a = 0
  let mut best_q = c51_q_value(probs[0], c51.support)
  for a in 1..<c51.net.n_actions {
    let q = c51_q_value(probs[a], c51.support)
    if q > best_q {
      best_q = q
      best_a = a
    }
  }
  best_a
}

///|
/// One C51 update step: cross-entropy loss + SGD on (s, a) logits.
///
/// For each transition in the batch:
///   m = projected_target(r, s', done)        // n_atoms
///   p = softmax(logits(s)[a])                 // n_atoms
///   L = -Σ_i m[i] · log p[i]                // cross-entropy
///   d_logits[a][i] = (p[i] - m[i])            // gradient of CE wrt logits
///
/// Returns mean cross-entropy loss.
pub fn c51_update(
  c51 : C51,
  states : Array[Int],
  actions : Array[Int],
  rewards : Array[Float],
  next_states : Array[Int],
  dones : Array[Bool],
) -> Float {
  let n = states.length()
  let mut total_loss = 0.0F
  for k in 0..<n {
    let s = states[k]
    let a = actions[k]
    let r = rewards[k]
    let s_next = next_states[k]
    let d = dones[k]
    let m = c51_projected_target(c51, r, s_next, d)
    let probs = c51_forward(c51.net, s)
    let p = probs[a]
    // Cross-entropy: -Σ m[i] · log p[i]
    let mut loss = 0.0F
    for i in 0..<c51.n_atoms {
      let lp = if p[i] > 0.0F { logf(p[i]) } else { -20.0F }
      loss = loss - m[i] * lp
    }
    total_loss = total_loss + loss
    // Gradient: d_logits[i] = p[i] - m[i]  (CE derivative w.r.t. logit).
    let g = 2.0F * c51.lr
    for i in 0..<c51.n_atoms {
      let d_logit = (p[i] - m[i]) * g
      // Update only the entry for the chosen (s, a).
      if s >= 0 && s < c51.net.n_states {
        c51.net.w[a][s][i] = c51.net.w[a][s][i] - d_logit
        c51.net.b[a][i] = c51.net.b[a][i] - d_logit
      }
    }
  }
  total_loss / Float::from_int(n)
}

///|
/// Polyak soft update of target net from online net.
pub fn c51_soft_update(c51 : C51, tau : Float) -> Unit {
  let n_a = c51.net.n_actions
  let n_s = c51.net.n_states
  let n_i = c51.net.n_atoms
  for a in 0..<n_a {
    for s in 0..<n_s {
      for i in 0..<n_i {
        c51.net_target.w[a][s][i] = tau * c51.net.w[a][s][i] +
          (1.0F - tau) * c51.net_target.w[a][s][i]
      }
    }
    for i in 0..<n_i {
      c51.net_target.b[a][i] = tau * c51.net.b[a][i] +
        (1.0F - tau) * c51.net_target.b[a][i]
    }
  }
}

///|
/// Initial sync (hard copy online → target).
fn c51_copy(target : C51Net, source : C51Net) -> Unit {
  let n_a = source.n_actions
  let n_s = source.n_states
  let n_i = source.n_atoms
  for a in 0..<n_a {
    for s in 0..<n_s {
      for i in 0..<n_i {
        target.w[a][s][i] = source.w[a][s][i]
      }
    }
    for i in 0..<n_i {
      target.b[a][i] = source.b[a][i]
    }
  }
  let _ = n_a
  let _ = n_s
  let _ = n_i
}