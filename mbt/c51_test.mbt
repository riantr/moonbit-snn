// c51_test.mbt — tests for v0.39.0 Categorical DQN (C51).
//
// Coverage:
//   1. Support array: n_atoms entries, monotonic in [V_min, V_max].
//   2. Forward: probs sum to 1 per action (atom dimension is softmax).
//   3. Forward: zero-init net → uniform distribution (logits = bias).
//   4. Projected target with done=True → delta distribution at reward.
//   5. Projected target with done=False → mass preserved (sums to 1).
//   6. Projected target: clamped to [V_min, V_max] when r + γ·z exceeds range.
//   7. Update: loss is finite + positive; bias changes.
//   8. Soft target update: τ=0 leaves target untouched; τ=1 copies online.
//   9. Determinism: same seed → identical forward.
//  10. Greedy action: returns valid action in [0, n_actions).

///|
test "c51_support_monotonic" {
  let sup = c51_support(-10.0F, 10.0F, 51)
  assert_eq(sup.length(), 51)
  assert_true((sup[0] - -10.0F).abs() < 1.0e-5F)
  assert_true((sup[50] - 10.0F).abs() < 1.0e-5F)
  // Monotonic non-decreasing.
  for i in 0..<50 {
    assert_true(sup[i + 1] >= sup[i])
  }
  // Δz = 20 / 50 = 0.4.
  assert_true((sup[1] - sup[0] - 0.4F).abs() < 1.0e-5F)
}

///|
test "c51_forward_probs_sum_to_one" {
  let c51 = C51::new(
    4, 3, 11, -5.0F, 5.0F, 0.9F, 0.01F, 42UL,
  )
  let probs = c51_forward(c51.net, 1)
  assert_eq(probs.length(), 3)
  for a in 0..<3 {
    assert_eq(probs[a].length(), 11)
    let mut s = 0.0F
    for i in 0..<11 {
      s = s + probs[a][i]
    }
    assert_true((s - 1.0F).abs() < 1.0e-5F)
  }
}

///|
test "c51_forward_zero_init_is_uniform" {
  let net = C51Net::new(3, 2, 5, -1.0F, 1.0F, 99UL)
  for a in 0..<net.n_actions {
    for s in 0..<net.n_states {
      for i in 0..<net.n_atoms {
        net.w[a][s][i] = 0.0F
      }
    }
    for i in 0..<net.n_atoms {
      net.b[a][i] = 0.0F
    }
  }
  let probs = c51_forward(net, 0)
  for a in 0..<2 {
    for i in 0..<5 {
      assert_true((probs[a][i] - 0.2F).abs() < 1.0e-5F)
    }
  }
}

///|
test "c51_target_done_is_delta_distribution" {
  let c51 = C51::new(3, 2, 11, -5.0F, 5.0F, 0.9F, 0.01F, 7UL)
  // reward = 1.0; delta_z = 1.0 (since V_max - V_min = 10, n_atoms-1 = 10).
  // b = (1 - (-5)) / 1.0 = 6. lo_clamped = 6, hi = 7.
  // m[6] = 1 - 0 = 1.0, m[7] = 0. So m[6] = 1.
  let m = c51_projected_target(c51, 1.0F, 1, true)
  let mut s = 0.0F
  for i in 0..<11 {
    s = s + m[i]
  }
  assert_true((s - 1.0F).abs() < 1.0e-5F)
  assert_true(m[6] > 0.99F)
}

///|
test "c51_target_not_done_preserves_mass" {
  let c51 = C51::new(3, 2, 11, -5.0F, 5.0F, 0.9F, 0.01F, 7UL)
  // Build a uniform target distribution.
  for a in 0..<c51.net_target.n_actions {
    for s in 0..<c51.net_target.n_states {
      for i in 0..<c51.net_target.n_atoms {
        c51.net_target.w[a][s][i] = 0.0F
      }
    }
    for i in 0..<c51.net_target.n_atoms {
      c51.net_target.b[a][i] = 0.0F
    }
  }
  let m = c51_projected_target(c51, 0.0F, 0, false)
  let mut s = 0.0F
  for i in 0..<11 {
    s = s + m[i]
  }
  assert_true((s - 1.0F).abs() < 1.0e-5F)
  for i in 0..<11 {
    assert_true(m[i] >= 0.0F)
  }
}

///|
test "c51_target_clamps_to_support" {
  let c51 = C51::new(3, 2, 11, -1.0F, 1.0F, 0.9F, 0.01F, 7UL)
  for a in 0..<c51.net_target.n_actions {
    for s in 0..<c51.net_target.n_states {
      for i in 0..<c51.net_target.n_atoms {
        c51.net_target.w[a][s][i] = 0.0F
      }
    }
    for i in 0..<c51.net_target.n_atoms {
      c51.net_target.b[a][i] = 0.0F
    }
  }
  // reward = 100, V_max = 1.0 → Tz_j = 100 + 0.9 · z_j, all > 1.0,
  // so all clamp to V_max = 1.0 → b = (1 - (-1)) / 0.2 = 10.
  // lo_clamped = min(10, 9) = 9, hi = 10, frac = 0.
  // So m[9] gets all mass.
  let m = c51_projected_target(c51, 100.0F, 0, false)
  let mut s = 0.0F
  for i in 0..<11 {
    s = s + m[i]
  }
  assert_true((s - 1.0F).abs() < 1.0e-5F)
  let mut upper = 0.0F
  for i in 7..<11 {
    upper = upper + m[i]
  }
  assert_true(upper > 0.9F)
}

///|
test "c51_update_loss_finite_positive" {
  let c51 = C51::new(
    4, 2, 11, -5.0F, 5.0F, 0.9F, 0.01F, 11UL,
  )
  let states : Array[Int] = [0, 1, 2, 3]
  let actions : Array[Int] = [0, 1, 0, 1]
  let rewards : Array[Float] = [1.0F, 0.5F, -1.0F, 0.0F]
  let next_states : Array[Int] = [1, 0, 3, 2]
  let dones : Array[Bool] = [false, false, true, false]
  let loss = c51_update(c51, states, actions, rewards, next_states, dones)
  assert_true(loss == loss)  // NaN check
  assert_true(loss > 0.0F)
  // Bias should have changed.
  assert_true(c51.net.b[0][0] != 0.0F || c51.net.b[1][0] != 0.0F)
}

///|
test "c51_soft_update_tau_zero_no_change" {
  let c51 = C51::new(3, 2, 5, -1.0F, 1.0F, 0.9F, 0.01F, 17UL)
  let w_before = c51.net_target.w[0][0][0]
  c51_soft_update(c51, 0.0F)
  let w_after = c51.net_target.w[0][0][0]
  assert_true((w_before - w_after).abs() < 1.0e-7F)
}

///|
test "c51_soft_update_tau_one_copies_online" {
  let c51 = C51::new(3, 2, 5, -1.0F, 1.0F, 0.9F, 0.01F, 17UL)
  c51.net.w[0][0][0] = 1.23F
  c51.net_target.w[0][0][0] = 0.0F
  c51_soft_update(c51, 1.0F)
  assert_true((c51.net_target.w[0][0][0] - 1.23F).abs() < 1.0e-5F)
}

///|
test "c51_deterministic_forward" {
  let c51 = C51::new(4, 3, 7, -1.0F, 1.0F, 0.9F, 0.01F, 99UL)
  let p1 = c51_forward(c51.net, 2)
  let p2 = c51_forward(c51.net, 2)
  for a in 0..<3 {
    for i in 0..<7 {
      assert_true((p1[a][i] - p2[a][i]).abs() < 1.0e-7F)
    }
  }
}

///|
test "c51_greedy_action_valid" {
  let c51 = C51::new(4, 3, 5, -1.0F, 1.0F, 0.9F, 0.01F, 33UL)
  let a = c51_greedy_action(c51, 2)
  assert_true(a >= 0 && a < 3)
}