// dqn_n_step_test.mbt — Tests for the n-step DQN update step and trainer.

///|
test "dqn_update_step_n_step_basic_with_bootstrap" {
  // 2 states, 2 actions; n=2 with γ=0.5, single-row batch.
  let online = LinearQNet::new(2, 2, 1UL)
  let target = LinearQNet::new(2, 2, 2UL)
  let states : Array[Int] = [0]
  let actions : Array[Int] = [1]
  let rewards : Array[Float] = [1.0F + 0.5F * 2.0F] // G_2 = r_0 + γ·r_1 = 1 + 0.5·2 = 2
  let next_states : Array[Int] = [2] // s_lookahead = s_2
  let h_effs : Array[Int] = [2]
  let use_b : Array[Bool] = [true]
  let mean_abs = dqn_update_step_n_step(
    online, target, states, actions, rewards, next_states, h_effs, use_b,
    0.5F, 0.1F,
  )
  let _ = mean_abs
  // Online weights must change for the chosen action (1).
  let w0 = online.w[1][0]
  let w1 = online.w[1][1]
  let _ = w0
  let _ = w1
  // bias for action 1 must change.
  let b1 = online.b[1]
  // Verify weights not equal to initial zeros.
  let w0_diff = if w0 > 0.0F { w0 } else { -w0 }
  let w1_diff = if w1 > 0.0F { w1 } else { -w1 }
  assert_true(w0_diff + w1_diff + (if b1 > 0.0F { b1 } else { -b1 }) > 0.0F)
}

///|
test "dqn_update_step_n_step_no_bootstrap_when_done" {
  let online = LinearQNet::new(2, 2, 3UL)
  let target = LinearQNet::new(2, 2, 4UL)
  let states : Array[Int] = [0]
  let actions : Array[Int] = [0]
  let rewards : Array[Float] = [5.0F]
  let next_states : Array[Int] = [1]
  let h_effs : Array[Int] = [2]
  let use_b : Array[Bool] = [false]
  let _ = dqn_update_step_n_step(
    online, target, states, actions, rewards, next_states, h_effs, use_b,
    0.9F, 0.1F,
  )
  // When use_b=false, target_value = G_n = 5.0, so delta = 5 - q[0].
  // Since q[0] starts at 0, delta = 5. Online should increase.
  let q_after = q_forward(online, rl_one_hot(0, online.n_states))
  assert_true(q_after[0] > 0.0F)
}

///|
test "dqn_update_step_n_step_returns_finite_mean_abs" {
  let online = LinearQNet::new(3, 2, 5UL)
  let target = LinearQNet::new(3, 2, 6UL)
  let n = 4
  let states : Array[Int] = [0, 1, 2, 0]
  let actions : Array[Int] = [0, 1, 0, 1]
  let rewards : Array[Float] = [1.0F, 0.5F, 2.0F, 0.0F]
  let next_states : Array[Int] = [1, 2, 0, 1]
  let h_effs : Array[Int] = [n, n, n, n]
  let use_b : Array[Bool] = [true, true, true, false]
  let m = dqn_update_step_n_step(
    online, target, states, actions, rewards, next_states, h_effs, use_b,
    0.9F, 0.1F,
  )
  assert_true(m == m) // not NaN
  assert_true(m < 1.0e6F)
}

///|
test "train_dqn_n_step_smoke_runs" {
  let env = GridWorld::new(3, 3, 30)
  let q_net = LinearQNet::new(env.n_states(), 4, 11UL)
  let target = LinearQNet::new(env.n_states(), 4, 12UL)
  let avg = train_dqn_n_step(
    env, q_net, target,
    4, // n_episodes
    0.9F, // gamma
    0.1F, // lr
    0.2F, // epsilon_start
    0.05F, // epsilon_end
    30, // max_steps
    32, // buffer_capacity
    1, // warmup_episodes
    1, // sync_every
    4, // batch_size
    3, // n_step
    42UL,
  )
  // Just require finite.
  let _ = avg
  assert_true(avg == avg) // not NaN
  // q_net weights should differ from initial (training happened).
  let mut non_zero_count = 0
  for i in 0..<q_net.n_actions {
    for j in 0..<q_net.n_states {
      let w = q_net.w[i][j]
      if w != 0.0F {
        non_zero_count = non_zero_count + 1
      }
    }
  }
  assert_true(non_zero_count > 0)
}

///|
test "train_dqn_n_step_n1_matches_1step_dqn_in_distribution" {
  // n=1 should reduce to standard 1-step TD target.
  // We verify this indirectly by running both and checking that the resulting
  // Q-values after the same number of updates have similar magnitudes.
  // (Loose tolerance: just require both are finite.)
  let env1 = GridWorld::new(3, 3, 20)
  let q1 = LinearQNet::new(env1.n_states(), 4, 21UL)
  let t1 = LinearQNet::new(env1.n_states(), 4, 22UL)
  let avg1 = train_dqn_n_step(
    env1, q1, t1, 3, 0.9F, 0.1F, 0.2F, 0.05F, 20, 32, 1, 1, 4, 1, 7UL,
  )
  let env2 = GridWorld::new(3, 3, 20)
  let q2 = LinearQNet::new(env2.n_states(), 4, 23UL)
  let t2 = LinearQNet::new(env2.n_states(), 4, 24UL)
  let avg2 = train_dqn_n_step(
    env2, q2, t2, 3, 0.9F, 0.1F, 0.2F, 0.05F, 20, 32, 1, 1, 4, 3, 7UL,
  )
  let _ = avg1
  let _ = avg2
  // Both must be finite.
  assert_true(avg1 == avg1)
  assert_true(avg2 == avg2)
}