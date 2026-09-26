// sac_auto_alpha_test.mbt — tests for v0.38.1 SAC auto-tuned α.
//
// Coverage:
//   1. Construction + initial α = exp(log_alpha_init).
//   2. Determinism: same seed → identical α trajectory.
//   3. α grows when target_entropy more negative than current mean
//      negative entropy (policy too random).
//   4. α shrinks when target_entropy exceeds current mean negative
//      entropy (policy too deterministic relative to target).
//   5. log_alpha stays finite (no NaN / Inf) after many updates.
//   6. End-to-end train_sac_auto_alpha returns a finite mean return.
//   7. Critic update still works (non-NaN, non-zero).
//   8. Soft target update still works.

///|
test "sac_auto_alpha_init" {
  let sac = SacAutoAlpha::new(4, 2, 0.0F, -1.0F, 0.1F, 42UL)
  // α = exp(0) = 1.0
  assert_true((sac_auto_alpha_get(sac) - 1.0F).abs() < 1.0e-5F)
  // Q nets and targets well-formed.
  assert_eq(sac.q1.n_states, 4)
  assert_eq(sac.q1.n_actions, 2)
  assert_eq(sac.q1_target.n_states, 4)
  assert_eq(sac.q1_target.n_actions, 2)
  // log_alpha initial value preserved.
  assert_true((sac.log_alpha - 0.0F).abs() < 1.0e-7F)
}

///|
test "sac_auto_alpha_determinism" {
  let s1 = SacAutoAlpha::new(4, 2, 0.0F, -1.0F, 0.1F, 7UL)
  let s2 = SacAutoAlpha::new(4, 2, 0.0F, -1.0F, 0.1F, 7UL)
  let states : Array[Int] = [0, 1, 2, 3]
  let _ = sac_auto_alpha_update_alpha(s1, states)
  let _ = sac_auto_alpha_update_alpha(s2, states)
  assert_true((s1.log_alpha - s2.log_alpha).abs() < 1.0e-7F)
}

///|
test "sac_auto_alpha_grows_when_policy_too_deterministic" {
  // target_entropy = -1.0 (more negative than uniform's mean_neg_entropy
  // ≈ -0.693); policy too random relative to target → α should GROW.
  let sac = SacAutoAlpha::new(4, 2, 0.0F, -1.0F, 0.5F, 11UL)
  // Force uniform π by zeroing weights.
  for i in 0..<sac.policy.n_actions {
    for j in 0..<sac.policy.n_states {
      sac.policy.w[i][j] = 0.0F
    }
  }
  let states : Array[Int] = [0, 1, 2, 3]
  let before = sac.log_alpha
  let delta = sac_auto_alpha_update_alpha(sac, states)
  assert_true(delta > 0.0F)
  assert_true(sac.log_alpha > before)
  assert_true(sac_auto_alpha_get(sac) > 1.0F)
}

///|
test "sac_auto_alpha_shrinks_when_target_above_policy_randomness" {
  // target_entropy = 0.0 (target: fully deterministic).
  // Uniform π has mean_neg_entropy ≈ -0.693 < 0 → α should SHRINK.
  let sac = SacAutoAlpha::new(4, 2, 0.0F, 0.0F, 0.5F, 22UL)
  // Force uniform π by zeroing weights.
  for i in 0..<sac.policy.n_actions {
    for j in 0..<sac.policy.n_states {
      sac.policy.w[i][j] = 0.0F
    }
  }
  let states : Array[Int] = [0, 1, 2, 3]
  let before = sac.log_alpha
  let delta = sac_auto_alpha_update_alpha(sac, states)
  assert_true(delta < 0.0F)
  assert_true(sac.log_alpha < before)
  assert_true(sac_auto_alpha_get(sac) < 1.0F)
  // And α should stay positive.
  assert_true(sac_auto_alpha_get(sac) > 0.0F)
}

///|
test "sac_auto_alpha_log_alpha_stays_finite" {
  let sac = SacAutoAlpha::new(4, 2, 0.0F, -1.0F, 0.5F, 33UL)
  let states : Array[Int] = [0, 1, 2, 3]
  for _iter in 0..<50 {
    let _ = sac_auto_alpha_update_alpha(sac, states)
    // NaN check via self-inequality.
    assert_true(sac.log_alpha == sac.log_alpha)
    let alpha = sac_auto_alpha_get(sac)
    assert_true(alpha == alpha)
    assert_true(alpha > 0.0F)
    // Loose sanity: α shouldn't overflow to infinity in 50 steps.
    // (Hard cap of 1e30 leaves plenty of headroom for high-lr runs
    // while still catching actual overflow.)
    assert_true(alpha < 1.0e30F)
  }
}

///|
test "sac_auto_alpha_train_returns_finite_mean" {
  // 2x2 grid → 4 states.
  let env = GridWorld::new(2, 2, 5)
  let sac = SacAutoAlpha::new(4, 4, 0.0F, -2.0F, 0.05F, 44UL)
  let mean = train_sac_auto_alpha(
    env, sac, 15, 0.99F, 0.05F, 0.02F, 0.05F, 50, 200, 2, 8, 55UL,
  )
  assert_true(mean == mean)  // NaN check
  assert_true(mean > -5.0F && mean < 5.0F)
}

///|
test "sac_auto_alpha_critic_update_non_nan" {
  let sac = SacAutoAlpha::new(4, 2, 0.0F, -1.0F, 0.1F, 66UL)
  let states : Array[Int] = [0, 1, 0, 1]
  let actions : Array[Int] = [0, 1, 1, 0]
  let rewards : Array[Float] = [1.0F, 0.0F, 1.0F, 0.5F]
  let next_states : Array[Int] = [1, 0, 1, 0]
  let dones : Array[Bool] = [false, false, false, false]
  let loss = sac_auto_alpha_critic_update(
    sac, states, actions, rewards, next_states, dones,
    0.99F, 0.05F,
  )
  assert_true(loss == loss)  // NaN check
  assert_true(loss > 0.0F)
}

///|
test "sac_auto_alpha_soft_update_moves_target_toward_online" {
  // After a Polyak update with τ = 0.1, the target should have moved
  // exactly 10% of the way from its initial value toward the new
  // online value.
  let sac = SacAutoAlpha::new(3, 2, 0.0F, -1.0F, 0.1F, 77UL)
  let w0 = sac.q1_target.w[0][0]
  // Mutate online weight via a critic step.
  let states : Array[Int] = [0]
  let actions : Array[Int] = [0]
  let rewards : Array[Float] = [0.0F]
  let next_states : Array[Int] = [1]
  let dones : Array[Bool] = [false]
  let _ = sac_auto_alpha_critic_update(
    sac, states, actions, rewards, next_states, dones,
    0.9F, 0.5F,
  )
  let w_after_online = sac.q1.w[0][0]
  // Soft-update with τ = 0.1.
  sac_auto_alpha_soft_update(sac, 0.1F)
  let w_after_target = sac.q1_target.w[0][0]
  let d1 = (w_after_target - w0).abs()
  assert_true(d1 == d1)  // NaN check
  let expected_d1 = 0.1F * (w_after_online - w0).abs()
  assert_true((d1 - expected_d1).abs() < 1.0e-5F)
}