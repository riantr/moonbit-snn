// beam_search_planner.mbt — Beam search planner for Trajectory
// Transformer (v0.81.0).
//
// The TT itself does NOT directly predict actions (it predicts
// next_state, reward, done, value at each step). So to plan a
// trajectory we maintain K candidate "beams" — each is a partial
// trajectory of (state, action, reward, done) — and at each planning
// step expand them by:
//   1. Forward the TT to predict (next_state, reward, done, value) for
//      the current beam tail.
//   2. For each beam, sample K action candidates (uniform random in
//      [action_low, action_high] for now — could be replaced by a
//      policy proposal later).
//   3. Append (predicted_next_state, sampled_action, predicted_reward,
//      predicted_done) to form K new candidate trajectories.
//   4. Score each new candidate by the predicted value + accumulated
//      discounted reward.
//   5. Keep top-K.
//
// After `horizon` planning steps, return the first action of the
// highest-scoring beam (the action to execute in the environment now).
//
// Reference: Janner et al. 2021 Section 4 "Planning as Conditional
// Sequence Modeling" — beam search with stochastic action proposals.
//
// Scope of v0.81.0:
//   - BeamSearchPlanner struct (wraps a TrajectoryTransformer + K +
//     horizon + gamma)
//   - beam_search_plan: full pipeline from history → best first action

///|
/// One beam in the search. Holds a flat row-major Float buffer plus
/// the running discounted-return score. We store (state, action,
/// reward, done) per step up to the current beam length.
pub struct Beam {
  state_seq : Array[Float]
  action_seq : Array[Float]
  reward_seq : Array[Float]
  done_seq : Array[Float]
  length : Int
  score : Float
}

///|
/// BeamSearchPlanner config.
pub struct BeamSearchPlanner {
  tt : TrajectoryTransformer
  beam_width : Int
  horizon : Int
  gamma : Float
  action_low : Float
  action_high : Float
}

///|
/// Build a fresh BeamSearchPlanner. `beam_width = K`, `horizon = H`,
/// `gamma` is the discount used in accumulated score.
pub fn BeamSearchPlanner::new(
  tt : TrajectoryTransformer,
  beam_width : Int,
  horizon : Int,
  gamma : Float,
  action_low : Float,
  action_high : Float,
) -> BeamSearchPlanner {
  { tt, beam_width, horizon, gamma, action_low, action_high }
}

///|
/// Initialize K beams from a single (state, action, reward, done)
/// history. Each beam starts with the same history but a separate
/// (independent) score of 0.
fn beam_search_init_beams(
  state_seq : Array[Float],
  action_seq : Array[Float],
  reward_seq : Array[Float],
  done_seq : Array[Float],
  history_len : Int,
  state_dim : Int,
  action_dim : Int,
  beam_width : Int,
) -> Array[Beam] {
  let beams : Array[Beam] = Array::make(beam_width, {
    state_seq: Array::make(history_len * state_dim, 0.0F),
    action_seq: Array::make(history_len * action_dim, 0.0F),
    reward_seq: Array::make(history_len, 0.0F),
    done_seq: Array::make(history_len, 0.0F),
    length: 0,
    score: 0.0F,
  })
  for k in 0..<beam_width {
    let beam : Beam = {
      state_seq: state_seq.copy(),
      action_seq: action_seq.copy(),
      reward_seq: reward_seq.copy(),
      done_seq: done_seq.copy(),
      length: history_len,
      score: 0.0F,
    }
    beams[k] = beam
  }
  beams
}

///|
/// Score a beam by its accumulated discounted reward (no terminal
/// awareness — caller can subtract score if done fires).
fn beam_score(
  reward_seq : Array[Float],
  done_seq : Array[Float],
  length : Int,
  gamma : Float,
) -> Float {
  let mut s = 0.0F
  let mut discount = 1.0F
  let mut terminated = false
  for t in 0..<length {
    if terminated {
      break
    }
    s = s + discount * reward_seq[t]
    discount = discount * gamma
    if done_seq[t] > 0.5F {
      terminated = true
    }
  }
  s
}

///|
/// Pick the top-K beams by score from a list of K*K candidates.
/// Returns a fresh Array[Beam] of length K containing the highest
/// scorers (ties broken by index).
fn beam_search_topk(
  candidates : Array[Beam],
  beam_width : Int,
) -> Array[Beam] {
  // Sort indices by score descending (insertion sort — fine for K*K small).
  let n = candidates.length()
  let mut indices : Array[Int] = Array::make(n, 0)
  for i in 0..<n {
    indices[i] = i
  }
  // Simple O(n²) selection: find max repeatedly.
  let chosen : Array[Beam] = []
  for _k in 0..<beam_width {
    if indices.length() == 0 {
      break
    }
    let mut best_idx = 0
    let mut best_score = candidates[indices[0]].score
    for j in 1..<indices.length() {
      let s = candidates[indices[j]].score
      if s > best_score {
        best_score = s
        best_idx = j
      }
    }
    chosen.push(candidates[indices[best_idx]])
    // Remove the chosen index.
    let new_indices : Array[Int] = []
    for j in 0..<indices.length() {
      if j != best_idx {
        new_indices.push(indices[j])
      }
    }
    indices = new_indices
  }
  chosen
}

///|
/// Append one step (state, action, reward, done) to a beam's buffers.
fn beam_append_step(
  beam : Beam,
  state_t : Array[Float],
  action_t : Array[Float],
  reward_t : Float,
  done_t : Float,
) -> Beam {
  let new_state : Array[Float] = Array::make(
    (beam.length + 1) * state_t.length(), 0.0F,
  )
  for i in 0..<beam.state_seq.length() {
    new_state[i] = beam.state_seq[i]
  }
  let st_off = beam.length * state_t.length()
  for k in 0..<state_t.length() {
    new_state[st_off + k] = state_t[k]
  }
  let new_action : Array[Float] = Array::make(
    (beam.length + 1) * action_t.length(), 0.0F,
  )
  for i in 0..<beam.action_seq.length() {
    new_action[i] = beam.action_seq[i]
  }
  let at_off = beam.length * action_t.length()
  for k in 0..<action_t.length() {
    new_action[at_off + k] = action_t[k]
  }
  let new_reward : Array[Float] = Array::make(beam.length + 1, 0.0F)
  for i in 0..<beam.reward_seq.length() {
    new_reward[i] = beam.reward_seq[i]
  }
  new_reward[beam.length] = reward_t
  let new_done : Array[Float] = Array::make(beam.length + 1, 0.0F)
  for i in 0..<beam.done_seq.length() {
    new_done[i] = beam.done_seq[i]
  }
  new_done[beam.length] = done_t
  {
    state_seq: new_state,
    action_seq: new_action,
    reward_seq: new_reward,
    done_seq: new_done,
    length: beam.length + 1,
    score: beam.score,
  }
}

///|
/// Run beam search from a single (state, action, reward, done) history.
/// Returns the first action of the highest-scoring beam (length
/// `tt.action_dim`). All TT predictions are done in-place per beam.
pub fn beam_search_plan(
  planner : BeamSearchPlanner,
  state_seq : Array[Float],
  action_seq : Array[Float],
  reward_seq : Array[Float],
  done_seq : Array[Float],
  history_len : Int,
  rng : Xoshiro,
) -> Array[Float] {
  let tt = planner.tt
  let state_dim = tt.state_dim
  let action_dim = tt.action_dim
  // Initial beams.
  let mut beams = beam_search_init_beams(
    state_seq, action_seq, reward_seq, done_seq, history_len,
    state_dim, action_dim, planner.beam_width,
  )
  // Timesteps: simple 0..length mapping (re-built each step).
  let timesteps_buf : Array[Int] = Array::make(planner.horizon + history_len, 0)
  for t in 0..<(planner.horizon + history_len) {
    timesteps_buf[t] = t
  }
  // Expand H steps.
  for _h in 0..<planner.horizon {
    let candidates : Array[Beam] = []
    for k in 0..<beams.length() {
      let beam = beams[k]
      // Predict next (s', r', done, value) at this beam's tail.
      let (next_state, r_pred, d_pred, v_pred) = tt_predict_next(
        tt,
        beam.state_seq,
        beam.action_seq,
        beam.reward_seq,
        beam.done_seq,
        timesteps_buf,
        beam.length,
      )
      // Score this beam by accumulated discounted reward + value
      //   (the value contributes as future discounted reward).
      let beam_score_now = beam_score(
        beam.reward_seq, beam.done_seq, beam.length, planner.gamma,
      )
      let base_score = beam_score_now +
        planner.gamma_power(beam.length) * v_pred
      // Sample K actions per beam.
      for _a in 0..<planner.beam_width {
        let action_t : Array[Float] = Array::make(action_dim, 0.0F)
        for i in 0..<action_dim {
          // Uniform random in [action_low, action_high] (could be replaced
          // by a policy proposal later).
          let u = next_f32(rng)
          action_t[i] = planner.action_low +
            u * (planner.action_high - planner.action_low)
        }
        // Append (next_state, action_t, r_pred, d_pred).
        let mut new_beam = beam_append_step(
          beam, next_state, action_t, r_pred, d_pred,
        )
        // Add the immediate reward to the score.
        new_beam = {
          ..new_beam,
          score: base_score + planner.gamma_power(beam.length) * r_pred,
        }
        candidates.push(new_beam)
      }
    }
    // Pick top-K.
    beams = beam_search_topk(candidates, planner.beam_width)
  }
  // Return the first action of the highest-scoring beam.
  if beams.length() == 0 {
    let empty : Array[Float] = Array::make(action_dim, 0.0F)
    return empty
  }
  let mut best = beams[0]
  for k in 1..<beams.length() {
    if beams[k].score > best.score {
      best = beams[k]
    }
  }
  let first_action : Array[Float] = Array::make(action_dim, 0.0F)
  for i in 0..<action_dim {
    first_action[i] = best.action_seq[i]
  }
  first_action
}

/// Helper: γ^n as Float.
fn BeamSearchPlanner::gamma_power(self : BeamSearchPlanner, n : Int) -> Float {
  let mut p = 1.0F
  for _i in 0..<n {
    p = p * self.gamma
  }
  p
}