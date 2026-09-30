// td3_test.mbt — Unit tests for the TD3 agent (Fujimoto 2018).

///|
test "td3_new_targets_match_online" {
  let td3 = TD3::new(
    3, 2, 8, -1.0F, 1.0F, 0.99F, 0.005F, 0.1F, 0.2F, 0.5F, 2, 42UL,
  )
  let mut h = 0
  while h < td3.actor.hidden {
    if td3.actor.w1[h][0] != td3.actor_target.w1[h][0] {
      fail("actor target w1 not equal at h=\{h}")
    }
    h = h + 1
  }
  let mut hh = 0
  while hh < td3.critic1.hidden {
    if td3.critic1.w1[hh][0] != td3.critic1_target.w1[hh][0] {
      fail("critic1 target w1 not equal at hh=\{hh}")
    }
    hh = hh + 1
  }
}

///|
test "td3_select_action_eval_in_range" {
  let td3 = TD3::new(
    3, 2, 8, -1.0F, 1.0F, 0.99F, 0.005F, 0.0F, 0.2F, 0.5F, 2, 7UL,
  )
  let state : Array[Float] = [0.5F, -0.5F, 0.1F]
  let action = td3_select_action_eval(td3, state)
  assert_eq(action.length(), 2)
  let mut k = 0
  while k < action.length() {
    if action[k] < -1.0F || action[k] > 1.0F {
      fail("eval action \{k} = \{action[k]} out of range")
    }
    k = k + 1
  }
}

///|
test "td3_select_action_with_noise_stays_in_range" {
  let td3 = TD3::new(
    3, 1, 8, -1.0F, 1.0F, 0.99F, 0.005F, 0.5F, 0.2F, 0.5F, 2, 7UL,
  )
  let state : Array[Float] = [0.0F, 0.0F, 0.0F]
  let rng = Xoshiro::from_state(1UL, 2UL, 3UL, 4UL)
  let mut k = 0
  while k < 100 {
    let action = td3_select_action(td3, state, rng)
    let a = action[0]
    if a < -1.0F || a > 1.0F {
      fail("noisy action = \{a} out of range")
    }
    k = k + 1
  }
}

///|
test "td3_policy_delay_skips_actor_update" {
  // With policy_delay=2, the actor should only be updated on every 2nd
  // call to td3_update_step.
  let td3 = TD3::new(
    2, 1, 4, -1.0F, 1.0F, 0.99F, 0.005F, 0.1F, 0.2F, 0.5F, 2, 7UL,
  )
  let buf = ContinuousReplayBuffer::new(8, 2, 1)
  buf.push([0.1F, 0.1F], [0.2F], 0.5F, [0.2F, 0.2F], false)
  buf.push([0.2F, 0.2F], [0.1F], 0.4F, [0.3F, 0.3F], false)
  let rng = Xoshiro::from_state(5UL, 6UL, 7UL, 8UL)
  let w2_before = td3.actor.w2[0][0]
  let b2_before = td3.actor.b2[0]
  // step_count=1: NOT a multiple of policy_delay=2, so actor skip.
  let batch = buf.sample(2, rng)
  let _ = td3_update_step(td3, batch, 0.001F, 0.0001F, 1, rng)
  let w2_after_skip = td3.actor.w2[0][0]
  let b2_after_skip = td3.actor.b2[0]
  if w2_after_skip != w2_before || b2_after_skip != b2_before {
    fail("actor should NOT update on step_count=1 (policy_delay=2)")
  }
  // step_count=2: multiple of 2, actor + targets update.
  let _ = td3_update_step(td3, batch, 0.001F, 0.0001F, 2, rng)
  let w2_after_update = td3.actor.w2[0][0]
  let b2_after_update = td3.actor.b2[0]
  if w2_after_update == w2_before && b2_after_update == b2_before {
    fail("actor should update on step_count=2 (policy_delay=2)")
  }
  let _ = w2_after_skip
}

///|
test "td3_soft_update_moves_target_toward_online" {
  let td3 = TD3::new(
    2, 1, 4, -1.0F, 1.0F, 0.99F, 0.5F, 0.1F, 0.2F, 0.5F, 2, 7UL,
  )
  td3.actor.b2[0] = 10.0F
  let b2_target_before = td3.actor_target.b2[0]
  td3_soft_update(td3)
  let b2_target_after = td3.actor_target.b2[0]
  let expected = b2_target_before * 0.5F + 5.0F
  let diff = if b2_target_after > expected {
    b2_target_after - expected
  } else {
    expected - b2_target_after
  }
  if diff > 1.0e-4F {
    fail("target b2 not moved correctly")
  }
}

///|
test "td3_update_step_runs_finite" {
  let td3 = TD3::new(
    3, 1, 8, -1.0F, 1.0F, 0.99F, 0.005F, 0.1F, 0.2F, 0.5F, 2, 7UL,
  )
  let buf = ContinuousReplayBuffer::new(16, 3, 1)
  let mut i = 0
  while i < 8 {
    buf.push(
      [Float::from_int(i) * 0.1F, 0.1F, 0.1F],
      [0.5F],
      Float::from_int(i) * 0.01F,
      [0.2F, 0.2F, 0.2F],
      false,
    )
    i = i + 1
  }
  let rng = Xoshiro::from_state(1UL, 2UL, 3UL, 4UL)
  let batch = buf.sample(4, rng)
  let td = td3_update_step(td3, batch, 0.001F, 0.0001F, 1, rng)
  if td != td {
    fail("TD error is NaN")
  }
}

///|
test "td3_target_noise_clipped_to_clip_range" {
  let td3 = TD3::new(
    2, 1, 4, -1.0F, 1.0F, 0.99F, 0.005F, 0.1F, 5.0F, 0.05F, 2, 7UL,
  )
  let buf = ContinuousReplayBuffer::new(8, 2, 1)
  let mut i = 0
  while i < 4 {
    buf.push(
      [Float::from_int(i) * 0.1F, 0.1F],
      [0.0F],
      Float::from_int(i) * 0.1F,
      [0.2F, 0.2F],
      false,
    )
    i = i + 1
  }
  let rng = Xoshiro::from_state(99UL, 88UL, 77UL, 66UL)
  let batch = buf.sample(4, rng)
  let _ = td3_update_step(td3, batch, 0.001F, 0.0001F, 1, rng)
  let b1_after = td3.critic1_target.b1[0]
  if b1_after != b1_after {
    fail("target b1 went NaN")
  }
  if b1_after < -100.0F || b1_after > 100.0F {
    fail("target b1 grew out of range: \{b1_after}")
  }
}

///|
test "td3_train_runs_without_crash" {
  // Smoke test: train TD3 for 2 episodes on a trivial LQR-like env.
  // Build a simple env: state = [x], action = [a], x' = 0.95·x + 0.1·a,
  // reward = -|x|. The optimal policy is a = -k·x for some k.
  let env_step = fn(
    state : Array[Float],
    action : Array[Float],
  ) -> (Array[Float], Float, Bool) {
    let x = state[0]
    let a = action[0]
    let x_next_d = 0.95 * x.to_double() + 0.1 * a.to_double()
    let x_next = Float::from_double(x_next_d)
    let next_state : Array[Float] = [x_next]
    let r_d = if x < 0.0F { -x.to_double() } else { x.to_double() }
    let r = Float::from_double(-r_d)
    let done = false
    (next_state, r, done)
  }
  let td3 = TD3::new(
    1, 1, 4, -1.0F, 1.0F, 0.99F, 0.005F, 0.1F, 0.2F, 0.5F, 2, 7UL,
  )
  let avg = train_td3(
    1, 1, env_step, td3, 2, 32, 1, 4, 50, 42UL,
  )
  if avg != avg {
    fail("avg return is NaN")
  }
}
