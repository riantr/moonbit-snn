// double_dqn_test.mbt — Tests for Double DQN (v0.37.0).

///|
test "double_dqn_update_step: zero return reduces |δ| (zero init)" {
  let env = GridWorld::new(2, 2, 5)
  let q = LinearQNet::new(4, 4, 7UL)
  let target = LinearQNet::new(4, 4, 11UL)
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
  let mean = double_dqn_update_step(q, target, ss, aa, rr, nss, dd, 0.9F, 0.1F)
  assert_true(mean.abs() < 1.0e-5F)
  let _ = env
}

///|
test "double_dqn_update_step: positive reward increases Q" {
  let q = LinearQNet::new(2, 2, 13UL)
  let target = LinearQNet::new(2, 2, 17UL)
  for i in 0..<2 {
    for j in 0..<2 {
      q.w[i][j] = 0.0F
      target.w[i][j] = 0.0F
    }
    q.b[i] = 0.0F
    target.b[i] = 0.0F
  }
  let ss : Array[Int] = [0]
  let aa : Array[Int] = [0]
  let rr : Array[Float] = [1.0F]
  let nss : Array[Int] = [0]
  let dd : Array[Bool] = [true]
  let _ = double_dqn_update_step(q, target, ss, aa, rr, nss, dd, 0.9F, 0.1F)
  let x : Array[Float] = [1.0F, 0.0F]
  let q_vals = q_forward(q, x)
  assert_true(q_vals[0] > 0.0F)
}

///|
test "double_dqn_vs_vanilla_diff: zero when online == target" {
  let q = LinearQNet::new(2, 2, 19UL)
  for i in 0..<2 {
    for j in 0..<2 {
      q.w[i][j] = 0.0F
    }
    q.b[i] = 0.0F
  }
  let target = LinearQNet::new(2, 2, 23UL)
  qnet_copy(target, q)
  // With zero weights, all Q-values are 0. argmax picks 0;
  // vanilla max also picks 0. Difference = 0.
  let nss : Array[Int] = [0, 1]
  let diff = double_dqn_vs_vanilla_diff(q, target, nss, 0.9F)
  assert_true(diff.abs() < 1.0e-5F)
}

///|
test "double_dqn_vs_vanilla_diff: positive when online != target" {
  // Hand-craft so that online's argmax differs from target's max.
  let q = LinearQNet::new(2, 2, 29UL)
  // Online: action 0 has bigger logit.
  q.w[0][0] = 5.0F
  q.w[1][0] = 1.0F
  q.w[0][1] = 0.0F
  q.w[1][1] = 0.0F
  // Target: action 1 has bigger logit.
  let target = LinearQNet::new(2, 2, 31UL)
  target.w[0][0] = 0.0F
  target.w[1][0] = 10.0F
  target.w[0][1] = 0.0F
  target.w[1][1] = 0.0F
  // For state 0: online picks a=0 (Q=5), target max is a=1 (Q=10).
  // Double picks target's Q at a=0 = 0; vanilla picks target's max = 10.
  // Difference = |0 - 10| = 10.
  let nss : Array[Int] = [0]
  let diff = double_dqn_vs_vanilla_diff(q, target, nss, 1.0F)
  assert_true(diff > 5.0F)
}

///|
test "train_double_dqn: shapes + return is finite" {
  let env = GridWorld::new(2, 2, 5)
  let q = LinearQNet::new(4, 4, 37UL)
  let target = LinearQNet::new(4, 4, 41UL)
  let mean = train_double_dqn(
    env, q, target, 30, 0.9F, 0.05F, 1.0F, 0.05F, 5, 100, 5, 10, 8, 43UL,
  )
  assert_true(mean == mean)
  assert_true(mean > -5.0F && mean < 5.0F)
}

///|
test "double_dqn_update_step: finite mean |δ| on random batch" {
  let env = GridWorld::new(2, 2, 5)
  let q = LinearQNet::new(4, 4, 47UL)
  let target = LinearQNet::new(4, 4, 53UL)
  let rng = Xoshiro::from_state(59UL, 61UL, 67UL, 71UL)
  let buffer = ReplayBuffer::new(20)
  for k in 0..<10 {
    buffer.push(k % 4, k % 4, 0.0F, (k + 1) % 4, false)
  }
  let (ss, aa, rr, nss, dd) = buffer.sample(5, rng)
  let mean = double_dqn_update_step(q, target, ss, aa, rr, nss, dd, 0.9F, 0.1F)
  assert_true(mean == mean)
  assert_true(mean < 10.0F)
  let _ = env
}