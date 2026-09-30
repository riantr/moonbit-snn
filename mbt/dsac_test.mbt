// dsac_test.mbt — Unit tests for Distributional SAC.

///|
test "gaussian_qnet_new_has_zero_bias_initially" {
  let q = LinearGaussianQNet::new(3, 2, 1UL)
  assert_eq(q.n_states, 3)
  assert_eq(q.n_actions, 2)
  // μ bias is small but not necessarily zero (jittered); log-σ bias is 0.
  for a in 0..<q.n_actions {
    assert_eq(q.b_sigma[a], 0.0F)
  }
}

///|
test "gaussian_q_forward_returns_correct_shapes" {
  let q = LinearGaussianQNet::new(4, 3, 2UL)
  let x : Array[Float] = [1.0F, 0.0F, 0.0F, 0.0F]
  let (mus, sigs) = gaussian_q_forward(q, x)
  assert_eq(mus.length(), 3)
  assert_eq(sigs.length(), 3)
  // σ must be in (0, 10] (clamp range).
  for a in 0..<3 {
    let s = sigs[a]
    assert_true(s > 0.0F)
    assert_true(s <= 10.0F)
  }
}

///|
test "gaussian_q_forward_zero_input_gives_bias_only" {
  let q = LinearGaussianQNet::new(3, 2, 3UL)
  // Set all weights to zero so output = bias only.
  for a in 0..<q.n_actions {
    for s in 0..<q.n_states {
      q.w_mu[a][s] = 0.0F
      q.w_sigma[a][s] = 0.0F
    }
  }
  q.b_mu[0] = 1.5F
  q.b_mu[1] = -2.0F
  q.b_sigma[0] = 0.0F // σ = exp(0) = 1
  q.b_sigma[1] = 1.6094F // ≈ ln(5) → σ ≈ 5
  let x : Array[Float] = [0.0F, 0.0F, 0.0F]
  let (mus, sigs) = gaussian_q_forward(q, x)
  let mu0_diff = if mus[0] > 1.5F { mus[0] - 1.5F } else { 1.5F - mus[0] }
  let mu1_diff = if mus[1] > -2.0F { mus[1] - -2.0F } else { -2.0F - mus[1] }
  assert_true(mu0_diff < 1.0e-4F)
  assert_true(mu1_diff < 1.0e-4F)
  let s0_diff = if sigs[0] > 1.0F { sigs[0] - 1.0F } else { 1.0F - sigs[0] }
  assert_true(s0_diff < 1.0e-3F)
  // σ for a=1 should be ≈ 5.
  let s1_diff = if sigs[1] > 5.0F { sigs[1] - 5.0F } else { 5.0F - sigs[1] }
  assert_true(s1_diff < 0.1F)
}

///|
test "gaussian_nll_zero_diff_zero_sigma_baseline" {
  // NLL at y=μ, σ=1 is 0.5·log(2π) ≈ 0.9189.
  let nll = gaussian_nll(1.0F, 1.0F, 1.0F)
  let expected = 0.9189F
  let diff = if nll > expected { nll - expected } else { expected - nll }
  assert_true(diff < 0.01F)
}

///|
test "gaussian_nll_increases_with_residual" {
  let nll_close = gaussian_nll(1.05F, 1.0F, 1.0F)
  let nll_far = gaussian_nll(3.0F, 1.0F, 1.0F)
  assert_true(nll_far > nll_close)
}

///|
test "gaussian_qnet_copy_copies_all_arrays" {
  let src = LinearGaussianQNet::new(3, 2, 4UL)
  let dst = LinearGaussianQNet::new(3, 2, 5UL)
  // Mutate src.
  src.w_mu[0][1] = 7.5F
  src.b_sigma[1] = 0.5F
  gaussian_qnet_copy(dst, src)
  assert_eq(dst.w_mu[0][1], 7.5F)
  assert_eq(dst.b_sigma[1], 0.5F)
}

///|
test "dsac_new_targets_match_online_initially" {
  let agent = DSac::new(4, 3, 0.5F, 7UL)
  // After construction, target weights match online weights.
  for a in 0..<agent.q1.n_actions {
    for s in 0..<agent.q1.n_states {
      assert_eq(agent.q1.w_mu[a][s], agent.q1_target.w_mu[a][s])
      assert_eq(agent.q2.w_sigma[a][s], agent.q2_target.w_sigma[a][s])
    }
  }
}

///|
test "dsac_critic_update_changes_mu_and_sigma" {
  let agent = DSac::new(3, 2, 0.5F, 11UL)
  let states : Array[Int] = [0, 1, 2]
  let actions : Array[Int] = [0, 1, 0]
  let targets : Array[Float] = [2.0F, -1.0F, 0.5F]
  let mu_before_0 = agent.q1.w_mu[0][0]
  let sigma_before_0 = agent.q1.b_sigma[0]
  let nll = dsac_critic_update(agent, states, actions, targets, 0.05F)
  let _ = nll
  // Verify weights changed (gradient flow check).
  let mu_after_0 = agent.q1.w_mu[0][0]
  let sigma_after_0 = agent.q1.b_sigma[0]
  assert_true(mu_after_0 != mu_before_0)
  assert_true(sigma_after_0 != sigma_before_0)
}

///|
test "dsac_soft_update_convex_combination" {
  let agent = DSac::new(2, 2, 0.5F, 13UL)
  // Force online weights to specific values.
  agent.q1.w_mu[0][0] = 10.0F
  agent.q1_target.w_mu[0][0] = 0.0F
  dsac_soft_update(agent, 0.5F)
  // τ=0.5: target ← 0.5·0 + 0.5·10 = 5.
  let new_target = agent.q1_target.w_mu[0][0]
  let diff = if new_target > 5.0F { new_target - 5.0F } else { 5.0F - new_target }
  assert_true(diff < 1.0e-5F)
}

///|
test "dsac_soft_target_zero_when_done" {
  let agent = DSac::new(3, 2, 0.1F, 17UL)
  let rng = Xoshiro::from_state(1UL, 2UL, 3UL, 4UL)
  // When done=true, target is exactly reward (no bootstrap).
  let target_val = dsac_soft_target(
    agent.q1_target, agent.q2_target, agent.policy, agent.alpha,
    0, 7.0F, true, 0.9F, rng,
  )
  let diff = if target_val > 7.0F { target_val - 7.0F } else { 7.0F - target_val }
  assert_true(diff < 1.0e-5F)
}

///|
test "dsac_soft_target_finite_when_not_done" {
  let agent = DSac::new(3, 2, 0.1F, 19UL)
  let rng = Xoshiro::from_state(1UL, 2UL, 3UL, 4UL)
  let target_val = dsac_soft_target(
    agent.q1_target, agent.q2_target, agent.policy, agent.alpha,
    0, 1.0F, false, 0.9F, rng,
  )
  assert_true(target_val == target_val) // not NaN
  assert_true(target_val < 1.0e6F)
}

///|
test "dsac_sample_action_returns_valid_action" {
  let agent = DSac::new(3, 2, 0.1F, 23UL)
  let rng = Xoshiro::from_state(1UL, 2UL, 3UL, 4UL)
  for _k in 0..<10 {
    let a = dsac_sample_action(agent, 0, rng)
    assert_true(a >= 0 && a < 2)
  }
}