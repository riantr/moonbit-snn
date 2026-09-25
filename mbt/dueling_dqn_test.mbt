// dueling_dqn_test.mbt — Tests for Dueling DQN (v0.37.1).

///|
test "dueling_qnet_new: shapes" {
  let d = DuelingQNet::new(4, 3, 7UL)
  assert_eq(d.n_states, 4)
  assert_eq(d.n_actions, 3)
  assert_eq(d.value.n_states, 4)
  assert_eq(d.value.n_actions, 3)
  assert_eq(d.advantage.n_states, 4)
  assert_eq(d.advantage.n_actions, 3)
}

///|
test "dueling_q_forward: mean advantage subtracted" {
  let d = DuelingQNet::new(2, 3, 11UL)
  // Zero out everything.
  for i in 0..<3 {
    for j in 0..<2 {
      d.value.w[i][j] = 0.0F
      d.advantage.w[i][j] = 0.0F
    }
    d.value.b[i] = 0.0F
    d.advantage.b[i] = 0.0F
  }
  // Set V(s) = 2 (in row 0 of value) and advantages = [1, 2, 3].
  d.value.w[0][0] = 2.0F
  d.advantage.w[0][0] = 1.0F
  d.advantage.w[1][0] = 2.0F
  d.advantage.w[2][0] = 3.0F
  let x : Array[Float] = [1.0F, 0.0F]
  let q = dueling_q_forward(d, x)
  // mean_a = (1 + 2 + 3) / 3 = 2.
  // Q[0] = 2 + 1 - 2 = 1.
  // Q[1] = 2 + 2 - 2 = 2.
  // Q[2] = 2 + 3 - 2 = 3.
  let diff0 = if q[0] > 1.0F { q[0] - 1.0F } else { 1.0F - q[0] }
  let diff1 = if q[1] > 2.0F { q[1] - 2.0F } else { 2.0F - q[1] }
  let diff2 = if q[2] > 3.0F { q[2] - 3.0F } else { 3.0F - q[2] }
  assert_true(diff0 < 1.0e-5F)
  assert_true(diff1 < 1.0e-5F)
  assert_true(diff2 < 1.0e-5F)
}

///|
test "dueling_q_forward: identical advantage -> Q = V" {
  let d = DuelingQNet::new(2, 3, 17UL)
  for i in 0..<3 {
    for j in 0..<2 {
      d.value.w[i][j] = 0.0F
      d.advantage.w[i][j] = 0.0F
    }
    d.value.b[i] = 0.0F
    d.advantage.b[i] = 0.0F
  }
  // Same advantage for all 3 actions: mean = advantage.
  d.value.w[0][0] = 1.5F
  d.advantage.w[0][0] = 0.5F
  d.advantage.w[1][0] = 0.5F
  d.advantage.w[2][0] = 0.5F
  let x : Array[Float] = [1.0F, 0.0F]
  let q = dueling_q_forward(d, x)
  // Q[i] = 1.5 + 0.5 - 0.5 = 1.5 for all i.
  for i in 0..<3 {
    let diff = if q[i] > 1.5F { q[i] - 1.5F } else { 1.5F - q[i] }
    assert_true(diff < 1.0e-5F)
  }
}

///|
test "dueling_dqn_update_step: zero init + zero reward -> zero |δ|" {
  let env = GridWorld::new(2, 2, 5)
  let q = DuelingQNet::new(4, 4, 23UL)
  let target = DuelingQNet::new(4, 4, 29UL)
  for i in 0..<4 {
    for j in 0..<4 {
      q.value.w[i][j] = 0.0F
      q.advantage.w[i][j] = 0.0F
    }
    q.value.b[i] = 0.0F
    q.advantage.b[i] = 0.0F
  }
  dueling_qnet_copy(target, q)
  let ss : Array[Int] = [0]
  let aa : Array[Int] = [0]
  let rr : Array[Float] = [0.0F]
  let nss : Array[Int] = [0]
  let dd : Array[Bool] = [true]
  let mean = dueling_dqn_update_step(q, target, ss, aa, rr, nss, dd, 0.9F, 0.1F)
  assert_true(mean.abs() < 1.0e-5F)
  let _ = env
}

///|
test "dueling_dqn_update_step: positive reward increases Q" {
  let q = DuelingQNet::new(2, 2, 31UL)
  let target = DuelingQNet::new(2, 2, 37UL)
  for i in 0..<2 {
    for j in 0..<2 {
      q.value.w[i][j] = 0.0F
      q.advantage.w[i][j] = 0.0F
      target.value.w[i][j] = 0.0F
      target.advantage.w[i][j] = 0.0F
    }
    q.value.b[i] = 0.0F
    q.advantage.b[i] = 0.0F
    target.value.b[i] = 0.0F
    target.advantage.b[i] = 0.0F
  }
  let ss : Array[Int] = [0]
  let aa : Array[Int] = [0]
  let rr : Array[Float] = [1.0F]
  let nss : Array[Int] = [0]
  let dd : Array[Bool] = [true]
  let _ = dueling_dqn_update_step(q, target, ss, aa, rr, nss, dd, 0.9F, 0.1F)
  let x : Array[Float] = [1.0F, 0.0F]
  let q_after = dueling_q_forward(q, x)
  // Q[0] should have moved toward 1 from 0, so > 0.
  assert_true(q_after[0] > 0.0F)
}

///|
test "dueling_q_argmax: greedy" {
  let q1 : Array[Float] = [1.0F, 2.0F, 3.0F]
  assert_eq(dueling_q_argmax(q1), 2)
  let q2 : Array[Float] = [3.0F, 3.0F, 1.0F]
  assert_eq(dueling_q_argmax(q2), 0)
}

///|
test "dueling_eps_greedy_action: epsilon=0 -> argmax" {
  let q = DuelingQNet::new(2, 2, 41UL)
  q.value.w[0][0] = 5.0F
  q.advantage.w[0][0] = 1.0F
  q.advantage.w[1][0] = 0.0F
  let rng = Xoshiro::from_state(43UL, 47UL, 53UL, 59UL)
  let a = dueling_eps_greedy_action(q, 0, 0.0F, rng)
  // For state 0: V=5, A=[1,0], mean=0.5. Q=[5.5, 4.5]. argmax=0.
  assert_eq(a, 0)
}

///|
test "train_dueling_dqn: shapes + mean is finite" {
  let env = GridWorld::new(2, 2, 5)
  let q = DuelingQNet::new(4, 4, 67UL)
  let target = DuelingQNet::new(4, 4, 71UL)
  let mean = train_dueling_dqn(
    env, q, target, 30, 0.9F, 0.05F, 1.0F, 0.05F, 5, 100, 5, 10, 8, 73UL,
  )
  assert_true(mean == mean)
  assert_true(mean > -5.0F && mean < 5.0F)
}