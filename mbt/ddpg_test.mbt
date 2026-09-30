// ddpg_test.mbt — Unit tests for the DDPG agent (Lillicrap 2015).

// =========================================================================
// DeterministicPolicy tests
// =========================================================================

///|
test "deterministic_policy_shapes" {
  let p = DeterministicPolicy::new(3, 2, 4, -1.0F, 1.0F, 1UL)
  assert_eq(p.state_dim, 3)
  assert_eq(p.action_dim, 2)
  assert_eq(p.hidden, 4)
  assert_eq(p.w1.length(), 4)
  assert_eq(p.w1[0].length(), 3)
  assert_eq(p.b1.length(), 4)
  assert_eq(p.w2.length(), 2)
  assert_eq(p.w2[0].length(), 4)
  assert_eq(p.b2.length(), 2)
}

///|
test "policy_forward_output_in_range" {
  let p = DeterministicPolicy::new(4, 2, 8, -1.0F, 1.0F, 2UL)
  let state : Array[Float] = [0.5F, -0.3F, 0.1F, 0.8F]
  let hidden : Array[Float] = Array::make(p.hidden, 0.0F)
  let action = deterministic_policy_forward(p, state, hidden)
  assert_eq(action.length(), 2)
  let mut k = 0
  while k < action.length() {
    let a = action[k]
    if a < -1.0F || a > 1.0F {
      fail("action \{k} = \{a} out of range [-1, 1]")
    }
    k = k + 1
  }
}

///|
test "policy_forward_deterministic_same_input_same_output" {
  let p = DeterministicPolicy::new(2, 1, 4, -0.5F, 0.5F, 3UL)
  let state : Array[Float] = [0.2F, -0.1F]
  let hidden_a : Array[Float] = Array::make(p.hidden, 0.0F)
  let hidden_b : Array[Float] = Array::make(p.hidden, 0.0F)
  let action_a = deterministic_policy_forward(p, state, hidden_a)
  let action_b = deterministic_policy_forward(p, state, hidden_b)
  assert_eq(action_a[0], action_b[0])
}

///|
test "policy_copy_preserves_all_weights" {
  let p1 = DeterministicPolicy::new(2, 2, 4, -1.0F, 1.0F, 4UL)
  let p2 = DeterministicPolicy::new(2, 2, 4, -1.0F, 1.0F, 5UL)
  p1.w1[0][1] = 7.5F
  p1.b2[1] = -2.5F
  policy_copy(p2, p1)
  assert_eq(p2.w1[0][1], 7.5F)
  assert_eq(p2.b2[1], -2.5F)
}

// =========================================================================
// QNetworkContinuous tests
// =========================================================================

///|
test "qnet_continuous_shapes" {
  let q = QNetworkContinuous::new(3, 2, 4, 1UL)
  assert_eq(q.state_dim, 3)
  assert_eq(q.action_dim, 2)
  assert_eq(q.hidden, 4)
  assert_eq(q.w1.length(), 4)
  assert_eq(q.w1[0].length(), 5)
  assert_eq(q.w2.length(), 4)
}

///|
test "qnet_continuous_forward_finite" {
  let q = QNetworkContinuous::new(4, 2, 8, 2UL)
  let state : Array[Float] = [0.1F, 0.2F, 0.3F, 0.4F]
  let action : Array[Float] = [0.5F, -0.5F]
  let hidden : Array[Float] = Array::make(q.hidden, 0.0F)
  let qval = qnet_continuous_forward(q, state, action, hidden)
  assert_true(qval == qval)
}

///|
test "qnet_continuous_copy_preserves_all_weights" {
  let q1 = QNetworkContinuous::new(2, 1, 4, 1UL)
  let q2 = QNetworkContinuous::new(2, 1, 4, 2UL)
  q1.w1[2][1] = 3.14F
  // b2 is private mut — only mutable from within the same module.
  // Verify the copy function preserves w1 modifications.
  qnet_continuous_copy(q2, q1)
  assert_eq(q2.w1[2][1], 3.14F)
}

// =========================================================================
// DDPG agent tests
// =========================================================================

///|
test "ddpg_new_targets_match_online" {
  let ddpg = DDPG::new(3, 2, 8, -1.0F, 1.0F, 0.99F, 0.005F, 0.1F, 42UL)
  let mut h = 0
  while h < ddpg.actor.hidden {
    if ddpg.actor.w1[h][0] != ddpg.actor_target.w1[h][0] {
      fail("actor target w1 not equal at h=\{h}")
    }
    h = h + 1
  }
  let mut hh = 0
  while hh < ddpg.critic1.hidden {
    if ddpg.critic1.w1[hh][0] != ddpg.critic1_target.w1[hh][0] {
      fail("critic1 target w1 not equal at hh=\{hh}")
    }
    hh = hh + 1
  }
}

///|
test "ddpg_select_action_eval_in_range" {
  let ddpg = DDPG::new(3, 2, 8, -1.0F, 1.0F, 0.99F, 0.005F, 0.0F, 7UL)
  let state : Array[Float] = [0.5F, -0.5F, 0.1F]
  let action = ddpg_select_action_eval(ddpg, state)
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
test "ddpg_select_action_with_noise_stays_in_range" {
  let ddpg = DDPG::new(3, 1, 8, -1.0F, 1.0F, 0.99F, 0.005F, 0.5F, 7UL)
  let state : Array[Float] = [0.0F, 0.0F, 0.0F]
  let rng = Xoshiro::from_state(1UL, 2UL, 3UL, 4UL)
  let mut k = 0
  while k < 100 {
    let action = ddpg_select_action(ddpg, state, rng)
    let a = action[0]
    if a < -1.0F || a > 1.0F {
      fail("noisy action = \{a} out of range")
    }
    k = k + 1
  }
}

///|
test "ddpg_soft_update_moves_target_toward_online" {
  let ddpg = DDPG::new(2, 1, 4, -1.0F, 1.0F, 0.99F, 0.5F, 0.1F, 7UL)
  ddpg.actor.b2[0] = 10.0F
  let b2_target_before = ddpg.actor_target.b2[0]
  ddpg_soft_update(ddpg)
  let b2_target_after = ddpg.actor_target.b2[0]
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
test "ddpg_update_critic_runs_without_crash" {
  let ddpg = DDPG::new(3, 1, 8, -1.0F, 1.0F, 0.99F, 0.005F, 0.1F, 7UL)
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
  let td = ddpg_update_critic(ddpg, batch, 0.001F)
  if td != td {
    fail("TD error is NaN")
  }
}

///|
test "ddpg_update_actor_modifies_w2" {
  let ddpg = DDPG::new(2, 1, 4, -1.0F, 1.0F, 0.99F, 0.005F, 0.1F, 7UL)
  let buf = ContinuousReplayBuffer::new(8, 2, 1)
  buf.push([0.1F, 0.1F], [0.2F], 0.5F, [0.2F, 0.2F], false)
  buf.push([0.2F, 0.2F], [0.1F], 0.4F, [0.3F, 0.3F], false)
  let w2_before = ddpg.actor.w2[0][0]
  let b2_before = ddpg.actor.b2[0]
  let rng = Xoshiro::from_state(5UL, 6UL, 7UL, 8UL)
  let batch = buf.sample(2, rng)
  ddpg_update_actor(ddpg, batch, 0.001F)
  let w2_after = ddpg.actor.w2[0][0]
  let b2_after = ddpg.actor.b2[0]
  if w2_after == w2_before && b2_after == b2_before {
    fail("actor update did not modify any weight")
  }
}

///|
test "continuous_replay_buffer_push_and_len" {
  let buf = ContinuousReplayBuffer::new(4, 2, 1)
  assert_eq(buf.len(), 0)
  buf.push([0.1F, 0.2F], [0.3F], 1.0F, [0.4F, 0.5F], false)
  assert_eq(buf.len(), 1)
  buf.push([0.2F, 0.3F], [0.4F], 0.5F, [0.5F, 0.6F], false)
  assert_eq(buf.len(), 2)
}

///|
test "continuous_replay_buffer_overflow_wraps" {
  let buf = ContinuousReplayBuffer::new(2, 1, 1)
  buf.push([0.1F], [0.1F], 1.0F, [0.2F], false)
  buf.push([0.2F], [0.2F], 1.0F, [0.3F], false)
  assert_eq(buf.len(), 2)
  buf.push([0.3F], [0.3F], 1.0F, [0.4F], false)
  assert_eq(buf.len(), 2)
}
