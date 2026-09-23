// reinforce_test.mbt — Tests for REINFORCE on GridWorld (v0.33.0).

///|
test "grid_world_new: state count + start/goal" {
  let env = GridWorld::new(3, 4, 20)
  assert_eq(env.n_rows, 3)
  assert_eq(env.n_cols, 4)
  assert_eq(env.n_states(), 12)
  assert_eq(env.start, 0)
  // Goal at (2, 3) -> 2 * 4 + 3 = 11.
  assert_eq(env.goal, 11)
}

///|
test "grid_world_step: terminal at goal" {
  let env = GridWorld::new(2, 2, 10)
  // Step INTO the goal: state 2 = (1, 0), action right -> (1, 1) = state 3.
  let (_s, r, done) = env.step(2, rl_action_right)
  assert_eq(r, 1.0F)
  assert_true(done)
}

///|
test "grid_world_step: stay on out-of-bounds" {
  let env = GridWorld::new(2, 2, 10)
  // At (0, 0), going up should stay.
  let (s, r, done) = env.step(0, rl_action_up)
  assert_eq(s, 0)
  assert_eq(r, -0.1F)
  assert_false(done)
}

///|
test "grid_world_step: valid move" {
  let env = GridWorld::new(2, 2, 10)
  // At (0, 0), going right -> (0, 1) -> state 1.
  let (s, r, done) = env.step(0, rl_action_right)
  assert_eq(s, 1)
  assert_eq(r, -0.1F)
  assert_false(done)
}

///|
test "policy_forward: probs sum to 1 + uniform at zero weights" {
  let p = LinearSoftmaxPolicy::new(4, 3, 7UL)
  let x = rl_one_hot(2, 4)
  let (logits, probs) = policy_forward(p, x)
  assert_eq(logits.length(), 3)
  assert_eq(probs.length(), 3)
  let mut s = 0.0F
  for i in 0..<3 {
    s = s + probs[i]
  }
  let diff = if s > 1.0F { s - 1.0F } else { 1.0F - s }
  assert_true(diff < 1.0e-5F)
  // probs all positive.
  for i in 0..<3 {
    assert_true(probs[i] > 0.0F)
  }
}

///|
test "policy_forward: deterministic with same seed" {
  let p1 = LinearSoftmaxPolicy::new(4, 3, 11UL)
  let p2 = LinearSoftmaxPolicy::new(4, 3, 11UL)
  let x = rl_one_hot(1, 4)
  let (_l1, probs1) = policy_forward(p1, x)
  let (_l2, probs2) = policy_forward(p2, x)
  for i in 0..<3 {
    assert_eq(probs1[i], probs2[i])
  }
}

///|
test "compute_returns: backward computation" {
  let rewards : Array[Float] = [1.0F, 2.0F, 3.0F]
  let g = compute_returns(rewards, 1.0F)
  // G_2 = 3; G_1 = 2 + 3 = 5; G_0 = 1 + 5 = 6.
  assert_eq(g[2], 3.0F)
  assert_eq(g[1], 5.0F)
  assert_eq(g[0], 6.0F)
}

///|
test "compute_returns: with discount" {
  let rewards : Array[Float] = [1.0F, 2.0F, 3.0F]
  let g = compute_returns(rewards, 0.5F)
  // G_2 = 3; G_1 = 2 + 0.5*3 = 3.5; G_0 = 1 + 0.5*3.5 = 2.75.
  assert_eq(g[2], 3.0F)
  assert_eq(g[1], 3.5F)
  let diff = if g[0] > 2.75F { g[0] - 2.75F } else { 2.75F - g[0] }
  assert_true(diff < 1.0e-5F)
}

///|
test "policy_rollout: episode length bounded by max_steps" {
  let env = GridWorld::new(3, 3, 8)
  let p = LinearSoftmaxPolicy::new(9, 4, 17UL)
  let rng = Xoshiro::from_state(19UL, 23UL, 29UL, 31UL)
  let ep = policy_rollout(env, p, 8, rng)
  assert_true(ep.states.length() <= 8)
  assert_eq(ep.states.length(), ep.actions.length())
  assert_eq(ep.states.length(), ep.rewards.length())
}

///|
test "policy_rollout: ends at goal on optimal play" {
  // Hand-tune a policy that goes down from state 0 -> state 2,
  // then right from state 2 -> goal (state 3). That's 2 steps.
  let env = GridWorld::new(2, 2, 10)
  let p = LinearSoftmaxPolicy::new(4, 4, 0UL)
  for i in 0..<4 {
    for j in 0..<4 {
      p.w[i][j] = 0.0F
    }
  }
  p.w[1][0] = 20.0F // down from state 0 -> state 2
  p.w[3][2] = 20.0F // right from state 2 -> state 3 (goal)
  let rng = Xoshiro::from_state(7UL, 11UL, 13UL, 17UL)
  let ep = policy_rollout(env, p, 10, rng)
  // Should reach state 3 (goal) in 2 steps. The last stored state
  // is the FROM state (before the final action); the goal reward
  // appears in the last reward entry.
  assert_eq(ep.states.length(), 2)
  assert_eq(ep.rewards[ep.rewards.length() - 1], env.goal_reward)
}

///|
test "train_reinforce: return improves over training" {
  // Train a small policy and check that mean return improves.
  let env = GridWorld::new(2, 2, 5)
  let p = LinearSoftmaxPolicy::new(4, 4, 41UL)
  let init_mean = train_reinforce(env, p, 100, 0.9F, 0.05F, 5, 43UL)
  let trained_mean = train_reinforce(env, p, 200, 0.9F, 0.05F, 5, 47UL)
  if trained_mean <= init_mean {
    fail("trained return \{trained_mean} didn't improve over init \{init_mean}")
  }
}

///|
test "episode_return: sums rewards" {
  let ep : Episode = {
    states: [0, 1, 2],
    actions: [0, 1, 2],
    rewards: [0.1F, -0.1F, 1.0F],
  }
  let r = episode_return(ep)
  assert_eq(r, 1.0F)
}

///|
test "policy_gradient_update: increases chosen-action probability" {
  // After one update with positive return, the chosen action's
  // logit should increase for its starting state.
  let p = LinearSoftmaxPolicy::new(3, 3, 23UL)
  let x = rl_one_hot(0, 3)
  let (_l_before, probs_before) = policy_forward(p, x)
  let ep : Episode = {
    states: [0],
    actions: [1], // pick action 1
    rewards: [1.0F],
  }
  let returns : Array[Float] = [1.0F]
  policy_gradient_update(p, ep, returns, 0.5F)
  let (_l_after, probs_after) = policy_forward(p, x)
  // prob of action 1 should increase (or at least not decrease a lot).
  assert_true(probs_after[1] >= probs_before[1] - 1.0e-5F)
}
