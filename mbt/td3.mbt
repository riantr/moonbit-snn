// td3.mbt — Twin Delayed DDPG (Fujimoto et al. 2018).
//
// TD3 improves on DDPG (v0.54.0) with three modifications:
//   1. Twin Q-networks (already in our DDPG — same dual critic structure).
//   2. Target policy smoothing: when computing the Q-target, add
//      clipped Gaussian noise to the target action to reduce
//      variance from Q-function approximation errors.
//        a' = clip(μ̂(s') + clip(ε, -clip_range, clip_range), a_low, a_high)
//        where ε ~ N(0, target_noise_std)
//   3. Delayed policy update: update the actor and target networks
//      every `policy_delay` critic updates (instead of every step).
//
// The TD3 agent reuses the same networks as DDPG (deterministic actor,
// twin continuous-action critics, twin targets) — only the *update
// logic* differs.
//
// Reference:
//   Fujimoto, van Hoof, Meger, "Addressing Function Approximation
//   Error in Actor-Critic Methods" (ICML 2018).

///|
/// TD3 agent. Same networks as DDPG; the only new fields are
/// `target_noise_std` and `policy_delay`.
pub struct TD3 {
  actor : DeterministicPolicy
  critic1 : QNetworkContinuous
  critic2 : QNetworkContinuous
  actor_target : DeterministicPolicy
  critic1_target : QNetworkContinuous
  critic2_target : QNetworkContinuous
  gamma : Float
  tau : Float
  exploration_noise : Float
  // TD3-specific.
  target_noise_std : Float
  target_noise_clip : Float
  policy_delay : Int
}

///|
/// Construct a TD3 agent. Mirrors DDPG::new with two extra parameters:
/// `target_noise_std` (Gaussian noise stddev for target smoothing) and
/// `policy_delay` (number of critic updates between actor + target updates).
pub fn TD3::new(
  state_dim : Int,
  action_dim : Int,
  hidden : Int,
  action_low : Float,
  action_high : Float,
  gamma : Float,
  tau : Float,
  exploration_noise : Float,
  target_noise_std : Float,
  target_noise_clip : Float,
  policy_delay : Int,
  seed : UInt64,
) -> TD3 {
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
    target_noise_std,
    target_noise_clip,
    policy_delay,
  }
}

///|
/// Deterministic policy action (no noise). For evaluation.
pub fn td3_select_action_eval(
  td3 : TD3,
  state : Array[Float],
) -> Array[Float] {
  let hidden : Array[Float] = Array::make(td3.actor.hidden, 0.0F)
  deterministic_policy_forward(td3.actor, state, hidden)
}

///|
/// Action with Gaussian exploration noise (clipped to action range).
pub fn td3_select_action(
  td3 : TD3,
  state : Array[Float],
  rng : Xoshiro,
) -> Array[Float] {
  let action = td3_select_action_eval(td3, state)
  let noisy : Array[Float] = Array::make(td3.actor.action_dim, 0.0F)
  for a in 0..<td3.actor.action_dim {
    let (z, _) = box_muller(rng)
    let noise = Float::from_double(z) * td3.exploration_noise
    let mut v = action[a] + noise
    if v < td3.actor.action_low {
      v = td3.actor.action_low
    } else if v > td3.actor.action_high {
      v = td3.actor.action_high
    }
    ignore(noisy.set(a, v))
  }
  noisy
}

///|
/// Compute the target action for TD3: target actor + clipped Gaussian
/// noise, clipped to action range. This is "target policy smoothing".
fn td3_smoothed_target_action(
  td3 : TD3,
  next_state : Array[Float],
  rng : Xoshiro,
  hidden_buf : Array[Float],
) -> Array[Float] {
  let target_act = deterministic_policy_forward(
    td3.actor_target, next_state, hidden_buf,
  )
  let smoothed : Array[Float] = Array::make(td3.actor.action_dim, 0.0F)
  let clip = td3.target_noise_clip
  let action_low = td3.actor.action_low
  let action_high = td3.actor.action_high
  for a in 0..<td3.actor.action_dim {
    let (z, _) = box_muller(rng)
    let noise = Float::from_double(z) * td3.target_noise_std
    let clipped_noise = if noise > clip {
      clip
    } else if noise < -clip {
      -clip
    } else {
      noise
    }
    let mut v = target_act[a] + clipped_noise
    if v < action_low {
      v = action_low
    } else if v > action_high {
      v = action_high
    }
    ignore(smoothed.set(a, v))
  }
  smoothed
}

///|
/// Run one TD3 update step on a mini-batch. Returns the mean absolute
/// TD error across both critics. Updates critics every call; updates
/// actor + targets only when `step_count % policy_delay == 0`.
///
/// Implementation:
///   1. Compute smoothed target action a'.
///   2. Compute twin Q-targets on (next_state, a').
///   3. Twin-min: q_target = r + γ(1-d)·min(Q̂1, Q̂2).
///   4. Update both online critics via analytic gradient.
///   5. If step_count % policy_delay == 0, update actor + soft-sync targets.
pub fn td3_update_step(
  td3 : TD3,
  batch : (Array[Float], Array[Float], Array[Float], Array[Float], Array[Float]),
  lr_critic : Float,
  lr_actor : Float,
  step_count : Int,
  rng : Xoshiro,
) -> Float {
  let (states_b, actions_b, rewards_b, next_states_b, dones_b) = batch
  let batch_size = rewards_b.length()
  let s_dim = td3.actor.state_dim
  let a_dim = td3.actor.action_dim
  let hidden_dim = td3.actor.hidden
  let mut total_abs_td = 0.0F
  let hidden_q1 : Array[Float] = Array::make(hidden_dim, 0.0F)
  let hidden_q2 : Array[Float] = Array::make(hidden_dim, 0.0F)
  let hidden_q1_t : Array[Float] = Array::make(hidden_dim, 0.0F)
  let hidden_q2_t : Array[Float] = Array::make(hidden_dim, 0.0F)
  let hidden_actor_t : Array[Float] = Array::make(hidden_dim, 0.0F)
  for k in 0..<batch_size {
    let s_off = k * s_dim
    let a_off = k * a_dim
    let state_k : Array[Float] = Array::make(s_dim, 0.0F)
    let action_k : Array[Float] = Array::make(a_dim, 0.0F)
    let next_state_k : Array[Float] = Array::make(s_dim, 0.0F)
    for j in 0..<s_dim {
      state_k[j] = states_b[s_off + j]
      next_state_k[j] = next_states_b[s_off + j]
    }
    for j in 0..<a_dim {
      action_k[j] = actions_b[a_off + j]
    }
    let r = rewards_b[k]
    let done = dones_b[k]
    // Step 1+2: smoothed target action + twin Q-targets.
    let next_action = td3_smoothed_target_action(
      td3, next_state_k, rng, hidden_actor_t,
    )
    let q1_t = qnet_continuous_forward(
      td3.critic1_target, next_state_k, next_action, hidden_q1_t,
    )
    let q2_t = qnet_continuous_forward(
      td3.critic2_target, next_state_k, next_action, hidden_q2_t,
    )
    let q_target = r + td3.gamma * (1.0F - done) *
      (if q1_t < q2_t { q1_t } else { q2_t })
    // Step 3+4: critic forward + update.
    let q1 = qnet_continuous_forward(
      td3.critic1, state_k, action_k, hidden_q1,
    )
    let q2 = qnet_continuous_forward(
      td3.critic2, state_k, action_k, hidden_q2,
    )
    let td1 = q1 - q_target
    let td2 = q2 - q_target
    total_abs_td = total_abs_td +
      (if td1 < 0.0F { -td1 } else { td1 }) +
      (if td2 < 0.0F { -td2 } else { td2 })
    td3_apply_critic_grad(td3.critic1, state_k, action_k, hidden_q1, 2.0F * td1 * lr_critic)
    td3_apply_critic_grad(td3.critic2, state_k, action_k, hidden_q2, 2.0F * td2 * lr_critic)
  }
  let _ = hidden_q1
  let _ = hidden_q2
  let _ = hidden_q1_t
  let _ = hidden_q2_t
  let _ = hidden_actor_t
  // Step 5: delayed actor + target update.
  if step_count % td3.policy_delay == 0 && step_count > 0 {
    td3_actor_update(td3, batch, lr_actor)
    td3_soft_update(td3)
  }
  total_abs_td / Float::from_int(batch_size)
}

///|
/// Apply critic gradient (same as DDPG's critic_apply_grad).
fn td3_apply_critic_grad(
  net : QNetworkContinuous,
  state : Array[Float],
  action : Array[Float],
  hidden : Array[Float],
  delta : Float,
) -> Unit {
  for h in 0..<net.hidden {
    net.w2[h] = net.w2[h] - delta * hidden[h]
  }
  net.b2 = net.b2 - delta
  for h in 0..<net.hidden {
    let grad_h = delta * net.w2[h]
    let gate = if hidden[h] > 0.0F { 1.0F } else { 0.0F }
    let grad_h_eff = grad_h * gate
    net.b1[h] = net.b1[h] - grad_h_eff
    for j in 0..<net.state_dim {
      net.w1[h][j] = net.w1[h][j] - grad_h_eff * state[j]
    }
    for j in 0..<net.action_dim {
      net.w1[h][net.state_dim + j] = net.w1[h][net.state_dim + j] -
        grad_h_eff * action[j]
    }
  }
}

///|
/// Deterministic-policy gradient for the actor (same as DDPG).
fn td3_actor_update(
  td3 : TD3,
  batch : (Array[Float], Array[Float], Array[Float], Array[Float], Array[Float]),
  lr_actor : Float,
) -> Unit {
  let (states_b, _actions_b, _rewards_b, _next_states_b, _dones_b) = batch
  let batch_size = states_b.length() / td3.actor.state_dim
  let s_dim = td3.actor.state_dim
  let a_dim = td3.actor.action_dim
  let hidden_dim = td3.actor.hidden
  let hidden_act : Array[Float] = Array::make(hidden_dim, 0.0F)
  let hidden_q : Array[Float] = Array::make(hidden_dim, 0.0F)
  let range = (td3.actor.action_high - td3.actor.action_low) * 0.5F
  for k in 0..<batch_size {
    let s_off = k * s_dim
    let state_k : Array[Float] = Array::make(s_dim, 0.0F)
    for j in 0..<s_dim {
      state_k[j] = states_b[s_off + j]
    }
    let action_k = deterministic_policy_forward(td3.actor, state_k, hidden_act)
    let _ = qnet_continuous_forward(
      td3.critic1, state_k, action_k, hidden_q,
    )
    for a in 0..<a_dim {
      let mut g_a_j = 0.0F
      for h in 0..<hidden_dim {
        let gate = if hidden_q[h] > 0.0F { 1.0F } else { 0.0F }
        g_a_j = g_a_j + td3.critic1.w1[h][s_dim + a] *
          td3.critic1.w2[h] * gate
      }
      let mut s_pre = td3.actor.b2[a]
      for h in 0..<hidden_dim {
        s_pre = s_pre + td3.actor.w2[a][h] * hidden_act[h]
      }
      let t = Float::from_double(@math.tanh(s_pre.to_double()))
      let one_minus_t2 = 1.0F - t * t
      for h in 0..<hidden_dim {
        let grad_w2 = g_a_j * one_minus_t2 * range * hidden_act[h]
        td3.actor.w2[a][h] = td3.actor.w2[a][h] + lr_actor * grad_w2
      }
      td3.actor.b2[a] = td3.actor.b2[a] + lr_actor *
        g_a_j * one_minus_t2 * range
    }
  }
}

///|
/// Polyak soft-update for all 5 networks (same as DDPG).
pub fn td3_soft_update(td3 : TD3) -> Unit {
  let tau = td3.tau
  let one_minus_tau = 1.0F - tau
  for h in 0..<td3.actor_target.hidden {
    td3.actor_target.b1[h] = one_minus_tau * td3.actor_target.b1[h] +
      tau * td3.actor.b1[h]
    for s in 0..<td3.actor_target.state_dim {
      td3.actor_target.w1[h][s] = one_minus_tau *
        td3.actor_target.w1[h][s] + tau * td3.actor.w1[h][s]
    }
  }
  for a in 0..<td3.actor_target.action_dim {
    td3.actor_target.b2[a] = one_minus_tau * td3.actor_target.b2[a] +
      tau * td3.actor.b2[a]
    for h in 0..<td3.actor_target.hidden {
      td3.actor_target.w2[a][h] = one_minus_tau *
        td3.actor_target.w2[a][h] + tau * td3.actor.w2[a][h]
    }
  }
  let in_dim = td3.critic1_target.state_dim + td3.critic1_target.action_dim
  for h in 0..<td3.critic1_target.hidden {
    td3.critic1_target.b1[h] = one_minus_tau * td3.critic1_target.b1[h] +
      tau * td3.critic1.b1[h]
    for k in 0..<in_dim {
      td3.critic1_target.w1[h][k] = one_minus_tau *
        td3.critic1_target.w1[h][k] + tau * td3.critic1.w1[h][k]
    }
  }
  for h in 0..<td3.critic1_target.hidden {
    td3.critic1_target.w2[h] = one_minus_tau * td3.critic1_target.w2[h] +
      tau * td3.critic1.w2[h]
  }
  td3.critic1_target.b2 = one_minus_tau * td3.critic1_target.b2 +
    tau * td3.critic1.b2
  let in_dim2 = td3.critic2_target.state_dim + td3.critic2_target.action_dim
  for h in 0..<td3.critic2_target.hidden {
    td3.critic2_target.b1[h] = one_minus_tau * td3.critic2_target.b1[h] +
      tau * td3.critic2.b1[h]
    for k in 0..<in_dim2 {
      td3.critic2_target.w1[h][k] = one_minus_tau *
        td3.critic2_target.w1[h][k] + tau * td3.critic2.w1[h][k]
    }
  }
  for h in 0..<td3.critic2_target.hidden {
    td3.critic2_target.w2[h] = one_minus_tau * td3.critic2_target.w2[h] +
      tau * td3.critic2.w2[h]
  }
  td3.critic2_target.b2 = one_minus_tau * td3.critic2_target.b2 +
    tau * td3.critic2.b2
}

///|
/// Train TD3 for `n_episodes`. Mirrors `train_ddpg` but uses
/// `td3_update_step` (which handles delayed actor updates internally).
pub fn train_td3(
  env_state_dim : Int,
  env_action_dim : Int,
  env_step : (Array[Float], Array[Float]) -> (Array[Float], Float, Bool),
  td3 : TD3,
  n_episodes : Int,
  buffer_capacity : Int,
  warmup_episodes : Int,
  batch_size : Int,
  max_steps : Int,
  seed : UInt64,
) -> Float {
  let rng = Xoshiro::from_state(seed, seed + 7UL, seed + 13UL, seed + 17UL)
  let buffer = ContinuousReplayBuffer::new(buffer_capacity, env_state_dim, env_action_dim)
  let mut total_return = 0.0F
  let mut return_count = 0
  let mut global_step = 0
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
      let action = td3_select_action(td3, state, rng)
      let (next_state, r, d) = env_step(state, action)
      buffer.push(state, action, r, next_state, d)
      ep_return = ep_return + r
      state = next_state
      done = d
      t = t + 1
      global_step = global_step + 1
    }
    total_return = total_return + ep_return
    return_count = return_count + 1
    // Train.
    if ep >= warmup_episodes && buffer.len() >= batch_size {
      let batch = buffer.sample(batch_size, rng)
      let _ = td3_update_step(td3, batch, 0.001F, 0.0001F, global_step, rng)
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