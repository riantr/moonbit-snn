// actor_critic.mbt — Advantage Actor-Critic (A2C) on GridWorld (v0.33.1).
//
// One-step TD(0) actor-critic:
//   - Critic (value net): V(s) = w_v · x_s
//     Update: δ_t = r_t + γ · V(s_{t+1}) · (1 - done_t) - V(s_t)
//             w_v += α_v · δ_t · x_{s_t}
//   - Actor (policy): REINFORCE with baseline = TD error
//     Update: w += α_p · δ_t · ∇log π(a_t | s_t)
//
// Reuses `LinearSoftmaxPolicy` and `policy_gradient_update` from
// reinforce.mbt. The policy gradient update is re-derived inline
// here so the actor-critic delta replaces the return G_t.

///|
/// Linear value network: V(s) = w · x_s, where x_s is the
/// one-hot encoding of state s. Length n_states weights.
pub struct LinearValueNet {
  n_states : Int
  w : Array[Float]
}

///|
pub fn LinearValueNet::new(n_states : Int, seed : UInt64) -> LinearValueNet {
  let std = sqrtf(0.1F / Float::from_int(n_states))
  let rng = Xoshiro::from_state(seed, seed + 1UL, seed + 2UL, seed + 3UL)
  let w : Array[Float] = Array::make(n_states, 0.0F)
  for i in 0..<n_states {
    let (z1, _) = box_muller(rng)
    w[i] = Float::from_double(z1) * std
  }
  { n_states, w }
}

///|
/// Forward: V(s) = w · x.
pub fn value_forward(net : LinearValueNet, x : Array[Float]) -> Float {
  let mut s = 0.0F
  for i in 0..<x.length() {
    s = s + net.w[i] * x[i]
  }
  s
}

///|
/// Episode record augmented with value estimates for actor-critic.
pub(all) struct EpisodeWithValues {
  states : Array[Int]
  actions : Array[Int]
  rewards : Array[Float]
  values : Array[Float]  // V(s_t)
  next_values : Array[Float]  // V(s_{t+1}); 0 if terminal
  dones : Array[Bool]
}

///|
/// Roll out one episode under the current policy, recording values.
pub fn actor_critic_rollout(
  env : GridWorld,
  policy : LinearSoftmaxPolicy,
  value_net : LinearValueNet,
  max_steps : Int,
  rng : Xoshiro,
) -> EpisodeWithValues {
  let states : Array[Int] = []
  let actions : Array[Int] = []
  let rewards : Array[Float] = []
  let values : Array[Float] = []
  let next_values : Array[Float] = []
  let dones : Array[Bool] = []
  let mut state = env.start
  let mut done = false
  let mut t = 0
  while !done && t < max_steps {
    let x = rl_one_hot(state, env.n_states())
    let v = value_forward(value_net, x)
    let (_logits, probs) = policy_forward(policy, x)
    let (a, _lp) = sample_categorical(probs, rng)
    let (next_state, r, d) = env.step(state, a)
    let next_x = rl_one_hot(next_state, env.n_states())
    let v_next = if d { 0.0F } else { value_forward(value_net, next_x) }
    states.push(state)
    actions.push(a)
    rewards.push(r)
    values.push(v)
    next_values.push(v_next)
    dones.push(d)
    state = next_state
    done = d
    t = t + 1
  }
  { states, actions, rewards, values, next_values, dones }
}

///|
/// One-step TD actor-critic update. Computes per-step TD errors
/// `δ_t = r_t + γ · V(s_{t+1}) · (1 - done_t) - V(s_t)`, then:
///
///   - critic: w_v += α_v · δ_t · x_{s_t}
///   - actor:  d_logit[i] = (1{i == a_t} - π(a|s_t)) · δ_t
///             w += α_p · d_logit ⊗ x_{s_t}
///
/// Returns the mean absolute TD error over the episode.
pub fn actor_critic_update(
  policy : LinearSoftmaxPolicy,
  value_net : LinearValueNet,
  episode : EpisodeWithValues,
  gamma : Float,
  lr_policy : Float,
  lr_value : Float,
) -> Float {
  let t = episode.states.length()
  let n_a = policy.n_actions
  let n_s = policy.n_states
  let mut total_abs_delta = 0.0F
  for step in 0..<t {
    let r = episode.rewards[step]
    let v = episode.values[step]
    let v_next = episode.next_values[step]
    let d_t = episode.dones[step]
    let not_done = if d_t { 0.0F } else { 1.0F }
    let delta = r + gamma * v_next * not_done - v
    let abs_d = if delta < 0.0F { -delta } else { delta }
    total_abs_delta = total_abs_delta + abs_d
    // Critic update.
    let x = rl_one_hot(episode.states[step], n_s)
    for i in 0..<n_s {
      value_net.w[i] = value_net.w[i] + lr_value * delta * x[i]
    }
    // Actor update with TD error as the (signed) baseline.
    if delta != 0.0F {
      let (_logits, probs) = policy_forward(policy, x)
      let a = episode.actions[step]
      for i in 0..<n_a {
        let indicator = if i == a { 1.0F } else { 0.0F }
        let d_logit = (indicator - probs[i]) * delta
        let row = policy.w[i]
        for j in 0..<n_s {
          row[j] = row[j] + lr_policy * d_logit * x[j]
        }
      }
    }
  }
  total_abs_delta / Float::from_int(t)
}

///|
/// Train A2C for `n_episodes`. Returns the mean undiscounted
/// episode return.
pub fn train_actor_critic(
  env : GridWorld,
  policy : LinearSoftmaxPolicy,
  value_net : LinearValueNet,
  n_episodes : Int,
  gamma : Float,
  lr_policy : Float,
  lr_value : Float,
  max_steps : Int,
  seed : UInt64,
) -> Float {
  let rng = Xoshiro::from_state(seed, seed + 7UL, seed + 13UL, seed + 17UL)
  let mut total_return = 0.0F
  for _ep in 0..<n_episodes {
    let episode = actor_critic_rollout(env, policy, value_net, max_steps, rng)
    let mut ep_return = 0.0F
    for r in episode.rewards {
      ep_return = ep_return + r
    }
    total_return = total_return + ep_return
    let _ = actor_critic_update(policy, value_net, episode, gamma, lr_policy, lr_value)
  }
  total_return / Float::from_int(n_episodes)
}