// lstm_policy_test.mbt — Tests for LSTM-policy REINFORCE (v0.33.2).

///|
test "corridor_env_new: shape + goal on right" {
  let env = CorridorEnv::new(5, 10)
  assert_eq(env.n_cells, 5)
  assert_eq(env.goal_side, 1)
  assert_eq(env.goal_cell(), 4)
}

///|
test "corridor_observation: indicator + position one-hot" {
  let env = CorridorEnv::new(4, 10)
  let obs = corridor_observation(env, 2)
  assert_eq(obs.length(), 6) // 2 + 4
  // Indicator: goal_side=1 -> obs[1]=1.
  assert_eq(obs[0], 0.0F)
  assert_eq(obs[1], 1.0F)
  // Position 2 -> obs[2+2] = obs[4] = 1.
  for i in 0..<4 {
    if i == 2 {
      assert_eq(obs[2 + i], 1.0F)
    } else {
      assert_eq(obs[2 + i], 0.0F)
    }
  }
}

///|
test "corridor_observation: indicator flips with goal_side" {
  let env_l : CorridorEnv = CorridorEnv::{ n_cells: 4, goal_side: 0, step_penalty: -0.1F, goal_reward: 1.0F, max_steps: 10 }
  let env_r : CorridorEnv = CorridorEnv::{ n_cells: 4, goal_side: 1, step_penalty: -0.1F, goal_reward: 1.0F, max_steps: 10 }
  let obs_l = corridor_observation(env_l, 0)
  let obs_r = corridor_observation(env_r, 0)
  assert_eq(obs_l[0], 1.0F)
  assert_eq(obs_l[1], 0.0F)
  assert_eq(obs_r[0], 0.0F)
  assert_eq(obs_r[1], 1.0F)
}

///|
test "corridor_step: terminal at goal" {
  let env = CorridorEnv::new(3, 5)
  // Goal at cell 2 (right). From cell 1, action=right -> cell 2 (goal).
  let (_s, r, done) = env.step(1, 1)
  assert_eq(r, 1.0F)
  assert_true(done)
}

///|
test "corridor_step: stay on out-of-bounds" {
  let env = CorridorEnv::new(3, 5)
  // At cell 0 (left edge), action=left -> stay.
  let (s, r, done) = env.step(0, 0)
  assert_eq(s, 0)
  assert_eq(r, -0.1F)
  assert_false(done)
}

///|
test "lstm_policy_new: shapes" {
  let p = LstmPolicy::new(3, 4, 11UL)
  assert_eq(p.cell.d_x, 5) // 2 + 3
  assert_eq(p.cell.d_h, 4)
  assert_eq(p.w_out.length(), 2)
  for i in 0..<2 {
    assert_eq(p.w_out[i].length(), 4)
  }
  assert_eq(p.b_out.length(), 2)
}

///|
test "lstm_policy_forward: action + cache lengths match obs_seq" {
  let p = LstmPolicy::new(3, 4, 13UL)
  let obs_seq : Array[Array[Float]] = [
    [1.0F, 0.0F, 1.0F, 0.0F, 0.0F],
    [1.0F, 0.0F, 0.0F, 1.0F, 0.0F],
    [1.0F, 0.0F, 0.0F, 0.0F, 1.0F],
  ]
  let h0 : Array[Float] = [0.0F, 0.0F, 0.0F, 0.0F]
  let c0 : Array[Float] = [0.0F, 0.0F, 0.0F, 0.0F]
  let rng = Xoshiro::from_state(17UL, 19UL, 23UL, 29UL)
  let (actions, cache) = lstm_policy_forward(p, obs_seq, h0, c0, rng)
  assert_eq(actions.length(), 3)
  for t in 0..<3 {
    assert_true(actions[t] == 0 || actions[t] == 1)
    assert_eq(cache.cell_caches[t].h_t.length(), 4)
    assert_eq(cache.probs[t].length(), 2)
    assert_eq(cache.hs[t].length(), 4)
  }
  // probs sum to 1.
  for t in 0..<3 {
    let s = cache.probs[t][0] + cache.probs[t][1]
    let diff = if s > 1.0F { s - 1.0F } else { 1.0F - s }
    assert_true(diff < 1.0e-4F)
  }
}

///|
test "lstm_policy_gradient_update: zero returns -> no change" {
  let p = LstmPolicy::new(3, 3, 31UL)
  let obs_seq : Array[Array[Float]] = [
    [1.0F, 0.0F, 1.0F, 0.0F, 0.0F],
    [1.0F, 0.0F, 0.0F, 1.0F, 0.0F],
  ]
  let h0 : Array[Float] = [0.0F, 0.0F, 0.0F]
  let c0 : Array[Float] = [0.0F, 0.0F, 0.0F]
  let rng = Xoshiro::from_state(37UL, 41UL, 43UL, 47UL)
  let (actions, cache) = lstm_policy_forward(p, obs_seq, h0, c0, rng)
  // Snapshot weights before update.
  let w0_snapshot : Array[Array[Float]] = Array::make(2, [])
  for i in 0..<2 {
    w0_snapshot[i] = Array::make(3, 0.0F)
    for j in 0..<3 {
      w0_snapshot[i][j] = p.w_out[i][j]
    }
  }
  let cell_w_snapshot : Array[Array[Float]] = Array::make(3, [])
  for i in 0..<3 {
    cell_w_snapshot[i] = Array::make(5, 0.0F)
    for j in 0..<5 {
      cell_w_snapshot[i][j] = p.cell.w_f[i][j]
    }
  }
  // Zero returns.
  let returns : Array[Float] = [0.0F, 0.0F]
  lstm_policy_gradient_update(p, actions, returns, cache, 0.1F)
  // Weights unchanged (returns are all zero -> early continue).
  for i in 0..<2 {
    for j in 0..<3 {
      assert_eq(p.w_out[i][j], w0_snapshot[i][j])
    }
  }
  for i in 0..<3 {
    for j in 0..<5 {
      assert_eq(p.cell.w_f[i][j], cell_w_snapshot[i][j])
    }
  }
}

///|
test "lstm_policy_gradient_update: positive return increases chosen prob" {
  let p = LstmPolicy::new(3, 3, 53UL)
  let obs_seq : Array[Array[Float]] = [
    [1.0F, 0.0F, 1.0F, 0.0F, 0.0F],
    [1.0F, 0.0F, 0.0F, 1.0F, 0.0F],
    [1.0F, 0.0F, 0.0F, 0.0F, 1.0F],
  ]
  let h0 : Array[Float] = [0.0F, 0.0F, 0.0F]
  let c0 : Array[Float] = [0.0F, 0.0F, 0.0F]
  let rng = Xoshiro::from_state(59UL, 61UL, 67UL, 71UL)
  let (actions, cache) = lstm_policy_forward(p, obs_seq, h0, c0, rng)
  // Set all returns positive.
  let returns : Array[Float] = [1.0F, 1.0F, 1.0F]
  lstm_policy_gradient_update(p, actions, returns, cache, 0.5F)
  // After update, probabilities for the chosen actions should have
  // increased or stayed the same (REINFORCE with positive return).
  let rng2 = Xoshiro::from_state(73UL, 79UL, 83UL, 89UL)
  let (_a2, cache2) = lstm_policy_forward(p, obs_seq, h0, c0, rng2)
  for t in 0..<3 {
    let a = actions[t]
    // cache2 (after update) prob of chosen action >= cache (before).
    assert_true(cache2.probs[t][a] >= cache.probs[t][a] - 1.0e-3F)
  }
}

///|
test "train_lstm_reinforce: shapes + return is finite" {
  let p = LstmPolicy::new(5, 8, 97UL)
  let mean_return = train_lstm_reinforce(5, p, 5, 0.9F, 0.01F, 4, 101UL)
  // Just verify it's a finite Float (not NaN or out of range).
  assert_true(mean_return == mean_return) // NaN check
  assert_true(mean_return > -100.0F && mean_return < 100.0F)
}

///|
test "lstm_episode_return: sums rewards" {
  let r : Array[Float] = [0.1F, -0.1F, 1.0F, -0.1F]
  let s = lstm_episode_return(r)
  assert_eq(s, 0.9F)
}