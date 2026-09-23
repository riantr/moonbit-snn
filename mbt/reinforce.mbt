// reinforce.mbt — REINFORCE policy gradient on a small GridWorld (v0.33.0).
//
// Vanilla policy gradient (Monte Carlo):
//   ∇_θ J(θ) = E_τ [ Σ_t ∇_θ log π_θ(a_t | s_t) · G_t ]
// where G_t = Σ_{k >= t} γ^(k-t) · r_k is the discounted return.
//
// Components:
//   - `GridWorld`        — small deterministic MDP (n×n, 4 actions)
//   - `LinearSoftmaxPolicy` — linear logits W · x, softmax, no hidden layer
//   - `Episode`          — list of (state, action, reward) tuples
//   - `policy_rollout`   — sample one episode under the current policy
//   - `compute_returns`  — Monte-Carlo discounted returns
//   - `policy_gradient_update` — REINFORCE gradient step
//   - `train_reinforce`  — K-episode training loop

///|
/// Action encoding: 0=up, 1=down, 2=left, 3=right.
pub let rl_action_up : Int = 0

///|
pub let rl_action_down : Int = 1

///|
pub let rl_action_left : Int = 2

///|
pub let rl_action_right : Int = 3

///|
/// Deterministic 2D GridWorld. `n_states = n_rows * n_cols`.
/// State is `(row, col)` flattened to `row * n_cols + col`.
pub struct GridWorld {
  n_rows : Int
  n_cols : Int
  start : Int
  goal : Int
  step_penalty : Float
  goal_reward : Float
  max_steps : Int
}

///|
/// Build a simple GridWorld with start at (0, 0), goal at
/// (n_rows-1, n_cols-1), step penalty -0.1, goal reward +1.0.
pub fn GridWorld::new(n_rows : Int, n_cols : Int, max_steps : Int) -> GridWorld {
  {
    n_rows,
    n_cols,
    start: 0,
    goal: (n_rows - 1) * n_cols + (n_cols - 1),
    step_penalty: -0.1F,
    goal_reward: 1.0F,
    max_steps,
  }
}

///|
pub fn GridWorld::n_states(self : GridWorld) -> Int {
  self.n_rows * self.n_cols
}

///|
/// One-hot encode a state index into a length `n` vector.
pub fn rl_one_hot(state : Int, n : Int) -> Array[Float] {
  let x : Array[Float] = Array::make(n, 0.0F)
  if state >= 0 && state < n {
    x[state] = 1.0F
  }
  x
}

///|
/// Apply action. Returns (next_state, reward, done).
/// Going off the grid keeps the agent in place and adds step_penalty.
pub fn GridWorld::step(self : GridWorld, state : Int, action : Int) -> (Int, Float, Bool) {
  let row = state / self.n_cols
  let col = state % self.n_cols
  let mut new_row = row
  let mut new_col = col
  if action == rl_action_up {
    if row > 0 {
      new_row = row - 1
    }
  } else if action == rl_action_down {
    if row < self.n_rows - 1 {
      new_row = row + 1
    }
  } else if action == rl_action_left {
    if col > 0 {
      new_col = col - 1
    }
  } else if action == rl_action_right {
    if col < self.n_cols - 1 {
      new_col = col + 1
    }
  }
  let next_state = new_row * self.n_cols + new_col
  if next_state == self.goal {
    (next_state, self.goal_reward, true)
  } else {
    (next_state, self.step_penalty, false)
  }
}

///|
/// Linear softmax policy: logits = W · x, π = softmax(logits).
pub struct LinearSoftmaxPolicy {
  n_states : Int
  n_actions : Int
  w : Array[Array[Float]]
}

///|
pub fn LinearSoftmaxPolicy::new(
  n_states : Int,
  n_actions : Int,
  seed : UInt64,
) -> LinearSoftmaxPolicy {
  let std = sqrtf(1.0F / Float::from_int(n_states))
  let rng = Xoshiro::from_state(seed, seed + 1UL, seed + 2UL, seed + 3UL)
  let w = xavier_normal(n_actions, n_states, std, rng)
  { n_states, n_actions, w }
}

///|
/// Compute logits and numerically stable softmax probabilities.
pub fn policy_forward(
  policy : LinearSoftmaxPolicy,
  x : Array[Float],
) -> (Array[Float], Array[Float]) {
  let n_actions = policy.n_actions
  let logits : Array[Float] = Array::make(n_actions, 0.0F)
  for i in 0..<n_actions {
    let row = policy.w[i]
    let mut s = 0.0F
    for j in 0..<row.length() {
      s = s + row[j] * x[j]
    }
    logits[i] = s
  }
  let mut max_l = logits[0]
  for i in 1..<n_actions {
    if logits[i] > max_l {
      max_l = logits[i]
    }
  }
  let probs : Array[Float] = Array::make(n_actions, 0.0F)
  let mut sum_e = 0.0F
  for i in 0..<n_actions {
    let e = expf(logits[i] - max_l)
    probs[i] = e
    sum_e = sum_e + e
  }
  let inv_sum = 1.0F / sum_e
  for i in 0..<n_actions {
    probs[i] = probs[i] * inv_sum
  }
  (logits, probs)
}

///|
/// Sample action from a categorical distribution. Returns
/// `(action, log_prob)`. `log_prob` is clamped to -20 to avoid -inf.
pub fn sample_categorical(probs : Array[Float], rng : Xoshiro) -> (Int, Float) {
  let n = probs.length()
  let (u_raw, _) = box_muller(rng)
  let u = Float::from_double(u_raw).abs()
  let mut thr = u
  if thr > 1.0F {
    thr = 1.0F - 1.0e-7F
  } else if thr < 0.0F {
    thr = 0.0F
  }
  let mut cum = 0.0F
  let mut a = n - 1
  for i in 0..<n {
    cum = cum + probs[i]
    if cum >= thr {
      a = i
      break
    }
  }
  let lp = if probs[a] > 0.0F {
    logf(probs[a])
  } else {
    -20.0F
  }
  (a, lp)
}

///|
/// Episode record: states, actions, rewards.
pub(all) struct Episode {
  states : Array[Int]
  actions : Array[Int]
  rewards : Array[Float]
}

///|
/// Roll out one episode under the current policy.
pub fn policy_rollout(
  env : GridWorld,
  policy : LinearSoftmaxPolicy,
  max_steps : Int,
  rng : Xoshiro,
) -> Episode {
  let states : Array[Int] = []
  let actions : Array[Int] = []
  let rewards : Array[Float] = []
  let mut state = env.start
  let mut done = false
  let mut t = 0
  while !done && t < max_steps {
    let x = rl_one_hot(state, env.n_states())
    let (_logits, probs) = policy_forward(policy, x)
    let (a, _lp) = sample_categorical(probs, rng)
    let (next_state, r, d) = env.step(state, a)
    states.push(state)
    actions.push(a)
    rewards.push(r)
    state = next_state
    done = d
    t = t + 1
  }
  { states, actions, rewards }
}

///|
/// Monte-Carlo discounted returns G_t = Σ_{k >= t} γ^(k-t) · r_k.
pub fn compute_returns(rewards : Array[Float], gamma : Float) -> Array[Float] {
  let t = rewards.length()
  let g : Array[Float] = Array::make(t, 0.0F)
  let mut running = 0.0F
  let mut i = t - 1
  while i >= 0 {
    running = rewards[i] + gamma * running
    g[i] = running
    i = i - 1
  }
  g
}

///|
/// Total undiscounted return of an episode.
pub fn episode_return(episode : Episode) -> Float {
  let mut s = 0.0F
  for r in episode.rewards {
    s = s + r
  }
  s
}

///|
/// REINFORCE policy gradient step. Modifies `policy.w` in place.
/// d_logits[i] = (1{i == a_t} - π(a|s_t)) · G_t
/// d_W[i, j] += d_logits[i] · x_t[j]
pub fn policy_gradient_update(
  policy : LinearSoftmaxPolicy,
  episode : Episode,
  returns : Array[Float],
  lr : Float,
) -> Unit {
  let t = episode.states.length()
  let n_a = policy.n_actions
  let n_s = policy.n_states
  for step in 0..<t {
    let g = returns[step]
    if g == 0.0F {
      continue
    }
    let x = rl_one_hot(episode.states[step], n_s)
    let (_logits, probs) = policy_forward(policy, x)
    let a = episode.actions[step]
    for i in 0..<n_a {
      let indicator = if i == a { 1.0F } else { 0.0F }
      let d_logit = (indicator - probs[i]) * g
      let row = policy.w[i]
      for j in 0..<n_s {
        row[j] = row[j] + lr * d_logit * x[j]
      }
    }
  }
}

///|
/// Train REINFORCE for `n_episodes` episodes. Returns the mean
/// episode return (undiscounted).
pub fn train_reinforce(
  env : GridWorld,
  policy : LinearSoftmaxPolicy,
  n_episodes : Int,
  gamma : Float,
  lr : Float,
  max_steps : Int,
  seed : UInt64,
) -> Float {
  let rng = Xoshiro::from_state(seed, seed + 7UL, seed + 13UL, seed + 17UL)
  let mut total_return = 0.0F
  for _ep in 0..<n_episodes {
    let episode = policy_rollout(env, policy, max_steps, rng)
    total_return = total_return + episode_return(episode)
    let returns = compute_returns(episode.rewards, gamma)
    policy_gradient_update(policy, episode, returns, lr)
  }
  total_return / Float::from_int(n_episodes)
}
