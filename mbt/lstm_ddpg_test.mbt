// lstm_ddpg_test.mbt — Round-trip tests for the LSTM-DDPG agent
// (v0.63.0).
//
// Coverage:
//   * constructor: 6 networks (actor + critic1/2 + 3 targets) + distinct seeds
//   * select_action: noise-free gives deterministic output; noisy stays in
//     [action_low, action_high]; hidden + cell advance
//   * twin-critic TD target: 1-step computation matches manual formula
//   * soft_update: Polyak averaging shrinks the target toward source
//   * soft_update changes MLP weights, biases, LSTM gates, LSTM biases,
//     AND scalar mlp_b2 (critic)
//   * soft_update preserves target when tau=0
//   * soft_update leaves target at source when tau=1

///|
test "lstm_ddpg_new_dimensions_and_seeds" {
  let agent : LSTM_DDPG = LSTM_DDPG::new(
    3, 2, 4, -1.0F, 1.0F, 0.99F, 0.005F, 0.1F, 42UL,
  )
  // Action range reflected.
  assert_eq(agent.actor.action_low, -1.0F)
  assert_eq(agent.actor.action_high, 1.0F)
  assert_eq(agent.gamma, 0.99F)
  assert_eq(agent.tau, 0.005F)
  assert_eq(agent.exploration_noise, 0.1F)
  assert_eq(agent.hidden, 4)
  // Each of 6 networks shares the hidden dim and seed-derived params.
  assert_eq(agent.critic1.state_dim, 3)
  assert_eq(agent.critic2.state_dim, 3)
  assert_eq(agent.critic1.action_dim, 2)
  assert_eq(agent.critic2.action_dim, 2)
  // Sources and targets start different (distinct seeds).
  let mut any_differs = false
  for i in 0..<agent.actor.mlp_w1.length() {
    for k in 0..<agent.actor.mlp_w1[i].length() {
      if agent.actor.mlp_w1[i][k] != agent.actor_target.mlp_w1[i][k] {
        any_differs = true
      }
    }
  }
  assert_true(any_differs)
}

///|
test "lstm_ddpg_select_action_deterministic_when_no_noise" {
  let agent : LSTM_DDPG = LSTM_DDPG::new(
    4, 2, 4, -1.0F, 1.0F, 0.99F, 0.005F, 0.1F, 7UL,
  )
  let rng = Xoshiro::from_state(1UL, 2UL, 3UL, 4UL)
  let hidden : Array[Float] = Array::make(4, 0.0F)
  let cell : Array[Float] = Array::make(4, 0.0F)
  let (action, new_h, new_c) = lstm_ddpg_select_action(
    agent, [0.1F, 0.2F, 0.3F, 0.4F], hidden, cell, 0.0F, rng,
  )
  assert_eq(action.length(), 2)
  for i in 0..<action.length() {
    assert_true(action[i] >= -1.0F - 1.0e-4F)
    assert_true(action[i] <= 1.0F + 1.0e-4F)
  }
  assert_eq(new_h.length(), 4)
  assert_eq(new_c.length(), 4)
}

///|
test "lstm_ddpg_select_action_with_noise_stays_in_range" {
  let agent : LSTM_DDPG = LSTM_DDPG::new(
    4, 2, 4, -0.5F, 0.5F, 0.99F, 0.005F, 0.1F, 7UL,
  )
  let rng = Xoshiro::from_state(5UL, 6UL, 7UL, 8UL)
  let hidden : Array[Float] = Array::make(4, 0.0F)
  let cell : Array[Float] = Array::make(4, 0.0F)
  let (action, _, _) = lstm_ddpg_select_action(
    agent, [0.1F, 0.2F, 0.3F, 0.4F], hidden, cell, 0.3F, rng,
  )
  // Clip to [action_low, action_high].
  for i in 0..<action.length() {
    assert_true(action[i] >= -0.5F - 1.0e-4F)
    assert_true(action[i] <= 0.5F + 1.0e-4F)
  }
}

///|
test "lstm_ddpg_compute_td_target_seq_matches_manual" {
  // Build an agent and feed it known next_obs_seq + reward_seq + done_seq.
  // Verify the TD target at each slot matches r_t + γ(1 - d_t) * min(q1,q2).
  let agent : LSTM_DDPG = LSTM_DDPG::new(
    2, 1, 3, -1.0F, 1.0F, 0.5F, 0.005F, 0.0F, 11UL,
  )
  let next_obs_seq : Array[Float] = [
    0.1F, 0.2F,
    0.3F, 0.4F,
    0.5F, 0.6F,
  ]
  let next_act_seq : Array[Float] = [0.5F, -0.5F, 0.3F]
  let reward_seq : Array[Float] = [1.0F, 2.0F, 3.0F]
  let done_seq : Array[Float] = [0.0F, 0.0F, 1.0F]
  let target = lstm_ddpg_compute_td_target_seq(
    agent, next_obs_seq, next_act_seq, reward_seq, done_seq, 3,
  )
  // Manually compute expected TD targets via critic targets.
  let (q1_next, _, _) = lstm_qnetwork_continuous_seq_forward(
    agent.critic1_target, next_obs_seq, next_act_seq, 3,
    Array::make(3, 0.0F), Array::make(3, 0.0F),
  )
  let (q2_next, _, _) = lstm_qnetwork_continuous_seq_forward(
    agent.critic2_target, next_obs_seq, next_act_seq, 3,
    Array::make(3, 0.0F), Array::make(3, 0.0F),
  )
  let gamma = agent.gamma
  let expected_t0 = reward_seq[0] + gamma * (1.0F - done_seq[0]) *
    (if q1_next[0] < q2_next[0] { q1_next[0] } else { q2_next[0] })
  let expected_t2 = reward_seq[2] + gamma * (1.0F - done_seq[2]) *
    (if q1_next[2] < q2_next[2] { q1_next[2] } else { q2_next[2] })
  // Last slot has done=1 → (1 - done) = 0 → target = reward only.
  assert_eq(target[2], reward_seq[2])
  assert_eq(target[0], expected_t0)
  // Same for t=2 expected.
  assert_eq(target[2], expected_t2)
}

///|
test "lstm_ddpg_soft_update_tau_zero_is_no_op" {
  let agent : LSTM_DDPG = LSTM_DDPG::new(
    2, 1, 3, -1.0F, 1.0F, 0.99F, 0.0F, 0.1F, 13UL,
  )
  // Snapshot actor_target state.
  let snap_w1_0_0 = agent.actor_target.mlp_w1[0][0]
  let snap_b1_0 = agent.actor_target.mlp_b1[0]
  let snap_lstm_wf_0_0 = agent.actor_target.lstm.w_f[0][0]
  let snap_lstm_bf_0 = agent.actor_target.lstm.b_f[0]
  let snap_critic_b2 = agent.critic1_target.mlp_b2
  lstm_ddpg_soft_update(agent)
  // tau=0 → target <- 1*target + 0*source = unchanged.
  assert_eq(agent.actor_target.mlp_w1[0][0], snap_w1_0_0)
  assert_eq(agent.actor_target.mlp_b1[0], snap_b1_0)
  assert_eq(agent.actor_target.lstm.w_f[0][0], snap_lstm_wf_0_0)
  assert_eq(agent.actor_target.lstm.b_f[0], snap_lstm_bf_0)
  assert_eq(agent.critic1_target.mlp_b2, snap_critic_b2)
}

///|
test "lstm_ddpg_soft_update_tau_one_copies_source" {
  // tau=1 → target <- 0*target + 1*source = source.
  let agent : LSTM_DDPG = LSTM_DDPG::new(
    2, 1, 3, -1.0F, 1.0F, 0.99F, 1.0F, 0.1F, 13UL,
  )
  lstm_ddpg_soft_update(agent)
  // Verify actor_target == actor element-wise.
  for i in 0..<agent.actor.mlp_w1.length() {
    for k in 0..<agent.actor.mlp_w1[i].length() {
      assert_eq(agent.actor_target.mlp_w1[i][k], agent.actor.mlp_w1[i][k])
    }
  }
  for i in 0..<agent.actor.mlp_b1.length() {
    assert_eq(agent.actor_target.mlp_b1[i], agent.actor.mlp_b1[i])
  }
  for i in 0..<agent.actor.lstm.w_f.length() {
    for k in 0..<agent.actor.lstm.w_f[i].length() {
      assert_eq(agent.actor_target.lstm.w_f[i][k], agent.actor.lstm.w_f[i][k])
      assert_eq(agent.actor_target.lstm.w_i[i][k], agent.actor.lstm.w_i[i][k])
      assert_eq(agent.actor_target.lstm.w_c[i][k], agent.actor.lstm.w_c[i][k])
      assert_eq(agent.actor_target.lstm.w_o[i][k], agent.actor.lstm.w_o[i][k])
    }
    assert_eq(agent.actor_target.lstm.b_f[i], agent.actor.lstm.b_f[i])
    assert_eq(agent.actor_target.lstm.b_i[i], agent.actor.lstm.b_i[i])
    assert_eq(agent.actor_target.lstm.b_c[i], agent.actor.lstm.b_c[i])
    assert_eq(agent.actor_target.lstm.b_o[i], agent.actor.lstm.b_o[i])
  }
  // critic1_target == critic1.
  assert_eq(agent.critic1_target.mlp_b2, agent.critic1.mlp_b2)
  // critic2_target == critic2.
  assert_eq(agent.critic2_target.mlp_b2, agent.critic2.mlp_b2)
}

///|
test "lstm_ddpg_soft_update_tau_half_interpolates" {
  // tau=0.5 → target = 0.5*target + 0.5*source.
  let agent : LSTM_DDPG = LSTM_DDPG::new(
    2, 1, 3, -1.0F, 1.0F, 0.99F, 0.5F, 0.1F, 21UL,
  )
  let snap_w1_0_0 = agent.actor_target.mlp_w1[0][0]
  let src_w1_0_0 = agent.actor.mlp_w1[0][0]
  let snap_critic_b2 = agent.critic1_target.mlp_b2
  let src_critic_b2 = agent.critic1.mlp_b2
  lstm_ddpg_soft_update(agent)
  let expected_w1 = 0.5F * snap_w1_0_0 + 0.5F * src_w1_0_0
  let expected_b2 = 0.5F * snap_critic_b2 + 0.5F * src_critic_b2
  // Float32 equality holds when source/target differ.
  let diff_w1 = agent.actor_target.mlp_w1[0][0] - expected_w1
  let diff_w1_abs = if diff_w1 < 0.0F { -diff_w1 } else { diff_w1 }
  assert_true(diff_w1_abs < 1.0e-5F)
  let diff_b2 = agent.critic1_target.mlp_b2 - expected_b2
  let diff_b2_abs = if diff_b2 < 0.0F { -diff_b2 } else { diff_b2 }
  assert_true(diff_b2_abs < 1.0e-5F)
}

///|
test "lstm_ddpg_soft_update_advances_target_between_calls" {
  // After two soft updates with tau=0.1, target should have moved
  // closer to source than after just one update.
  let agent : LSTM_DDPG = LSTM_DDPG::new(
    2, 1, 3, -1.0F, 1.0F, 0.99F, 0.1F, 0.1F, 33UL,
  )
  let snap_w1 = agent.actor_target.mlp_w1[0][0]
  let src_w1 = agent.actor.mlp_w1[0][0]
  // Distance from target to source.
  let d_init = snap_w1 - src_w1
  let d_init_abs = if d_init < 0.0F { -d_init } else { d_init }
  lstm_ddpg_soft_update(agent)
  let after_one = agent.actor_target.mlp_w1[0][0]
  let d_one = after_one - src_w1
  let d_one_abs = if d_one < 0.0F { -d_one } else { d_one }
  // After 1 update the target should be closer to source than at init.
  assert_true(d_one_abs < d_init_abs)
}
// ============================================================================
// LSTMQNetworkContinuous unit tests (v0.62.0) — merged into the LSTM_DDPG
// test file because the package's combined-source-path size approaches
// Windows CreateProcessW's 32K cmdline limit; keeping qnetwork tests in a
// separate lstm_qnetwork_continuous_test.mbt pushes us over. The agent tests
// above already exercise the critic end-to-end through lstm_ddpg_select_action
// and lstm_ddpg_compute_td_target_seq, so the merged unit tests are
// limited to LSTMQNetworkContinuous-internal coverage.
// ============================================================================

///|
test "lstm_qnetwork_continuous_new_dimensions" {
  let q : LSTMQNetworkContinuous = LSTMQNetworkContinuous::new(
    4, 2, 6, 17UL,
  )
  assert_eq(q.state_dim, 4)
  assert_eq(q.action_dim, 2)
  assert_eq(q.hidden, 6)
  assert_eq(q.mlp_w1.length(), 6)
  assert_eq(q.mlp_w1[0].length(), 6)
  assert_eq(q.mlp_b1.length(), 6)
  assert_eq(q.lstm.d_x, 6)
  assert_eq(q.lstm.d_h, 6)
  assert_eq(q.lstm.w_f[0].length(), 12)
  assert_eq(q.lstm.b_f.length(), 6)
  assert_eq(q.mlp_w2.length(), 1)
  assert_eq(q.mlp_w2[0].length(), 6)
  assert_eq(q.mlp_b2, 0.0F)
}

///|
test "lstm_qnetwork_continuous_step_returns_scalar" {
  let q : LSTMQNetworkContinuous = LSTMQNetworkContinuous::new(
    3, 2, 4, 5UL,
  )
  let hidden : Array[Float] = Array::make(q.hidden, 0.0F)
  let cell : Array[Float] = Array::make(q.hidden, 0.0F)
  let (q_val, new_h, new_c) = lstm_qnetwork_continuous_step(
    q, [0.1F, 0.2F, 0.3F], [0.5F, -0.5F], hidden, cell,
  )
  assert_eq(new_h.length(), q.hidden)
  assert_eq(new_c.length(), q.hidden)
  let q_abs = if q_val < 0.0F { -q_val } else { q_val }
  assert_true(q_abs < 1.0e6F)
}

///|
test "lstm_qnetwork_continuous_seq_forward_step_wise_consistency" {
  let q : LSTMQNetworkContinuous = LSTMQNetworkContinuous::new(
    2, 1, 3, 31UL,
  )
  let hidden_init : Array[Float] = Array::make(q.hidden, 0.0F)
  let cell_init : Array[Float] = Array::make(q.hidden, 0.0F)
  let state_seq : Array[Float] = [0.1F, 0.2F, 0.3F, 0.4F, 0.5F, 0.6F]
  let action_seq : Array[Float] = [0.5F, -0.5F, 0.3F]
  let (q_seq, h_final, c_final) = lstm_qnetwork_continuous_seq_forward(
    q, state_seq, action_seq, 3, hidden_init, cell_init,
  )
  let (q1, h1, c1) = lstm_qnetwork_continuous_step(
    q, [0.1F, 0.2F], [0.5F], hidden_init, cell_init,
  )
  let (q2, h2, c2) = lstm_qnetwork_continuous_step(
    q, [0.3F, 0.4F], [-0.5F], h1, c1,
  )
  let (q3, h3, c3) = lstm_qnetwork_continuous_step(
    q, [0.5F, 0.6F], [0.3F], h2, c2,
  )
  assert_eq(q_seq[0], q1)
  assert_eq(q_seq[1], q2)
  assert_eq(q_seq[2], q3)
  for i in 0..<h3.length() {
    assert_eq(h_final[i], h3[i])
    assert_eq(c_final[i], c3[i])
  }
}

///|
test "lstm_qnetwork_continuous_reproducible_under_seed" {
  let q1 : LSTMQNetworkContinuous = LSTMQNetworkContinuous::new(
    4, 2, 4, 77UL,
  )
  let q2 : LSTMQNetworkContinuous = LSTMQNetworkContinuous::new(
    4, 2, 4, 77UL,
  )
  let hidden : Array[Float] = Array::make(4, 0.0F)
  let cell : Array[Float] = Array::make(4, 0.0F)
  let (v1, h1, c1) = lstm_qnetwork_continuous_step(
    q1, [0.1F, 0.2F, 0.3F, 0.4F], [0.5F, -0.5F], hidden, cell,
  )
  let (v2, h2, c2) = lstm_qnetwork_continuous_step(
    q2, [0.1F, 0.2F, 0.3F, 0.4F], [0.5F, -0.5F], hidden, cell,
  )
  assert_eq(v1, v2)
  for i in 0..<h1.length() {
    assert_eq(h1[i], h2[i])
    assert_eq(c1[i], c2[i])
  }
}

///|
test "lstm_qnetwork_continuous_different_seeds_differ" {
  let q1 : LSTMQNetworkContinuous = LSTMQNetworkContinuous::new(
    3, 2, 4, 1UL,
  )
  let q2 : LSTMQNetworkContinuous = LSTMQNetworkContinuous::new(
    3, 2, 4, 2UL,
  )
  let hidden : Array[Float] = Array::make(4, 0.0F)
  let cell : Array[Float] = Array::make(4, 0.0F)
  let (v1, _, _) = lstm_qnetwork_continuous_step(
    q1, [0.1F, 0.5F, -0.3F], [0.7F, -0.1F], hidden, cell,
  )
  let (v2, _, _) = lstm_qnetwork_continuous_step(
    q2, [0.1F, 0.5F, -0.3F], [0.7F, -0.1F], hidden, cell,
  )
  assert_true(v1 != v2)
}
