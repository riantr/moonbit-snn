// recurrent_sac_test.mbt — tests for v0.38.2 recurrent SAC.
//
// Coverage:
//   1. Construction + initial α = exp(log_alpha_init).
//   2. Determinism: same seed → identical init state.
//   3. α grows when target_entropy more negative than uniform's
//      mean_neg_entropy (policy too random relative to target).
//   4. α shrinks when target_entropy > uniform's mean_neg_entropy.
//   5. log_alpha stays finite after many episodes.
//   6. End-to-end train_recurrent_sac returns a finite mean return.
//   7. Critic update produces finite, positive loss.
//   8. LstmQNet forward returns correct shape (n_steps × n_actions).

///|
test "recurrent_sac_init" {
  let sac = RecurrentSac::new(4, 4, 0.0F, -1.0F, 0.1F, 42UL)
  // α = exp(0) = 1.0
  assert_true((recurrent_sac_get_alpha(sac) - 1.0F).abs() < 1.0e-5F)
  // LstmPolicy and LstmQNet shapes.
  assert_eq(sac.policy.cell.d_h, 4)
  assert_eq(sac.q1.cell.d_h, 4)
  assert_eq(sac.q1.w_out.length(), 2)
  // Target critics synced to online.
  for i in 0..<sac.q1.w_out.length() {
    for j in 0..<sac.q1.w_out[i].length() {
      assert_true(
        (sac.q1.w_out[i][j] - sac.q1_target.w_out[i][j]).abs() < 1.0e-7F,
      )
    }
    assert_true(
      (sac.q1.b_out[i] - sac.q1_target.b_out[i]).abs() < 1.0e-7F,
    )
  }
}

///|
test "recurrent_sac_determinism" {
  let s1 = RecurrentSac::new(4, 4, 0.0F, -1.0F, 0.1F, 7UL)
  let s2 = RecurrentSac::new(4, 4, 0.0F, -1.0F, 0.1F, 7UL)
  assert_eq(s1.q1.w_out.length(), s2.q1.w_out.length())
  assert_eq(s1.policy.cell.d_h, s2.policy.cell.d_h)
  assert_true(
    (s1.policy.w_out[0][0] - s2.policy.w_out[0][0]).abs() < 1.0e-7F,
  )
}

///|
fn make_zero_episode(n_cells : Int, n_steps : Int) -> RecurrentSacEpisode {
  let obs : Array[Array[Float]] = []
  let obs2 : Array[Array[Float]] = []
  let acts : Array[Int] = []
  let rews : Array[Float] = []
  let dones : Array[Bool] = []
  for _t in 0..<n_steps {
    let o : Array[Float] = Array::make(2 + n_cells, 0.0F)
    obs.push(o)
    let o2 : Array[Float] = Array::make(2 + n_cells, 0.0F)
    obs2.push(o2)
    acts.push(0)
    rews.push(0.0F)
    dones.push(false)
  }
  RecurrentSacEpisode::new(obs, obs2, acts, rews, dones, 2, 1)
}

///|
test "recurrent_sac_alpha_grows_when_policy_too_random" {
  // target_entropy = -1.0; uniform over 2 actions has
  // mean_neg_entropy ≈ log(0.5) ≈ -0.693; delta > 0 → α grows.
  let sac = RecurrentSac::new(4, 4, 0.0F, -1.0F, 0.3F, 11UL)
  // Zero out policy.w_out to force uniform π.
  for i in 0..<sac.policy.w_out.length() {
    for j in 0..<sac.policy.cell.d_h {
      sac.policy.w_out[i][j] = 0.0F
    }
    sac.policy.b_out[i] = 0.0F
  }
  let ep = make_zero_episode(4, 3)
  let rng = Xoshiro::from_state(1UL, 2UL, 3UL, 4UL)
  let before = sac.log_alpha
  let delta = recurrent_sac_update_alpha(sac, ep, rng)
  assert_true(delta > 0.0F)
  assert_true(sac.log_alpha > before)
  assert_true(recurrent_sac_get_alpha(sac) > 1.0F)
}

///|
test "recurrent_sac_alpha_shrinks_when_policy_too_deterministic" {
  // target_entropy = 0.0; uniform π has mean_neg_entropy ≈ -0.693 < 0
  // → δ < 0 → α shrinks.
  let sac = RecurrentSac::new(4, 4, 0.0F, 0.0F, 0.3F, 22UL)
  for i in 0..<sac.policy.w_out.length() {
    for j in 0..<sac.policy.cell.d_h {
      sac.policy.w_out[i][j] = 0.0F
    }
    sac.policy.b_out[i] = 0.0F
  }
  let ep = make_zero_episode(4, 3)
  let rng = Xoshiro::from_state(1UL, 2UL, 3UL, 4UL)
  let before = sac.log_alpha
  let delta = recurrent_sac_update_alpha(sac, ep, rng)
  assert_true(delta < 0.0F)
  assert_true(sac.log_alpha < before)
  assert_true(recurrent_sac_get_alpha(sac) < 1.0F)
  assert_true(recurrent_sac_get_alpha(sac) > 0.0F)
}

///|
test "recurrent_sac_log_alpha_stays_finite" {
  let sac = RecurrentSac::new(4, 4, 0.0F, -1.0F, 0.3F, 33UL)
  let ep = make_zero_episode(4, 3)
  let rng = Xoshiro::from_state(1UL, 2UL, 3UL, 4UL)
  for _iter in 0..<30 {
    let _ = recurrent_sac_update_alpha(sac, ep, rng)
    assert_true(sac.log_alpha == sac.log_alpha)  // NaN check
    let alpha = recurrent_sac_get_alpha(sac)
    assert_true(alpha == alpha)  // NaN check
    assert_true(alpha > 0.0F)
    assert_true(alpha < 1.0e30F)
  }
}

///|
test "recurrent_sac_train_returns_finite_mean" {
  let sac = RecurrentSac::new(4, 4, 0.0F, -1.5F, 0.05F, 44UL)
  let ret = train_recurrent_sac(
    4, sac, 5, 0.9F, 0.02F, 0.01F, 0.1F, 8, 55UL,
  )
  assert_true(ret == ret)  // NaN check
  assert_true(ret > -5.0F && ret < 5.0F)
}

///|
test "recurrent_sac_critic_update_produces_positive_loss" {
  let sac = RecurrentSac::new(4, 4, 0.0F, -1.0F, 0.1F, 66UL)
  // Build a fake 3-step episode with non-trivial rewards.
  let obs : Array[Array[Float]] = []
  let obs2 : Array[Array[Float]] = []
  let acts : Array[Int] = []
  let rews : Array[Float] = []
  let dones : Array[Bool] = []
  for _t in 0..<3 {
    let o : Array[Float] = Array::make(6, 0.0F)
    obs.push(o)
    let o2 : Array[Float] = Array::make(6, 0.0F)
    obs2.push(o2)
    acts.push(0)
    rews.push(1.0F)
    dones.push(false)
  }
  let ep = RecurrentSacEpisode::new(obs, obs2, acts, rews, dones, 2, 1)
  let rng = Xoshiro::from_state(1UL, 2UL, 3UL, 4UL)
  let loss = recurrent_sac_critic_update(sac, ep, 0.9F, 0.05F, rng)
  assert_true(loss == loss)  // NaN check
  assert_true(loss > 0.0F)
}

///|
test "lstm_qnet_forward_returns_correct_shape" {
  // Build an LstmQNet and run a 4-step forward pass.
  let qnet = LstmQNet::new(6, 3, 2, 99UL)
  let obs_seq : Array[Array[Float]] = []
  for _t in 0..<4 {
    obs_seq.push(Array::make(6, 0.1F))
  }
  let h0 : Array[Float] = Array::make(3, 0.0F)
  let c0 : Array[Float] = Array::make(3, 0.0F)
  let (qs, cache) = lstm_qnet_forward(qnet, obs_seq, h0, c0)
  assert_eq(qs.length(), 4)
  assert_eq(cache.hs.length(), 4)
  assert_eq(cache.cell_caches.length(), 4)
  for t in 0..<4 {
    assert_eq(qs[t].length(), 2)
    assert_true(qs[t][0] == qs[t][0])  // NaN check on Q values
    assert_true(qs[t][1] == qs[t][1])
  }
}