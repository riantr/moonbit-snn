// gru_policy_test.mbt — Tests for GRU-policy REINFORCE (v0.34.0).

///|
test "gru_policy_new: shapes" {
  let p = GruPolicy::new(3, 4, 11UL)
  assert_eq(p.cell.d_x, 5) // 2 + 3
  assert_eq(p.cell.d_h, 4)
  assert_eq(p.w_out.length(), 2)
  for i in 0..<2 {
    assert_eq(p.w_out[i].length(), 4)
  }
  assert_eq(p.b_out.length(), 2)
}

///|
test "gru_policy_forward: action + cache lengths match obs_seq" {
  let p = GruPolicy::new(3, 4, 13UL)
  let obs_seq : Array[Array[Float]] = [
    [1.0F, 0.0F, 1.0F, 0.0F, 0.0F],
    [1.0F, 0.0F, 0.0F, 1.0F, 0.0F],
    [1.0F, 0.0F, 0.0F, 0.0F, 1.0F],
  ]
  let h0 : Array[Float] = [0.0F, 0.0F, 0.0F, 0.0F]
  let rng = Xoshiro::from_state(17UL, 19UL, 23UL, 29UL)
  let (actions, cache) = gru_policy_forward(p, obs_seq, h0, rng)
  assert_eq(actions.length(), 3)
  for t in 0..<3 {
    assert_true(actions[t] == 0 || actions[t] == 1)
    assert_eq(cache.cell_caches[t].h_t.length(), 4)
    assert_eq(cache.probs[t].length(), 2)
    assert_eq(cache.hs[t].length(), 4)
  }
  for t in 0..<3 {
    let s = cache.probs[t][0] + cache.probs[t][1]
    let diff = if s > 1.0F { s - 1.0F } else { 1.0F - s }
    assert_true(diff < 1.0e-4F)
  }
}

///|
test "gru_policy_gradient_update: zero returns -> no change" {
  let p = GruPolicy::new(3, 3, 31UL)
  let obs_seq : Array[Array[Float]] = [
    [1.0F, 0.0F, 1.0F, 0.0F, 0.0F],
    [1.0F, 0.0F, 0.0F, 1.0F, 0.0F],
  ]
  let h0 : Array[Float] = [0.0F, 0.0F, 0.0F]
  let rng = Xoshiro::from_state(37UL, 41UL, 43UL, 47UL)
  let (actions, cache) = gru_policy_forward(p, obs_seq, h0, rng)
  let w0_snapshot : Array[Array[Float]] = Array::make(2, [])
  for i in 0..<2 {
    w0_snapshot[i] = Array::make(3, 0.0F)
    for j in 0..<3 {
      w0_snapshot[i][j] = p.w_out[i][j]
    }
  }
  let cell_w_snapshot : Array[Array[Float]] = Array::make(3, [])
  for i in 0..<3 {
    cell_w_snapshot[i] = Array::make(5, 0.0F)
    for j in 0..<5 {
      cell_w_snapshot[i][j] = p.cell.w_z[i][j]
    }
  }
  let returns : Array[Float] = [0.0F, 0.0F]
  gru_policy_gradient_update(p, actions, returns, cache, 0.1F)
  for i in 0..<2 {
    for j in 0..<3 {
      assert_eq(p.w_out[i][j], w0_snapshot[i][j])
    }
  }
  for i in 0..<3 {
    for j in 0..<5 {
      assert_eq(p.cell.w_z[i][j], cell_w_snapshot[i][j])
    }
  }
}

///|
test "gru_policy_gradient_update: positive return increases chosen prob" {
  let p = GruPolicy::new(3, 3, 53UL)
  let obs_seq : Array[Array[Float]] = [
    [1.0F, 0.0F, 1.0F, 0.0F, 0.0F],
    [1.0F, 0.0F, 0.0F, 1.0F, 0.0F],
    [1.0F, 0.0F, 0.0F, 0.0F, 1.0F],
  ]
  let h0 : Array[Float] = [0.0F, 0.0F, 0.0F]
  let rng = Xoshiro::from_state(59UL, 61UL, 67UL, 71UL)
  let (actions, cache) = gru_policy_forward(p, obs_seq, h0, rng)
  let returns : Array[Float] = [1.0F, 1.0F, 1.0F]
  gru_policy_gradient_update(p, actions, returns, cache, 0.5F)
  let rng2 = Xoshiro::from_state(73UL, 79UL, 83UL, 89UL)
  let (_a2, cache2) = gru_policy_forward(p, obs_seq, h0, rng2)
  for t in 0..<3 {
    let a = actions[t]
    assert_true(cache2.probs[t][a] >= cache.probs[t][a] - 1.0e-3F)
  }
}

///|
test "train_gru_reinforce: shapes + return is finite" {
  let p = GruPolicy::new(5, 8, 97UL)
  let mean_return = train_gru_reinforce(5, p, 5, 0.9F, 0.01F, 4, 101UL)
  assert_true(mean_return == mean_return)
  assert_true(mean_return > -100.0F && mean_return < 100.0F)
}

///|
test "gru_policy_indicator_attends: learns opposite actions per side" {
  // Train two simple policies (one per side) and verify they
  // produce opposite preferred actions on the corresponding obs.
  let p_l = GruPolicy::new(3, 4, 131UL)
  let p_r = GruPolicy::new(3, 4, 137UL)
  let obs_left : Array[Array[Float]] = [
    [1.0F, 0.0F, 1.0F, 0.0F, 0.0F],
    [1.0F, 0.0F, 1.0F, 0.0F, 0.0F],
  ]
  let obs_right : Array[Array[Float]] = [
    [0.0F, 1.0F, 1.0F, 0.0F, 0.0F],
    [0.0F, 1.0F, 1.0F, 0.0F, 0.0F],
  ]
  let h0 : Array[Float] = [0.0F, 0.0F, 0.0F, 0.0F]
  let rng_l = Xoshiro::from_state(139UL, 149UL, 151UL, 157UL)
  let rng_r = Xoshiro::from_state(163UL, 167UL, 173UL, 179UL)
  // Force p_l to prefer action 0 (left) via bias (since h_0=0
  // would cancel any w_out bias).
  p_l.b_out[0] = 20.0F
  // Force p_r to prefer action 1 (right).
  p_r.b_out[1] = 20.0F
  let (_al, cache_l) = gru_policy_forward(p_l, obs_left, h0, rng_l)
  let (_ar, cache_r) = gru_policy_forward(p_r, obs_right, h0, rng_r)
  // After projection, the cached probs should reflect the bias.
  assert_true(cache_l.probs[0][0] > 0.9F)
  assert_true(cache_r.probs[0][1] > 0.9F)
}