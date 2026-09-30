// ddpg_update.mbt — DDPG training-time helpers: continuous replay buffer
// and per-step gradient update.

///|
/// Continuous replay buffer. Stores float-valued states/actions/rewards
/// rather than discrete indexes. Capacity-overflow is FIFO.
pub struct ContinuousReplayBuffer {
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
/// Construct a new continuous replay buffer.
pub fn ContinuousReplayBuffer::new(
  capacity : Int,
  state_dim : Int,
  action_dim : Int,
) -> ContinuousReplayBuffer {
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
/// Number of transitions currently stored.
pub fn ContinuousReplayBuffer::len(self : ContinuousReplayBuffer) -> Int {
  self.size
}

///|
/// Append a transition. Overwrites the oldest slot when full.
pub fn ContinuousReplayBuffer::push(
  self : ContinuousReplayBuffer,
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
/// Sample a mini-batch. Returns 5 parallel arrays (states, actions,
/// rewards, next_states, dones). The caller supplies its own RNG.
pub fn ContinuousReplayBuffer::sample(
  self : ContinuousReplayBuffer,
  batch_size : Int,
  rng : Xoshiro,
) -> (Array[Float], Array[Float], Array[Float], Array[Float], Array[Float]) {
  let states : Array[Float] = Array::make(batch_size * self.state_dim, 0.0F)
  let actions : Array[Float] = Array::make(batch_size * self.action_dim, 0.0F)
  let rewards : Array[Float] = Array::make(batch_size, 0.0F)
  let next_states : Array[Float] = Array::make(batch_size * self.state_dim, 0.0F)
  let dones : Array[Float] = Array::make(batch_size, 0.0F)
  for k in 0..<batch_size {
    let (z, _) = box_muller(rng)
    let u_pos = if z < 0.0 { -z } else { z }
    let idx = Float::from_double(u_pos * Double::from_int(self.size)).to_int()
    let safe = if idx < 0 {
      0
    } else if idx >= self.size {
      self.size - 1
    } else {
      idx
    }
    let s_off = safe * self.state_dim
    let a_off = safe * self.action_dim
    let b_s_off = k * self.state_dim
    let b_a_off = k * self.action_dim
    for j in 0..<self.state_dim {
      states[b_s_off + j] = self.states[s_off + j]
      next_states[b_s_off + j] = self.next_states[s_off + j]
    }
    for j in 0..<self.action_dim {
      actions[b_a_off + j] = self.actions[a_off + j]
    }
    rewards[k] = self.rewards[safe]
    dones[k] = self.dones[safe]
  }
  (states, actions, rewards, next_states, dones)
}

///|
/// Run one DDPG critic update step on a mini-batch. Returns the mean
/// absolute TD error. Updates critic1 and critic2 in place via analytic
/// gradient; the actor is left for a separate step.
pub fn ddpg_update_critic(
  ddpg : DDPG,
  batch : (Array[Float], Array[Float], Array[Float], Array[Float], Array[Float]),
  lr_critic : Float,
) -> Float {
  let (states_b, actions_b, rewards_b, next_states_b, dones_b) = batch
  let batch_size = rewards_b.length()
  let s_dim = ddpg.actor.state_dim
  let a_dim = ddpg.actor.action_dim
  let hidden_dim = ddpg.actor.hidden
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
    let next_action = deterministic_policy_forward(
      ddpg.actor_target, next_state_k, hidden_actor_t,
    )
    let q1_t = qnet_continuous_forward(
      ddpg.critic1_target, next_state_k, next_action, hidden_q1_t,
    )
    let q2_t = qnet_continuous_forward(
      ddpg.critic2_target, next_state_k, next_action, hidden_q2_t,
    )
    let q_target = r + ddpg.gamma * (1.0F - done) *
      (if q1_t < q2_t { q1_t } else { q2_t })
    let q1 = qnet_continuous_forward(
      ddpg.critic1, state_k, action_k, hidden_q1,
    )
    let q2 = qnet_continuous_forward(
      ddpg.critic2, state_k, action_k, hidden_q2,
    )
    let td1 = q1 - q_target
    let td2 = q2 - q_target
    total_abs_td = total_abs_td +
      (if td1 < 0.0F { -td1 } else { td1 }) +
      (if td2 < 0.0F { -td2 } else { td2 })
    critic_apply_grad(ddpg.critic1, state_k, action_k, hidden_q1, 2.0F * td1 * lr_critic)
    critic_apply_grad(ddpg.critic2, state_k, action_k, hidden_q2, 2.0F * td2 * lr_critic)
  }
  let _ = hidden_q1
  let _ = hidden_q2
  let _ = hidden_q1_t
  let _ = hidden_q2_t
  let _ = hidden_actor_t
  total_abs_td / Float::from_int(batch_size)
}

///|
/// Apply gradient `delta` to a continuous Q-network.
fn critic_apply_grad(
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
/// DDPG actor update step. Computes the deterministic-policy gradient
/// `∂Q/∂a · ∂a/∂θ^μ` on the sampled batch and applies the update to
/// actor (online; targets are not updated here).
///
/// Updates only W2 / b2 of the actor. Updating W1 / b1 of the actor
/// requires full chain rule through the hidden state and is a TODO for
/// a future version.
pub fn ddpg_update_actor(
  ddpg : DDPG,
  batch : (Array[Float], Array[Float], Array[Float], Array[Float], Array[Float]),
  lr_actor : Float,
) -> Unit {
  let (states_b, _actions_b, _rewards_b, _next_states_b, _dones_b) = batch
  let batch_size = states_b.length() / ddpg.actor.state_dim
  let s_dim = ddpg.actor.state_dim
  let a_dim = ddpg.actor.action_dim
  let hidden_dim = ddpg.actor.hidden
  let hidden_act : Array[Float] = Array::make(hidden_dim, 0.0F)
  let hidden_q : Array[Float] = Array::make(hidden_dim, 0.0F)
  let range = (ddpg.actor.action_high - ddpg.actor.action_low) * 0.5F
  for k in 0..<batch_size {
    let s_off = k * s_dim
    let state_k : Array[Float] = Array::make(s_dim, 0.0F)
    for j in 0..<s_dim {
      state_k[j] = states_b[s_off + j]
    }
    let action_k = deterministic_policy_forward(ddpg.actor, state_k, hidden_act)
    let _ = qnet_continuous_forward(
      ddpg.critic1, state_k, action_k, hidden_q,
    )
    for a in 0..<a_dim {
      let mut g_a_j = 0.0F
      for h in 0..<hidden_dim {
        let gate = if hidden_q[h] > 0.0F { 1.0F } else { 0.0F }
        g_a_j = g_a_j + ddpg.critic1.w1[h][s_dim + a] *
          ddpg.critic1.w2[h] * gate
      }
      let mut s_pre = ddpg.actor.b2[a]
      for h in 0..<hidden_dim {
        s_pre = s_pre + ddpg.actor.w2[a][h] * hidden_act[h]
      }
      let t = Float::from_double(@math.tanh(s_pre.to_double()))
      let one_minus_t2 = 1.0F - t * t
      for h in 0..<hidden_dim {
        let grad_w2 = g_a_j * one_minus_t2 * range * hidden_act[h]
        ddpg.actor.w2[a][h] = ddpg.actor.w2[a][h] + lr_actor * grad_w2
      }
      ddpg.actor.b2[a] = ddpg.actor.b2[a] + lr_actor *
        g_a_j * one_minus_t2 * range
    }
  }
}
