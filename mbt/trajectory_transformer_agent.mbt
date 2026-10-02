// trajectory_transformer_agent.mbt — Trajectory Transformer agent:
// full planning pipeline (history → beam search → best action) (v0.83.0).
//
// Wires:
//   - TrajectoryTransformer (v0.80.0) for the model
//   - BeamSearchPlanner (v0.81.0) for the planning algorithm
//
// The agent maintains a sliding-window history of (state, action,
// reward, done). On each environment step, it appends the new
// transition, runs beam search over the history, and returns the
// first action of the highest-scoring planned trajectory.
//
// Scope of v0.83.0:
//   - TrajectoryTransformerAgent struct + constructor
//   - tt_agent_step: append transition + run beam search + return action
//   - tt_agent_reset: clear history (called at episode boundary)
//
// Reference: Janner et al. 2021, Section 4 (the 'TT pipeline' for
// model-based control).

///|
/// Trajectory Transformer agent: model + planner + sliding history.
pub(all) struct TrajectoryTransformerAgent {
  tt : TrajectoryTransformer
  planner : BeamSearchPlanner
  context_length : Int
  action_dim : Int
  state_dim : Int
  // Sliding window history (state, action, reward, done). When the
  // history exceeds `context_length`, the oldest entry is dropped.
  mut history_states : Array[Float]
  mut history_actions : Array[Float]
  mut history_rewards : Array[Float]
  mut history_dones : Array[Float]
  mut history_len : Int
}

///|
/// Build a fresh TrajectoryTransformerAgent. `context_length` is the
/// number of past steps fed into the planner at inference time
/// (the planner sees this many transitions before planning forward).
pub fn TrajectoryTransformerAgent::new(
  tt : TrajectoryTransformer,
  planner : BeamSearchPlanner,
  context_length : Int,
) -> TrajectoryTransformerAgent {
  let state_dim = tt.state_dim
  let action_dim = tt.action_dim
  {
    tt,
    planner,
    context_length,
    action_dim,
    state_dim,
    history_states: Array::make(context_length * state_dim, 0.0F),
    history_actions: Array::make(context_length * action_dim, 0.0F),
    history_rewards: Array::make(context_length, 0.0F),
    history_dones: Array::make(context_length, 0.0F),
    history_len: 0,
  }
}

///|
/// Append one transition (state, action, reward, done) to the sliding
/// history. If history is full, the oldest entry is dropped. The
/// transition added here is the (s_t, a_{t-1}, r_t, d_t) tuple — i.e.
/// the action taken in the *previous* state, observed reward, and done
/// flag. The caller passes `prev_action` (the action executed before
/// observing `state`).
fn tt_agent_append(
  agent : TrajectoryTransformerAgent,
  state : Array[Float],
  prev_action : Array[Float],
  prev_reward : Float,
  prev_done : Float,
) -> Unit {
  let cl = agent.context_length
  if agent.history_len < cl {
    // Append at the tail.
    let t = agent.history_len
    let st_off = t * agent.state_dim
    let at_off = t * agent.action_dim
    for k in 0..<agent.state_dim {
      agent.history_states[st_off + k] = state[k]
    }
    for k in 0..<agent.action_dim {
      agent.history_actions[at_off + k] = prev_action[k]
    }
    agent.history_rewards[t] = prev_reward
    agent.history_dones[t] = prev_done
    agent.history_len = agent.history_len + 1
  } else {
    // Slide: drop oldest (index 0), shift all earlier entries left.
    let n_states_shift = (cl - 1) * agent.state_dim
    for i in 0..<n_states_shift {
      agent.history_states[i] = agent.history_states[i + agent.state_dim]
    }
    let n_actions_shift = (cl - 1) * agent.action_dim
    for i in 0..<n_actions_shift {
      agent.history_actions[i] = agent.history_actions[i + agent.action_dim]
    }
    for i in 0..<(cl - 1) {
      agent.history_rewards[i] = agent.history_rewards[i + 1]
      agent.history_dones[i] = agent.history_dones[i + 1]
    }
    // Write new entry at the tail.
    let t = cl - 1
    let st_off = t * agent.state_dim
    let at_off = t * agent.action_dim
    for k in 0..<agent.state_dim {
      agent.history_states[st_off + k] = state[k]
    }
    for k in 0..<agent.action_dim {
      agent.history_actions[at_off + k] = prev_action[k]
    }
    agent.history_rewards[t] = prev_reward
    agent.history_dones[t] = prev_done
  }
}

///|
/// Plan + act. Appends the new transition to history, runs beam
/// search over the (now-updated) sliding window, returns the chosen
/// action. `prev_action` is the action executed in the *previous*
/// state (before observing `state`).
pub fn tt_agent_step(
  agent : TrajectoryTransformerAgent,
  state : Array[Float],
  prev_action : Array[Float],
  prev_reward : Float,
  prev_done : Float,
  rng : Xoshiro,
) -> Array[Float] {
  tt_agent_append(agent, state, prev_action, prev_reward, prev_done)
  // Use only the first history_len entries for the planner.
  let cl = agent.context_length
  let state_seq : Array[Float] = Array::make(
    agent.history_len * agent.state_dim, 0.0F,
  )
  let action_seq : Array[Float] = Array::make(
    agent.history_len * agent.action_dim, 0.0F,
  )
  let reward_seq : Array[Float] = Array::make(agent.history_len, 0.0F)
  let done_seq : Array[Float] = Array::make(agent.history_len, 0.0F)
  let n_states_copy = agent.history_len * agent.state_dim
  for i in 0..<n_states_copy {
    state_seq[i] = agent.history_states[i]
  }
  let n_actions_copy = agent.history_len * agent.action_dim
  for i in 0..<n_actions_copy {
    action_seq[i] = agent.history_actions[i]
  }
  for i in 0..<agent.history_len {
    reward_seq[i] = agent.history_rewards[i]
    done_seq[i] = agent.history_dones[i]
  }
  // If history_len < context_length, pad with zeros (already done by
  // Array::make).
  let _ = cl
  beam_search_plan(
    agent.planner,
    state_seq,
    action_seq,
    reward_seq,
    done_seq,
    agent.history_len,
    rng,
  )
}

///|
/// Reset the agent's history (called at episode boundary). Clears
/// history_len to 0 (the buffers themselves stay allocated).
pub fn tt_agent_reset(agent : TrajectoryTransformerAgent) -> Unit {
  agent.history_len = 0
  // Zero out the buffers (optional but keeps state clean).
  for i in 0..<agent.history_states.length() {
    agent.history_states[i] = 0.0F
  }
  for i in 0..<agent.history_actions.length() {
    agent.history_actions[i] = 0.0F
  }
  for i in 0..<agent.history_rewards.length() {
    agent.history_rewards[i] = 0.0F
  }
  for i in 0..<agent.history_dones.length() {
    agent.history_dones[i] = 0.0F
  }
}