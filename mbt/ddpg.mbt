// ddpg.mbt �?Deep Deterministic Policy Gradient (Lillicrap et al. 2015).
//
// DDPG learns a deterministic policy μ(s; θ^μ) that maps states to a
// continuous action, plus a critic Q(s, a; θ^Q) for off-policy TD
// learning. Like SAC/DSAC, it uses twin critics to mitigate Q-value
// overestimation and Polyak-averaged target networks for stability.
//
// Differences vs SAC/DSAC:
//   - The actor is deterministic (no reparameterisation trick needed).
//     Exploration comes from additive Gaussian noise during training.
//   - Critic takes [state, action] as input and outputs a single scalar.
//   - No entropy bonus in the actor loss.
//
// Differences vs TD3 (next version):
//   - DDPG uses a single critic (we use twin for stability).
//   - DDPG does NOT smooth the target policy or delay actor updates.
//
// References:
//   - Lillicrap et al. 2015, "Continuous Control with Deep Reinforcement
//     Learning" (ICLR).
//   - Plappert et al. 2018 (twin-critic variant).

// =========================================================================
// DeterministicPolicy: state -> continuous action
// =========================================================================

///|
/// Deterministic policy network. One hidden layer with ReLU activation;
/// output is squashed via tanh to `[action_low, action_high]` per dim.
pub struct DeterministicPolicy {
  state_dim : Int
  action_dim : Int
  hidden : Int
  // W1[hidden, state_dim], b1[hidden]
  w1 : Array[Array[Float]]
  b1 : Array[Float]
  // W2[action_dim, hidden], b2[action_dim]
  w2 : Array[Array[Float]]
  b2 : Array[Float]
  action_low : Float
  action_high : Float
}

///|
/// Construct a DeterministicPolicy. Hidden size defaults to 64.
pub fn DeterministicPolicy::new(
  state_dim : Int,
  action_dim : Int,
  hidden : Int,
  action_low : Float,
  action_high : Float,
  seed : UInt64,
) -> DeterministicPolicy {
  let rng = Xoshiro::from_state(seed, seed + 7UL, seed + 13UL, seed + 17UL)
  let bound_w1 = Float::from_double(@math.pow(1.0 / Float::from_int(state_dim).to_double(), 0.5))
  let bound_w2 = Float::from_double(@math.pow(1.0 / Float::from_int(hidden).to_double(), 0.5))
  let w1 : Array[Array[Float]] = Array::make(hidden, [])
  let b1 : Array[Float] = Array::make(hidden, 0.0F)
  for h in 0..<hidden {
    let row : Array[Float] = Array::make(state_dim, 0.0F)
    for s in 0..<state_dim {
      let (z, _) = box_muller(rng)
      row[s] = Float::from_double(z) * bound_w1
    }
    ignore(w1.set(h, row))
  }
  let w2 : Array[Array[Float]] = Array::make(action_dim, [])
  let b2 : Array[Float] = Array::make(action_dim, 0.0F)
  for a in 0..<action_dim {
    let row : Array[Float] = Array::make(hidden, 0.0F)
    for h in 0..<hidden {
      let (z, _) = box_muller(rng)
      row[h] = Float::from_double(z) * bound_w2
    }
    ignore(w2.set(a, row))
  }
  {
    state_dim,
    action_dim,
    hidden,
    w1,
    b1,
    w2,
    b2,
    action_low,
    action_high,
  }
}

///|
/// Forward pass: state -> continuous action (in [action_low, action_high]).
/// `hidden_state_out` (optional) is filled with the post-ReLU hidden layer
/// for use in the critic's gradient computation.
pub fn deterministic_policy_forward(
  net : DeterministicPolicy,
  state : Array[Float],
  hidden_state_out : Array[Float],
) -> Array[Float] {
  // h = relu(W1 · state + b1)
  for h in 0..<net.hidden {
    let mut s = net.b1[h]
    for k in 0..<net.state_dim {
      s = s + net.w1[h][k] * state[k]
    }
    let relu = if s > 0.0F { s } else { 0.0F }
    ignore(hidden_state_out.set(h, relu))
  }
  // a = tanh(W2 · h + b2), then rescale to [action_low, action_high]
  let action : Array[Float] = Array::make(net.action_dim, 0.0F)
  let range = (net.action_high - net.action_low) * 0.5F
  let mid = (net.action_high + net.action_low) * 0.5F
  for a in 0..<net.action_dim {
    let mut s = net.b2[a]
    for h in 0..<net.hidden {
      s = s + net.w2[a][h] * hidden_state_out[h]
    }
    // tanh(s) via Double math.
    let t = Float::from_double(@math.tanh(s.to_double()))
    action[a] = t * range + mid
  }
  action
}

///|
/// Copy weights from `src` into `dst` (used by soft_update).
pub fn policy_copy(
  dst : DeterministicPolicy,
  src : DeterministicPolicy,
) -> Unit {
  for h in 0..<dst.hidden {
    dst.b1[h] = src.b1[h]
    for s in 0..<dst.state_dim {
      dst.w1[h][s] = src.w1[h][s]
    }
  }
  for a in 0..<dst.action_dim {
    dst.b2[a] = src.b2[a]
    for h in 0..<dst.hidden {
      dst.w2[a][h] = src.w2[a][h]
    }
  }
}

// =========================================================================
// QNetworkContinuous: [state, action] -> scalar Q
// =========================================================================

///|
/// Continuous Q-network: takes state + action as input, outputs scalar Q.
/// One hidden layer with ReLU; linear output.
pub struct QNetworkContinuous {
  state_dim : Int
  action_dim : Int
  hidden : Int
  // W1[hidden, state_dim + action_dim], b1[hidden]
  w1 : Array[Array[Float]]
  b1 : Array[Float]
  // W2[1, hidden], b2[1]
  w2 : Array[Float]
  mut b2 : Float
}

///|
/// Construct a continuous Q-network.
pub fn QNetworkContinuous::new(
  state_dim : Int,
  action_dim : Int,
  hidden : Int,
  seed : UInt64,
) -> QNetworkContinuous {
  let rng = Xoshiro::from_state(seed, seed + 11UL, seed + 23UL, seed + 37UL)
  let in_dim = state_dim + action_dim
  let bound_w1 = Float::from_double(@math.pow(1.0 / Float::from_int(in_dim).to_double(), 0.5))
  let bound_w2 = Float::from_double(@math.pow(1.0 / Float::from_int(hidden).to_double(), 0.5))
  let w1 : Array[Array[Float]] = Array::make(hidden, [])
  let b1 : Array[Float] = Array::make(hidden, 0.0F)
  for h in 0..<hidden {
    let row : Array[Float] = Array::make(in_dim, 0.0F)
    for k in 0..<in_dim {
      let (z, _) = box_muller(rng)
      row[k] = Float::from_double(z) * bound_w1
    }
    ignore(w1.set(h, row))
  }
  let w2 : Array[Float] = Array::make(hidden, 0.0F)
  for h in 0..<hidden {
    let (z, _) = box_muller(rng)
    w2[h] = Float::from_double(z) * bound_w2
  }
  let b2 : Float = 0.0F
  { state_dim, action_dim, hidden, w1, b1, w2, b2 }
}

///|
/// Forward pass: [state, action] -> scalar Q. `hidden_out` is filled with
/// the post-ReLU hidden state for use in critic gradient computation.
pub fn qnet_continuous_forward(
  net : QNetworkContinuous,
  state : Array[Float],
  action : Array[Float],
  hidden_out : Array[Float],
) -> Float {
  let in_dim = net.state_dim + net.action_dim
  // h = relu(W1 · [state; action] + b1)
  for h in 0..<net.hidden {
    let mut s = net.b1[h]
    // state contribution
    for k in 0..<net.state_dim {
      s = s + net.w1[h][k] * state[k]
    }
    // action contribution
    for k in 0..<net.action_dim {
      s = s + net.w1[h][net.state_dim + k] * action[k]
    }
    let relu = if s > 0.0F { s } else { 0.0F }
    ignore(hidden_out.set(h, relu))
  }
  // q = W2 · h + b2
  let mut q = net.b2
  for h in 0..<net.hidden {
    q = q + net.w2[h] * hidden_out[h]
  }
  q
}

///|
/// Copy weights from `src` into `dst` (used by soft_update).
pub fn qnet_continuous_copy(
  dst : QNetworkContinuous,
  src : QNetworkContinuous,
) -> Unit {
  for h in 0..<dst.hidden {
    dst.b1[h] = src.b1[h]
    for k in 0..<(dst.state_dim + dst.action_dim) {
      dst.w1[h][k] = src.w1[h][k]
    }
  }
  for h in 0..<dst.hidden {
    dst.w2[h] = src.w2[h]
  }
  dst.b2 = src.b2
}

// =========================================================================
// DDPG agent
// =========================================================================

///|
/// DDPG agent: deterministic actor + twin critics + targets + γ + τ.
pub struct DDPG {
  actor : DeterministicPolicy
  critic1 : QNetworkContinuous
  critic2 : QNetworkContinuous
  actor_target : DeterministicPolicy
  critic1_target : QNetworkContinuous
  critic2_target : QNetworkContinuous
  gamma : Float
  tau : Float
  exploration_noise : Float
}

///|
/// Construct a DDPG agent. Targets are initialised by copying online.
pub fn DDPG::new(
  state_dim : Int,
  action_dim : Int,
  hidden : Int,
  action_low : Float,
  action_high : Float,
  gamma : Float,
  tau : Float,
  exploration_noise : Float,
  seed : UInt64,
) -> DDPG {
  let actor = DeterministicPolicy::new(
    state_dim, action_dim, hidden, action_low, action_high, seed,
  )
  let critic1 = QNetworkContinuous::new(state_dim, action_dim, hidden, seed + 1UL)
  let critic2 = QNetworkContinuous::new(state_dim, action_dim, hidden, seed + 2UL)
  let actor_target = DeterministicPolicy::new(
    state_dim, action_dim, hidden, action_low, action_high, seed + 3UL,
  )
  let critic1_target = QNetworkContinuous::new(
    state_dim, action_dim, hidden, seed + 4UL,
  )
  let critic2_target = QNetworkContinuous::new(
    state_dim, action_dim, hidden, seed + 5UL,
  )
  policy_copy(actor_target, actor)
  qnet_continuous_copy(critic1_target, critic1)
  qnet_continuous_copy(critic2_target, critic2)
  {
    actor,
    critic1,
    critic2,
    actor_target,
    critic1_target,
    critic2_target,
    gamma,
    tau,
    exploration_noise,
  }
}

///|
/// Deterministic policy action. Returns `actor(state)` for evaluation.
pub fn ddpg_select_action_eval(
  ddpg : DDPG,
  state : Array[Float],
) -> Array[Float] {
  let hidden : Array[Float] = Array::make(ddpg.actor.hidden, 0.0F)
  deterministic_policy_forward(ddpg.actor, state, hidden)
}

///|
/// Action with Gaussian exploration noise (clipped to action range).
/// Used during training.
pub fn ddpg_select_action(
  ddpg : DDPG,
  state : Array[Float],
  rng : Xoshiro,
) -> Array[Float] {
  let action = ddpg_select_action_eval(ddpg, state)
  let noisy : Array[Float] = Array::make(ddpg.actor.action_dim, 0.0F)
  for a in 0..<ddpg.actor.action_dim {
    let (z, _) = box_muller(rng)
    let noise = Float::from_double(z) * ddpg.exploration_noise
    let mut v = action[a] + noise
    if v < ddpg.actor.action_low {
      v = ddpg.actor.action_low
    } else if v > ddpg.actor.action_high {
      v = ddpg.actor.action_high
    }
    ignore(noisy.set(a, v))
  }
  noisy
}

///|
/// Soft (Polyak) target update: target �?(1-τ)·target + τ·online.
pub fn ddpg_soft_update(ddpg : DDPG) -> Unit {
  let tau = ddpg.tau
  let one_minus_tau = 1.0F - tau
  // Actor target.
  for h in 0..<ddpg.actor_target.hidden {
    ddpg.actor_target.b1[h] = one_minus_tau * ddpg.actor_target.b1[h] +
      tau * ddpg.actor.b1[h]
    for s in 0..<ddpg.actor_target.state_dim {
      ddpg.actor_target.w1[h][s] = one_minus_tau * ddpg.actor_target.w1[h][s] +
        tau * ddpg.actor.w1[h][s]
    }
  }
  for a in 0..<ddpg.actor_target.action_dim {
    ddpg.actor_target.b2[a] = one_minus_tau * ddpg.actor_target.b2[a] +
      tau * ddpg.actor.b2[a]
    for h in 0..<ddpg.actor_target.hidden {
      ddpg.actor_target.w2[a][h] = one_minus_tau * ddpg.actor_target.w2[a][h] +
        tau * ddpg.actor.w2[a][h]
    }
  }
  // Critic1 target.
  let in_dim = ddpg.critic1_target.state_dim + ddpg.critic1_target.action_dim
  for h in 0..<ddpg.critic1_target.hidden {
    ddpg.critic1_target.b1[h] = one_minus_tau * ddpg.critic1_target.b1[h] +
      tau * ddpg.critic1.b1[h]
    for k in 0..<in_dim {
      ddpg.critic1_target.w1[h][k] = one_minus_tau *
        ddpg.critic1_target.w1[h][k] + tau * ddpg.critic1.w1[h][k]
    }
  }
  for h in 0..<ddpg.critic1_target.hidden {
    ddpg.critic1_target.w2[h] = one_minus_tau * ddpg.critic1_target.w2[h] +
      tau * ddpg.critic1.w2[h]
  }
  ddpg.critic1_target.b2 = one_minus_tau * ddpg.critic1_target.b2 +
    tau * ddpg.critic1.b2
  // Critic2 target (mirror of critic1).
  let in_dim2 = ddpg.critic2_target.state_dim + ddpg.critic2_target.action_dim
  for h in 0..<ddpg.critic2_target.hidden {
    ddpg.critic2_target.b1[h] = one_minus_tau * ddpg.critic2_target.b1[h] +
      tau * ddpg.critic2.b1[h]
    for k in 0..<in_dim2 {
      ddpg.critic2_target.w1[h][k] = one_minus_tau *
        ddpg.critic2_target.w1[h][k] + tau * ddpg.critic2.w1[h][k]
    }
  }
  for h in 0..<ddpg.critic2_target.hidden {
    ddpg.critic2_target.w2[h] = one_minus_tau * ddpg.critic2_target.w2[h] +
      tau * ddpg.critic2.w2[h]
  }
  ddpg.critic2_target.b2 = one_minus_tau * ddpg.critic2_target.b2 +
    tau * ddpg.critic2.b2
}

///|
/// Train DDPG for `n_episodes` episodes. Each episode:
///   1. Roll out under ddpg_select_action (with noise).
///   2. Store transitions in `buffer`.
///   3. After warmup, sample a mini-batch and call `ddpg_update_step`.
///   4. Polyak target update after each critic step.
///
/// Returns the mean episode return over the last 10 episodes.
pub fn train_ddpg(
  env_state_dim : Int,
  env_action_dim : Int,
  env_step : (Array[Float], Array[Float]) -> (Array[Float], Float, Bool),
  ddpg : DDPG,
  n_episodes : Int,
  buffer_capacity : Int,
  warmup_episodes : Int,
  batch_size : Int,
  sync_every : Int,
  max_steps : Int,
  seed : UInt64,
) -> Float {
  let rng = Xoshiro::from_state(seed, seed + 7UL, seed + 13UL, seed + 17UL)
  let buffer = ContinuousReplayBuffer::new(buffer_capacity, env_state_dim, env_action_dim)
  let mut total_return = 0.0F
  let mut return_count = 0
  for ep in 0..<n_episodes {
    let mut state = Array::make(env_state_dim, 0.0F)
    for k in 0..<env_state_dim {
      let (z, _) = box_muller(rng)
      state[k] = Float::from_double(z) * 0.5F
    }
    let mut done = false
    let mut t = 0
    let mut ep_return = 0.0F
    while !done && t < max_steps {
      let action = ddpg_select_action(ddpg, state, rng)
      let (next_state, r, d) = env_step(state, action)
      buffer.push(state, action, r, next_state, d)
      ep_return = ep_return + r
      state = next_state
      done = d
      t = t + 1
    }
    total_return = total_return + ep_return
    return_count = return_count + 1
    // Train.
    if ep >= warmup_episodes && buffer.len() >= batch_size {
      let batch = buffer.sample(batch_size, rng)
      let _ = ddpg_update_critic(ddpg, batch, 0.001F)
      let _ = ddpg_update_actor(ddpg, batch, 0.0001F)
    }
    // Polyak sync.
    if (ep + 1) % sync_every == 0 {
      ddpg_soft_update(ddpg)
    }
    let _ = env_action_dim
    let _ = env_state_dim
  }
  if return_count > 0 {
    total_return / Float::from_int(return_count)
  } else {
    0.0F
  }
}