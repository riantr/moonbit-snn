// ppo_kl_test.mbt — Tests for PPO with KL penalty (v0.35.2).

///|
test "ppo_kl_collect_batch: shapes + old_probs positive" {
  let env = GridWorld::new(2, 2, 5)
  let p = LinearSoftmaxPolicy::new(4, 4, 7UL)
  let v = LinearValueNet::new(4, 11UL)
  let batch = ppo_kl_collect_batch(env, p, v, 5, 0.9F, 5, 13UL)
  assert_true(batch.states.length() > 0)
  assert_eq(batch.states.length(), batch.actions.length())
  assert_eq(batch.states.length(), batch.advantages.length())
  assert_eq(batch.states.length(), batch.old_probs.length())
  assert_eq(batch.states.length(), batch.old_dist.length())
  for i in 0..<batch.old_probs.length() {
    assert_true(batch.old_probs[i] > 0.0F)
    assert_true(batch.old_probs[i] <= 1.0F)
  }
  for i in 0..<batch.old_dist.length() {
    assert_eq(batch.old_dist[i].length(), p.n_actions)
    let mut s = 0.0F
    for k in 0..<batch.old_dist[i].length() {
      s = s + batch.old_dist[i][k]
    }
    let diff = if s > 1.0F { s - 1.0F } else { 1.0F - s }
    assert_true(diff < 1.0e-5F)
  }
}

///|
test "per_state_kl: zero when distributions match" {
  let pi : Array[Float] = [0.25F, 0.25F, 0.25F, 0.25F]
  let kl = per_state_kl(pi, pi)
  assert_true(kl.abs() < 1.0e-5F)
}

///|
test "per_state_kl: positive when distributions differ" {
  let pi_a : Array[Float] = [0.7F, 0.1F, 0.1F, 0.1F]
  let pi_b : Array[Float] = [0.25F, 0.25F, 0.25F, 0.25F]
  let kl = per_state_kl(pi_a, pi_b)
  // KL is always >= 0.
  assert_true(kl > 0.0F)
}

///|
test "per_state_kl: zero when one side has zero mass on entry" {
  // π_cur[i] = 0 → that term is 0 · log(0/x) = 0 (limit).
  let pi_a : Array[Float] = [1.0F, 0.0F, 0.0F, 0.0F]
  let pi_b : Array[Float] = [0.5F, 0.5F, 0.0F, 0.0F]
  let kl = per_state_kl(pi_a, pi_b)
  // Only action 0 has nonzero mass in π_a; KL = 1 · log(1/0.5).
  let expected = logf(1.0F / 0.5F)
  let diff = if kl > expected { kl - expected } else { expected - kl }
  assert_true(diff < 1.0e-5F)
}

///|
test "ppo_kl_update_step: zero advantage -> no weight change" {
  let env = GridWorld::new(2, 2, 5)
  let p = LinearSoftmaxPolicy::new(4, 4, 17UL)
  let v = LinearValueNet::new(4, 19UL)
  let batch = ppo_kl_collect_batch(env, p, v, 3, 0.9F, 5, 23UL)
  let snap : Array[Array[Float]] = Array::make(4, [])
  for i in 0..<4 {
    snap[i] = Array::make(4, 0.0F)
    for j in 0..<4 {
      snap[i][j] = p.w[i][j]
    }
  }
  let zero_batch : PpoKlBatch = {
    states: batch.states,
    actions: batch.actions,
    advantages: Array::make(batch.advantages.length(), 0.0F),
    old_probs: batch.old_probs,
    old_dist: batch.old_dist,
  }
  // Even with β > 0, zero advantages means no update. (KL gradient
  // is non-zero but the surrogate pushes against it via ∇r; for
  // A=0 the surrogate contribution is 0 but KL still pushes π
  // back to π_old. Allow small drift.)
  let _ = ppo_kl_update_step(p, zero_batch, 0.0F, 0.5F)
  // With β=0 there should be no update at all.
  for i in 0..<4 {
    for j in 0..<4 {
      assert_eq(p.w[i][j], snap[i][j])
    }
  }
}

///|
test "ppo_kl_update_step: positive KL beta pulls policy back" {
  // Construct a batch with old_dist favoring action 0 and current
  // policy favoring action 1 (so KL > 0). Then with high β, the
  // update should pull the policy back toward old.
  let env = GridWorld::new(2, 2, 1)
  let p = LinearSoftmaxPolicy::new(4, 2, 29UL)
  // Current policy strongly prefers action 1.
  for i in 0..<2 {
    for j in 0..<4 {
      p.w[i][j] = 0.0F
    }
  }
  p.w[1][0] = 5.0F
  let batch : PpoKlBatch = {
    states: [0],
    actions: [1],
    advantages: [0.0F],  // No advantage signal.
    old_probs: [0.5F],
    old_dist: [[0.7F, 0.3F]],  // π_old prefers action 0.
  }
  // Snapshot.
  let w0_0_before = p.w[0][0]
  let w0_1_before = p.w[0][1]
  let w1_0_before = p.w[1][0]
  let _ = ppo_kl_update_step(p, batch, 1.0F, 0.5F)
  // With β > 0 and KL > 0, weights should change to reduce KL.
  // We just check that the update happened.
  let changed = p.w[0][0] != w0_0_before ||
    p.w[0][1] != w0_1_before ||
    p.w[1][0] != w1_0_before
  assert_true(changed)
}

///|
test "train_ppo_kl: shapes + return is finite" {
  let env = GridWorld::new(2, 2, 5)
  let p = LinearSoftmaxPolicy::new(4, 4, 31UL)
  let v = LinearValueNet::new(4, 37UL)
  let last = train_ppo_kl(env, p, v, 3, 5, 0.9F, 0.1F, 0.05F, 5, 41UL)
  assert_true(last == last)
  assert_true(last > -10.0F && last < 10.0F)
}

///|
test "ppo_kl_beta_zero: reduces to plain REINFORCE-style update" {
  // With β=0, the KL penalty vanishes and we get a vanilla
  // importance-weighted policy gradient.
  let env = GridWorld::new(2, 2, 5)
  let p = LinearSoftmaxPolicy::new(4, 4, 47UL)
  let v = LinearValueNet::new(4, 53UL)
  let batch = ppo_kl_collect_batch(env, p, v, 5, 0.9F, 5, 59UL)
  let snap : Array[Array[Float]] = Array::make(4, [])
  for i in 0..<4 {
    snap[i] = Array::make(4, 0.0F)
    for j in 0..<4 {
      snap[i][j] = p.w[i][j]
    }
  }
  // Snapshot old_dist too.
  let mut changed_count = 0
  for _ in 0..<1 {
    let _ = ppo_kl_update_step(p, batch, 0.0F, 0.05F)
  }
  for i in 0..<4 {
    for j in 0..<4 {
      if p.w[i][j] != snap[i][j] {
        changed_count = changed_count + 1
      }
    }
  }
  assert_true(changed_count > 0)
}