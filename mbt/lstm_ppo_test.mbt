// lstm_ppo_test.mbt — Tests for recurrent PPO with LSTM policy (v0.35.3).

///|
test "lstm_ppo_collect_batch: shapes + per-step advantages" {
  let p = LstmPolicy::new(5, 4, 7UL)
  let batch = lstm_ppo_collect_batch(5, p, 4, 0.9F, 4, 11UL)
  assert_true(batch.states.length() > 0)
  assert_eq(batch.states.length(), batch.actions.length())
  assert_eq(batch.states.length(), batch.advantages.length())
  assert_eq(batch.states.length(), batch.old_probs.length())
  assert_eq(batch.states.length(), batch.cur_probs.length())
  assert_eq(batch.states.length(), batch.cur_dist.length())
  assert_eq(batch.states.length(), batch.old_dist.length())
  // Old and current probs are initially equal (single-epoch PPO).
  for i in 0..<batch.old_probs.length() {
    assert_eq(batch.old_probs[i], batch.cur_probs[i])
  }
}

///|
test "lstm_ppo_collect_batch: cur_probs sum to 1 + finite" {
  let p = LstmPolicy::new(5, 4, 13UL)
  let batch = lstm_ppo_collect_batch(5, p, 3, 0.9F, 4, 17UL)
  for i in 0..<batch.cur_dist.length() {
    let mut s = 0.0F
    for k in 0..<batch.cur_dist[i].length() {
      assert_true(batch.cur_dist[i][k] > 0.0F)
      assert_true(batch.cur_dist[i][k] <= 1.0F)
      s = s + batch.cur_dist[i][k]
    }
    let diff = if s > 1.0F { s - 1.0F } else { 1.0F - s }
    assert_true(diff < 1.0e-4F)
  }
}

///|
test "lstm_ppo_update_step: weights bounded after update" {
  let p = LstmPolicy::new(5, 4, 23UL)
  let batch = lstm_ppo_collect_batch(5, p, 3, 0.9F, 4, 29UL)
  let snap : Array[Array[Float]] = Array::make(2, [])
  for i in 0..<2 {
    snap[i] = Array::make(4, 0.0F)
    for j in 0..<4 {
      snap[i][j] = p.w_out[i][j]
    }
  }
  let _ = lstm_ppo_update_step(p, batch, 0.2F, 0.05F)
  for i in 0..<2 {
    for j in 0..<4 {
      let diff = if p.w_out[i][j] > snap[i][j] {
        p.w_out[i][j] - snap[i][j]
      } else {
        snap[i][j] - p.w_out[i][j]
      }
      // Small lr + clipping keeps updates bounded.
      assert_true(diff < 1.0F)
    }
  }
}

///|
test "lstm_ppo_update_step: zero advantage + clip active -> no-op" {
  // With single-pass PPO and advantage=0, scale=0 -> no update.
  let p = LstmPolicy::new(3, 3, 31UL)
  let snap : Array[Array[Float]] = Array::make(2, [])
  for i in 0..<2 {
    snap[i] = Array::make(3, 0.0F)
    for j in 0..<3 {
      snap[i][j] = p.w_out[i][j]
    }
  }
  let batch : RecurrentPpoBatch = {
    states: [0, 0],
    actions: [0, 1],
    advantages: [0.0F, 0.0F],
    old_probs: [0.5F, 0.5F],
    cur_probs: [0.5F, 0.5F],
    cur_dist: [[0.5F, 0.5F], [0.5F, 0.5F]],
    old_dist: [[0.5F, 0.5F], [0.5F, 0.5F]],
  }
  let _ = lstm_ppo_update_step(p, batch, 0.2F, 0.5F)
  for i in 0..<2 {
    for j in 0..<3 {
      assert_eq(p.w_out[i][j], snap[i][j])
    }
  }
}

///|
test "lstm_ppo_update_step: KL returns finite non-negative" {
  let p = LstmPolicy::new(5, 4, 37UL)
  let batch = lstm_ppo_collect_batch(5, p, 3, 0.9F, 4, 41UL)
  let kl = lstm_ppo_update_step(p, batch, 0.2F, 0.05F)
  assert_true(kl >= 0.0F)
  assert_true(kl < 100.0F)
}

///|
test "train_lstm_ppo: shapes + mean is finite" {
  let p = LstmPolicy::new(5, 4, 47UL)
  let last = train_lstm_ppo(5, p, 3, 4, 0.9F, 0.2F, 0.01F, 4, 53UL)
  assert_true(last == last)
  assert_true(last > -10.0F && last < 10.0F)
}