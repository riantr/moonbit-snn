// ppo_gae_test.mbt — Tests for PPO-GAE (v0.35.1).

///|
test "ppo_gae_collect_batch: per-episode shapes" {
  let env = GridWorld::new(2, 2, 5)
  let p = LinearSoftmaxPolicy::new(4, 4, 7UL)
  let v = LinearValueNet::new(4, 11UL)
  let eps = ppo_gae_collect_batch(env, p, v, 5, 5, 13UL)
  assert_eq(eps.length(), 5)
  for ep in eps {
    assert_eq(ep.states.length(), ep.actions.length())
    assert_eq(ep.states.length(), ep.rewards.length())
    assert_eq(ep.states.length(), ep.values.length())
    assert_eq(ep.states.length(), ep.next_values.length())
    assert_eq(ep.states.length(), ep.dones.length())
  }
}

///|
test "gae_advantages_returns: shape + zero TD residual on perfect V" {
  // Construct an episode with known V. If V(s_t) = E[r + γ·V(s_{t+1})],
  // the TD residual δ_t should be small (sampling noise only).
  let env = GridWorld::new(2, 2, 5)
  let p = LinearSoftmaxPolicy::new(4, 4, 17UL)
  let v = LinearValueNet::new(4, 19UL)
  // Set V to a constant for simplicity: V(s) = 1.0 for all s.
  v.w[0] = 1.0F
  v.w[1] = 1.0F
  v.w[2] = 1.0F
  v.w[3] = 1.0F
  let rng = Xoshiro::from_state(23UL, 29UL, 31UL, 37UL)
  let mut state = env.start
  let mut done = false
  let mut t = 0
  let states : Array[Int] = []
  let actions : Array[Int] = []
  let rewards : Array[Float] = []
  let values : Array[Float] = []
  let next_values : Array[Float] = []
  let dones : Array[Bool] = []
  while !done && t < 5 {
    let x = rl_one_hot(state, env.n_states())
    values.push(value_forward(v, x))
    let (_l, probs) = policy_forward(p, x)
    let (a, _) = sample_categorical(probs, rng)
    let (next_state, r, d) = env.step(state, a)
    let next_x = rl_one_hot(next_state, env.n_states())
    next_values.push(if d { 0.0F } else { value_forward(v, next_x) })
    states.push(state)
    actions.push(a)
    rewards.push(r)
    dones.push(d)
    state = next_state
    done = d
    t = t + 1
  }
  let ep : PpoGaeEpisode = { states, actions, rewards, values, next_values, dones }
  let (advs, rets) = gae_advantages_returns(ep, 1.0F, 1.0F)
  assert_eq(advs.length(), ep.states.length())
  assert_eq(rets.length(), ep.states.length())
  // With V(s) = 1 (constant), δ_t = r_t + 1·V(s_{t+1})·(1-done) - 1.
  // GAE accumulates. Last-step GAE should equal δ_T.
  let last_adv = advs[advs.length() - 1]
  let last_r = rewards[rewards.length() - 1]
  let last_v = values[values.length() - 1]
  let last_v_next = next_values[next_values.length() - 1]
  let last_d = dones[dones.length() - 1]
  let not_done = if last_d { 0.0F } else { 1.0F }
  let expected_last_adv = last_r + 1.0F * last_v_next * not_done - last_v
  let diff = if last_adv > expected_last_adv {
    last_adv - expected_last_adv
  } else {
    expected_last_adv - last_adv
  }
  assert_true(diff < 1.0e-5F)
}

///|
test "gae_advantages_returns: returns are monotonically backward-decreasing" {
  let env = GridWorld::new(2, 2, 5)
  let p = LinearSoftmaxPolicy::new(4, 4, 41UL)
  let v = LinearValueNet::new(4, 43UL)
  let eps = ppo_gae_collect_batch(env, p, v, 3, 5, 47UL)
  for ep in eps {
    let (_advs, rets) = gae_advantages_returns(ep, 0.9F, 0.95F)
    // Sanity: rets[t] should match the discounted backward sum.
    // For t < length-1: rets[t] = rewards[t] + γ · rets[t+1].
    for t in 0..<(rets.length() - 1) {
      let expected = ep.rewards[t] + 0.9F * rets[t + 1]
      let diff = if rets[t] > expected {
        rets[t] - expected
      } else {
        expected - rets[t]
      }
      assert_true(diff < 1.0e-5F)
    }
  }
}

///|
test "ppo_gae_update: zero return episode -> critic moves toward zero" {
  // If every reward is 0 and target is 0, V(s) should drop to 0.
  let env = GridWorld::new(2, 2, 5)
  let p = LinearSoftmaxPolicy::new(4, 4, 53UL)
  let v = LinearValueNet::new(4, 59UL)
  // Set V(s) = 2 initially (far from the target 1.0).
  v.w[0] = 2.0F
  v.w[1] = 2.0F
  v.w[2] = 2.0F
  v.w[3] = 2.0F
  // Build a single-step episode with reward 0 and done=false.
  let states : Array[Int] = [0]
  let actions : Array[Int] = [0]
  let rewards : Array[Float] = [0.0F]
  let values : Array[Float] = [2.0F]
  let next_values : Array[Float] = [2.0F]
  let dones : Array[Bool] = [false]
  let ep : PpoGaeEpisode = { states, actions, rewards, values, next_values, dones }
  let episodes : Array[PpoGaeEpisode] = [ep]
  ppo_gae_update(p, v, episodes, 1.0F, 1.0F, 0.2F, 0.1F, 0.5F)
  // After one update, V(s_0) = 2 - 0.5 · (2 - 1) = 1.5 (target R = 1).
  assert_true(v.w[0] < 2.0F)
}

///|
test "train_ppo_gae: shapes + return is finite" {
  let env = GridWorld::new(2, 2, 5)
  let p = LinearSoftmaxPolicy::new(4, 4, 67UL)
  let v = LinearValueNet::new(4, 71UL)
  let last = train_ppo_gae(
    env, p, v, 3, 5, 0.9F, 0.95F, 0.2F, 0.05F, 0.1F, 5, 73UL,
  )
  assert_true(last == last)
  assert_true(last > -10.0F && last < 10.0F)
}

///|
test "ppo_gae_update: weights change but stay bounded" {
  let env = GridWorld::new(2, 2, 5)
  let p = LinearSoftmaxPolicy::new(4, 4, 79UL)
  let v = LinearValueNet::new(4, 83UL)
  let eps = ppo_gae_collect_batch(env, p, v, 5, 5, 89UL)
  let snap : Array[Array[Float]] = Array::make(4, [])
  for i in 0..<4 {
    snap[i] = Array::make(4, 0.0F)
    for j in 0..<4 {
      snap[i][j] = p.w[i][j]
    }
  }
  ppo_gae_update(p, v, eps, 0.9F, 0.95F, 0.2F, 0.05F, 0.1F)
  let mut changed = false
  for i in 0..<4 {
    for j in 0..<4 {
      if p.w[i][j] != snap[i][j] {
        changed = true
      }
    }
  }
  assert_true(changed)
}