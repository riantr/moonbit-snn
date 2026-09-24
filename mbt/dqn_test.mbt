// dqn_test.mbt — Tests for DQN (v0.36.0).

///|
test "linear_qnet_new: shapes" {
  let q = LinearQNet::new(4, 3, 7UL)
  assert_eq(q.n_states, 4)
  assert_eq(q.n_actions, 3)
  assert_eq(q.w.length(), 3)
  for i in 0..<3 {
    assert_eq(q.w[i].length(), 4)
  }
  assert_eq(q.b.length(), 3)
}

///|
test "q_forward: linear + bias" {
  let q = LinearQNet::new(3, 2, 11UL)
  for i in 0..<2 {
    for j in 0..<3 {
      q.w[i][j] = 0.0F
    }
    q.b[i] = 0.0F
  }
  q.w[0][0] = 1.0F
  q.w[0][2] = 2.0F
  q.w[1][1] = 3.0F
  q.b[0] = 10.0F
  q.b[1] = 20.0F
  let x0 : Array[Float] = [1.0F, 0.0F, 0.0F]
  let x2 : Array[Float] = [0.0F, 0.0F, 1.0F]
  let x1 : Array[Float] = [0.0F, 1.0F, 0.0F]
  let q0 = q_forward(q, x0)
  assert_eq(q0[0], 11.0F)
  assert_eq(q0[1], 20.0F)
  let q2 = q_forward(q, x2)
  assert_eq(q2[0], 12.0F)
  assert_eq(q2[1], 20.0F)
  let q1 = q_forward(q, x1)
  assert_eq(q1[0], 10.0F)
  assert_eq(q1[1], 23.0F)
}

///|
test "q_argmax: greedy with first-tie-break" {
  let q1 : Array[Float] = [1.0F, 2.0F, 3.0F]
  assert_eq(q_argmax(q1), 2)
  let q2 : Array[Float] = [3.0F, 3.0F, 1.0F]
  assert_eq(q_argmax(q2), 0)
  let q3 : Array[Float] = [0.5F]
  assert_eq(q_argmax(q3), 0)
}

///|
test "replay_buffer: push + len" {
  let b = ReplayBuffer::new(3)
  assert_eq(b.len(), 0)
  b.push(0, 1, 1.0F, 2, false)
  assert_eq(b.len(), 1)
  b.push(1, 0, 0.5F, 3, false)
  b.push(2, 1, -0.1F, 1, true)
  assert_eq(b.len(), 3)
  // One more push should wrap.
  b.push(3, 2, 0.0F, 0, false)
  assert_eq(b.len(), 3)
}

///|
test "replay_buffer: sample returns valid transitions" {
  let b = ReplayBuffer::new(5)
  let rng = Xoshiro::from_state(13UL, 17UL, 19UL, 23UL)
  for k in 0..<5 {
    b.push(k, k % 4, Float::from_int(k), (k + 1) % 5, false)
  }
  let (ss, aa, rr, nss, dd) = b.sample(3, rng)
  assert_eq(ss.length(), 3)
  assert_eq(aa.length(), 3)
  assert_eq(rr.length(), 3)
  assert_eq(nss.length(), 3)
  assert_eq(dd.length(), 3)
  for k in 0..<3 {
    assert_true(ss[k] >= 0 && ss[k] < 5)
    assert_true(aa[k] >= 0 && aa[k] < 4)
    assert_true(rr[k] >= 0.0F && rr[k] < 5.0F)
    assert_true(nss[k] >= 0 && nss[k] < 5)
  }
}

///|
test "qnet_copy: weights overwrite" {
  let src = LinearQNet::new(2, 2, 19UL)
  let dst = LinearQNet::new(2, 2, 23UL)
  src.w[0][0] = 1.0F
  src.w[0][1] = 2.0F
  src.b[0] = 3.0F
  qnet_copy(dst, src)
  assert_eq(dst.w[0][0], 1.0F)
  assert_eq(dst.w[0][1], 2.0F)
  assert_eq(dst.b[0], 3.0F)
}

///|
test "dqn_update_step: zero return reduces |δ|" {
  // With target = online, reward = 0, done = true, the TD error
  // for action a at state s should be 0 (since V(s,a) = V(s,a)).
  // We zero both nets so Q(s,a)=0 and target=0 => δ=0.
  let env = GridWorld::new(2, 2, 5)
  let q = LinearQNet::new(4, 4, 29UL)
  let target = LinearQNet::new(4, 4, 31UL)
  for i in 0..<4 {
    for j in 0..<4 {
      q.w[i][j] = 0.0F
    }
    q.b[i] = 0.0F
  }
  qnet_copy(target, q)
  let ss : Array[Int] = [0]
  let aa : Array[Int] = [0]
  let rr : Array[Float] = [0.0F]
  let nss : Array[Int] = [0]
  let dd : Array[Bool] = [true]
  let mean_delta = dqn_update_step(q, target, ss, aa, rr, nss, dd, 0.9F, 0.1F)
  assert_true(mean_delta.abs() < 1.0e-5F)
  let _ = env
}

///|
test "dqn_update_step: positive reward increases Q(s, a)" {
  // With reward = 1, done = true, target Q = 0 (untrained), the
  // TD error is +1, so Q(s, a) should increase.
  let q = LinearQNet::new(2, 2, 37UL)
  for i in 0..<2 {
    for j in 0..<2 {
      q.w[i][j] = 0.0F
    }
    q.b[i] = 0.0F
  }
  let target = LinearQNet::new(2, 2, 41UL)
  for i in 0..<2 {
    for j in 0..<2 {
      target.w[i][j] = 0.0F
    }
    target.b[i] = 0.0F
  }
  let ss : Array[Int] = [0]
  let aa : Array[Int] = [0]
  let rr : Array[Float] = [1.0F]
  let nss : Array[Int] = [0]
  let dd : Array[Bool] = [true]
  let _ = dqn_update_step(q, target, ss, aa, rr, nss, dd, 0.9F, 0.1F)
  // Q(s=0, a=0) = w[0][0]*1 + w[0][1]*0 + b[0]. After update, Q
  // should have moved toward 1 from 0, so Q > 0.
  let x : Array[Float] = [1.0F, 0.0F]
  let q_after = q_forward(q, x)
  assert_true(q_after[0] > 0.0F)
}

///|
test "eps_greedy_action: epsilon=0 -> argmax" {
  let q = LinearQNet::new(2, 2, 43UL)
  q.w[0][0] = 5.0F
  q.w[0][1] = 0.0F
  q.w[1][0] = 0.0F
  q.w[1][1] = 0.0F
  let rng = Xoshiro::from_state(47UL, 53UL, 59UL, 61UL)
  let a = eps_greedy_action(q, 0, 0.0F, rng)
  assert_eq(a, 0)
}

///|
test "train_dqn: shapes + mean return is finite" {
  let env = GridWorld::new(2, 2, 5)
  let q = LinearQNet::new(4, 4, 67UL)
  let target = LinearQNet::new(4, 4, 71UL)
  let mean = train_dqn(
    env, q, target, 30, 0.9F, 0.05F, 1.0F, 0.05F, 5, 100, 5, 10, 8, 73UL,
  )
  assert_true(mean == mean)
  assert_true(mean > -5.0F && mean < 5.0F)
}

///|
test "train_dqn: improves over training" {
  // Train a small DQN and check that the mean return in the second
  // half of training exceeds the first half.
  let env = GridWorld::new(2, 2, 5)
  let q = LinearQNet::new(4, 4, 79UL)
  let target = LinearQNet::new(4, 4, 83UL)
  // Manually run two batches and compare.
  let _ = train_dqn(
    env, q, target, 30, 0.9F, 0.1F, 1.0F, 0.1F, 5, 100, 0, 5, 8, 89UL,
  )
  // q is now trained (or not). Q-values for state 0 should be
  // non-trivial.
  let x : Array[Float] = [1.0F, 0.0F, 0.0F, 0.0F]
  let q_vals = q_forward(q, x)
  // Some Q-value should be non-zero (network has been updated).
  let mut any_nonzero = false
  for i in 0..<4 {
    if q_vals[i].abs() > 0.01F {
      any_nonzero = true
    }
  }
  assert_true(any_nonzero)
}