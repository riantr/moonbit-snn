// sac_test.mbt — Tests for Soft Actor-Critic (v0.38.0).

///|
test "sac_new: shapes + targets synced to online" {
  let sac = Sac::new(4, 3, 0.1F, 7UL)
  assert_eq(sac.policy.n_states, 4)
  assert_eq(sac.policy.n_actions, 3)
  assert_eq(sac.q1.n_states, 4)
  assert_eq(sac.q1.n_actions, 3)
  assert_eq(sac.q2.n_states, 4)
  assert_eq(sac.q2_target.n_states, 4)
  // Target should be a copy of online at init.
  for i in 0..<3 {
    for j in 0..<4 {
      assert_eq(sac.q1.w[i][j], sac.q1_target.w[i][j])
      assert_eq(sac.q2.w[i][j], sac.q2_target.w[i][j])
    }
  }
}

///|
test "sac_soft_target: terminal -> reward only" {
  let sac = Sac::new(2, 2, 0.1F, 11UL)
  // Zero out everything.
  for i in 0..<2 {
    for j in 0..<2 {
      sac.policy.w[i][j] = 0.0F
      sac.q1.w[i][j] = 0.0F
      sac.q2.w[i][j] = 0.0F
      sac.q1_target.w[i][j] = 0.0F
      sac.q2_target.w[i][j] = 0.0F
    }
    sac.q1.b[i] = 0.0F
    sac.q2.b[i] = 0.0F
    sac.q1_target.b[i] = 0.0F
    sac.q2_target.b[i] = 0.0F
  }
  let target = sac_soft_target(sac, 1, 1.0F, true, 0.9F)
  assert_eq(target, 1.0F)
}

///|
test "sac_soft_target: uniform pi + zero Q + non-zero reward" {
  // With zero weights, policy is uniform and Q=0. Soft target
  // reduces to r + γ·Σ_a π(a) · (0 - α·log π(a)) = r + γ·(-α·H).
  // For uniform 2-action, H = log(2) ≈ 0.693. α=0.1 → -α·H ≈ -0.0693.
  // γ=0.9, r=1 → target ≈ 1 + 0.9·(-0.0693) ≈ 0.9376.
  let sac = Sac::new(2, 2, 0.1F, 13UL)
  for i in 0..<2 {
    for j in 0..<2 {
      sac.policy.w[i][j] = 0.0F
      sac.q1.w[i][j] = 0.0F
      sac.q2.w[i][j] = 0.0F
      sac.q1_target.w[i][j] = 0.0F
      sac.q2_target.w[i][j] = 0.0F
    }
    sac.q1.b[i] = 0.0F
    sac.q2.b[i] = 0.0F
    sac.q1_target.b[i] = 0.0F
    sac.q2_target.b[i] = 0.0F
  }
  let target = sac_soft_target(sac, 1, 1.0F, false, 0.9F)
  // Entropy bonus: Σ π(a) · (-α · log π(a)) = α · H(π) > 0.
  // For uniform 2-action, H = log(2) ≈ 0.693. α=0.1 → bonus ≈ 0.0693.
  // So target ≈ r + γ · α · H = 1 + 0.9 · 0.0693 ≈ 1.0624.
  let expected = 1.0F + 0.9F * 0.1F * 0.693147F
  let diff = if target > expected { target - expected } else { expected - target }
  assert_true(diff < 1.0e-4F)
}

///|
test "sac_critic_update: zero init + zero reward -> zero loss" {
  let sac = Sac::new(2, 2, 0.1F, 17UL)
  for i in 0..<2 {
    for j in 0..<2 {
      sac.policy.w[i][j] = 0.0F
      sac.q1.w[i][j] = 0.0F
      sac.q2.w[i][j] = 0.0F
    }
    sac.q1.b[i] = 0.0F
    sac.q2.b[i] = 0.0F
  }
  // No reward, no future → target = 0. Q1(a) = 0. Loss = 0.
  let ss : Array[Int] = [0]
  let aa : Array[Int] = [0]
  let rr : Array[Float] = [0.0F]
  let nss : Array[Int] = [0]
  let dd : Array[Bool] = [true]
  let loss = sac_critic_update(sac, ss, aa, rr, nss, dd, 0.9F, 0.1F)
  assert_true(loss < 1.0e-5F)
}

///|
test "sac_critic_update: positive reward moves Q toward target" {
  let sac = Sac::new(2, 2, 0.1F, 23UL)
  for i in 0..<2 {
    for j in 0..<2 {
      sac.policy.w[i][j] = 0.0F
      sac.q1.w[i][j] = 0.0F
      sac.q2.w[i][j] = 0.0F
    }
    sac.q1.b[i] = 0.0F
    sac.q2.b[i] = 0.0F
  }
  let ss : Array[Int] = [0]
  let aa : Array[Int] = [0]
  let rr : Array[Float] = [1.0F]
  let nss : Array[Int] = [0]
  let dd : Array[Bool] = [true]
  let _ = sac_critic_update(sac, ss, aa, rr, nss, dd, 0.9F, 0.1F)
  let x : Array[Float] = [1.0F, 0.0F]
  let q_after = q_forward(sac.q1, x)
  // Q(0, 0) should have moved toward 1 from 0 → > 0.
  assert_true(q_after[0] > 0.0F)
}

///|
test "sac_actor_update: weights change + stay bounded" {
  let sac = Sac::new(4, 3, 0.1F, 29UL)
  let snap : Array[Array[Float]] = Array::make(3, [])
  for i in 0..<3 {
    snap[i] = Array::make(4, 0.0F)
    for j in 0..<4 {
      snap[i][j] = sac.policy.w[i][j]
    }
  }
  let ss : Array[Int] = [0, 1, 2, 3]
  sac_actor_update(sac, ss, 0.05F)
  let mut changed = false
  for i in 0..<3 {
    for j in 0..<4 {
      if sac.policy.w[i][j] != snap[i][j] {
        changed = true
      }
      let diff = if sac.policy.w[i][j] > snap[i][j] {
        sac.policy.w[i][j] - snap[i][j]
      } else {
        snap[i][j] - sac.policy.w[i][j]
      }
      assert_true(diff < 1.0F)
    }
  }
  assert_true(changed)
}

///|
test "sac_soft_update: target = tau · online + (1 - tau) · old_target" {
  let sac = Sac::new(2, 2, 0.1F, 31UL)
  // Set online = [2, 2] and target = [0, 0] (post-init zeros + drift).
  for i in 0..<2 {
    for j in 0..<2 {
      sac.q1.w[i][j] = 0.0F
      sac.q1_target.w[i][j] = 0.0F
    }
    sac.q1.b[i] = 0.0F
    sac.q1_target.b[i] = 0.0F
  }
  sac.q1.w[0][0] = 2.0F
  sac.q1_target.w[0][0] = 0.0F
  sac_soft_update(sac, 0.1F)
  // Expected: 0.1·2 + 0.9·0 = 0.2.
  let diff = if sac.q1_target.w[0][0] > 0.2F {
    sac.q1_target.w[0][0] - 0.2F
  } else {
    0.2F - sac.q1_target.w[0][0]
  }
  assert_true(diff < 1.0e-5F)
}

///|
test "sac_sample_action: returns valid action" {
  let sac = Sac::new(2, 3, 0.1F, 37UL)
  let rng = Xoshiro::from_state(41UL, 43UL, 47UL, 53UL)
  let a = sac_sample_action(sac.policy, 0, rng)
  assert_true(a >= 0 && a < 3)
}

///|
test "train_sac: shapes + return is finite" {
  let env = GridWorld::new(2, 2, 5)
  let sac = Sac::new(4, 4, 0.1F, 59UL)
  let mean = train_sac(
    env, sac, 20, 0.9F, 0.05F, 0.05F, 0.05F, 5, 100, 5, 8, 61UL,
  )
  assert_true(mean == mean)
  assert_true(mean > -5.0F && mean < 5.0F)
}