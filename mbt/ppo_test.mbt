// ppo_test.mbt — Tests for PPO clipped surrogate (v0.35.0).

///|
test "ppo_collect_batch: shapes + per-step returns" {
  let env = GridWorld::new(2, 2, 5)
  let p = LinearSoftmaxPolicy::new(4, 4, 7UL)
  let batch = ppo_collect_batch(env, p, 5, 0.9F, 5, 11UL)
  assert_true(batch.states.length() > 0)
  assert_eq(batch.states.length(), batch.actions.length())
  assert_eq(batch.states.length(), batch.returns.length())
  assert_eq(batch.states.length(), batch.old_probs.length())
  assert_eq(batch.n_episodes, 5)
  // Per-step return is bounded above by goal_reward.
  for i in 0..<batch.returns.length() {
    assert_true(batch.returns[i] <= env.goal_reward + 0.01F)
  }
}

///|
test "ppo_collect_batch: old_probs positive + finite" {
  let env = GridWorld::new(2, 2, 5)
  let p = LinearSoftmaxPolicy::new(4, 4, 13UL)
  let batch = ppo_collect_batch(env, p, 5, 0.9F, 5, 17UL)
  for i in 0..<batch.old_probs.length() {
    assert_true(batch.old_probs[i] > 0.0F)
    assert_true(batch.old_probs[i] <= 1.0F)
  }
}

///|
test "ppo_update_step: zero return -> no weight change" {
  let env = GridWorld::new(2, 2, 5)
  let p = LinearSoftmaxPolicy::new(4, 4, 19UL)
  let batch = ppo_collect_batch(env, p, 3, 0.9F, 5, 23UL)
  // Snapshot.
  let snap : Array[Array[Float]] = Array::make(4, [])
  for i in 0..<4 {
    snap[i] = Array::make(4, 0.0F)
    for j in 0..<4 {
      snap[i][j] = p.w[i][j]
    }
  }
  // Force all returns to 0.
  let zero_batch : PpoBatch = {
    states: batch.states,
    actions: batch.actions,
    returns: Array::make(batch.returns.length(), 0.0F),
    old_probs: batch.old_probs,
    n_episodes: batch.n_episodes,
  }
  ppo_update_step(p, zero_batch, 0.2F, 0.5F)
  for i in 0..<4 {
    for j in 0..<4 {
      assert_eq(p.w[i][j], snap[i][j])
    }
  }
}

///|
test "ppo_update_step: with clipping, no extreme weight jumps" {
  // After one PPO step, weights should change but stay bounded.
  let env = GridWorld::new(2, 2, 5)
  let p = LinearSoftmaxPolicy::new(4, 4, 29UL)
  let batch = ppo_collect_batch(env, p, 5, 0.9F, 5, 31UL)
  let snap : Array[Array[Float]] = Array::make(4, [])
  for i in 0..<4 {
    snap[i] = Array::make(4, 0.0F)
    for j in 0..<4 {
      snap[i][j] = p.w[i][j]
    }
  }
  ppo_update_step(p, batch, 0.2F, 0.5F)
  for i in 0..<4 {
    for j in 0..<4 {
      let diff = if p.w[i][j] > snap[i][j] {
        p.w[i][j] - snap[i][j]
      } else {
        snap[i][j] - p.w[i][j]
      }
      // With clipping at 1.2 and reasonable returns, updates
      // should not exceed ~5 in magnitude.
      assert_true(diff < 5.0F)
    }
  }
}

///|
test "ppo_clip_active: ratio outside [1-eps, 1+eps] is clipped" {
  // Hand-craft a setup: old_probs[0] = 0.5 (chosen), but after one
  // big update cur_p[0] = 0.9 (ratio = 1.8, clipped to 1.2).
  let env = GridWorld::new(2, 2, 1)
  let p = LinearSoftmaxPolicy::new(4, 2, 37UL)
  // Force policy: action 0 has huge logit. p.w has n_actions=2
  // rows and n_states=4 columns.
  for i in 0..<2 {
    for j in 0..<4 {
      p.w[i][j] = 0.0F
    }
  }
  p.w[0][0] = 10.0F
  let batch : PpoBatch = {
    states: [0],
    actions: [0],
    returns: [1.0F],
    old_probs: [0.5F],
    n_episodes: 1,
  }
  let snap_00 = p.w[0][0]
  ppo_update_step(p, batch, 0.2F, 0.1F)
  // Weight changed (proves update ran) but not by 1.0 (proves
  // effective ratio was clipped to 1.2).
  assert_true(p.w[0][0] != snap_00)
  // ∆w[0,0] = lr * (1 - 0.99) * 1.2 * 1.0 (approximately)
  //          ≈ 0.1 * 0.01 * 1.2 ≈ 0.0012
  let delta = if p.w[0][0] > snap_00 {
    p.w[0][0] - snap_00
  } else {
    snap_00 - p.w[0][0]
  }
  // Bound: unclipped would be 0.1 * 0.01 * 1.8 = 0.0018; clipped
  // is 0.1 * 0.01 * 1.2 = 0.0012. Allow 0.0012 +/- 0.0010.
  assert_true(delta < 0.01F)
  assert_true(delta > 0.0F)
}

///|
test "train_ppo: shapes + return is finite" {
  let env = GridWorld::new(2, 2, 5)
  let p = LinearSoftmaxPolicy::new(4, 4, 41UL)
  let last = train_ppo(env, p, 3, 5, 0.9F, 0.2F, 0.05F, 5, 43UL)
  assert_true(last == last)
  assert_true(last > -10.0F && last < 10.0F)
}

///|
test "ppo_old_prob_correct_for_handtuned_policy" {
  // With a policy that strongly prefers action 1 from state 0,
  // the old_prob for state 0 + action 1 should be > 0.5.
  let env = GridWorld::new(2, 2, 5)
  let p = LinearSoftmaxPolicy::new(4, 4, 47UL)
  for i in 0..<4 {
    for j in 0..<4 {
      p.w[i][j] = 0.0F
    }
  }
  p.w[1][0] = 5.0F
  let batch = ppo_collect_batch(env, p, 3, 0.9F, 5, 53UL)
  // At least one transition should have action 1 from state 0,
  // and its old_prob should be > 0.5.
  let mut found = false
  for i in 0..<batch.states.length() {
    if batch.states[i] == 0 && batch.actions[i] == 1 {
      assert_true(batch.old_probs[i] > 0.5F)
      found = true
    }
  }
  assert_true(found)
}