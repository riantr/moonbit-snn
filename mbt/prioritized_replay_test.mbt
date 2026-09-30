// prioritized_replay_test.mbt — Unit tests for the PER buffer.

///|
test "per_new_is_empty" {
  let buf = PrioritizedReplayBuffer::new(8, 0.6F, 0.4F, 0.01F)
  assert_eq(buf.len(), 0)
  assert_eq(buf.capacity(), 8)
  assert_eq(buf.total_priority(), 0.0F)
  assert_eq(buf.max_priority(), 1.0F)
}

///|
test "per_push_grows_size" {
  let buf = PrioritizedReplayBuffer::new(8, 0.6F, 0.4F, 0.01F)
  buf.push(0, 1, 1.0F, 1, false)
  buf.push(1, 0, 1.0F, 2, false)
  buf.push(2, 2, 1.0F, 3, true)
  assert_eq(buf.len(), 3)
  let total = buf.total_priority()
  // Three transitions inserted at max_priority = 1.0; total = 3.
  let diff = if total > 3.0F { total - 3.0F } else { 3.0F - total }
  assert_true(diff < 1.0e-5F)
}

///|
test "per_push_overwrites_when_full" {
  let buf = PrioritizedReplayBuffer::new(4, 0.6F, 0.4F, 0.01F)
  // Fill.
  for i in 0..<4 {
    buf.push(i, 0, 0.0F, i + 1, false)
  }
  assert_eq(buf.len(), 4)
  // Push more; size should stay at capacity.
  for i in 4..<7 {
    buf.push(i, 1, 1.0F, i + 1, false)
  }
  assert_eq(buf.len(), 4)
}

///|
test "per_sample_returns_batch_shapes" {
  let buf = PrioritizedReplayBuffer::new(16, 0.6F, 0.4F, 0.01F)
  let rng = Xoshiro::from_state(1UL, 2UL, 3UL, 4UL)
  for i in 0..<8 {
    buf.push(i % 5, i % 4, Float::from_int(i), (i + 1) % 5, i % 3 == 0)
  }
  let (ss, aa, rr, nss, dd, idx, w, probs) = buf.sample(4, rng)
  assert_eq(ss.length(), 4)
  assert_eq(aa.length(), 4)
  assert_eq(rr.length(), 4)
  assert_eq(nss.length(), 4)
  assert_eq(dd.length(), 4)
  assert_eq(idx.length(), 4)
  assert_eq(w.length(), 4)
  assert_eq(probs.length(), 4)
  // Indices must be in [0, size).
  for k in 0..<4 {
    let i = idx[k]
    assert_true(i >= 0 && i < 8)
  }
}

///|
test "per_sample_uniform_distribution" {
  // alpha = 0 → sampling must be uniform regardless of stored priorities.
  let buf = PrioritizedReplayBuffer::new(8, 0.0F, 0.4F, 0.01F)
  // Insert 4 transitions with sharply different priorities (would matter under
  // alpha > 0 but should be uniform at alpha = 0).
  buf.push(0, 0, 0.0F, 1, false)
  buf.push(1, 1, 0.0F, 2, false)
  buf.push(2, 2, 0.0F, 3, false)
  buf.push(3, 3, 0.0F, 0, false)
  let rng = Xoshiro::from_state(42UL, 7UL, 13UL, 19UL)
  let (ss, _aa, _rr, _nss, _dd, _idx, _w, probs) = buf.sample(4, rng)
  // Under alpha=0, each sample's probability should be ≈ 1/N = 0.25.
  let one_over_n = 1.0F / Float::from_int(buf.len())
  for k in 0..<4 {
    let p = probs[k]
    let diff = if p > one_over_n {
      p - one_over_n
    } else {
      one_over_n - p
    }
    assert_true(diff < 1.0e-3F)
    // States are drawn from {0, 1, 2, 3}.
    let _ = ss[k]
  }
}

///|
test "per_sample_high_priority_more_frequent" {
  // alpha = 1.0, beta = 0: completely prioritized sampling.
  // Insert 4 transitions: index 2 with priority 100, others with priority 1.
  let buf = PrioritizedReplayBuffer::new(8, 1.0F, 0.0F, 0.01F)
  buf.push(10, 0, 0.0F, 11, false)
  buf.push(20, 1, 0.0F, 21, false)
  buf.push(30, 2, 0.0F, 31, false) // this will be bumped up
  buf.push(40, 3, 0.0F, 41, false)
  // Bump priority of slot 2 (= state 30).
  buf.update_priorities([2], [99.0F])
  let rng = Xoshiro::from_state(7UL, 11UL, 13UL, 17UL)
  // Sample many times; slot 2 should dominate.
  let mut hits_slot2 = 0
  let n_batches = 200
  for _k in 0..<n_batches {
    let (ss, _aa, _rr, _nss, _dd, idx, _w, _p) = buf.sample(4, rng)
    // Find the slot in the batch that corresponds to state=30 (slot 2).
    for j in 0..<4 {
      if idx[j] == 2 {
        hits_slot2 = hits_slot2 + 1
        ignore(ss[j])
      }
    }
  }
  // Slot 2's share should be far above 1/4 = 25% of total draws.
  // Total draws = n_batches * batch_size = 800. Expect ~ hits ~ 100*800/(100+3) ≈ 776.
  // Be lenient: > 600 / 800 (i.e., slot 2 > 75% of draws).
  assert_true(hits_slot2 > 600)
}

///|
test "per_is_weights_in_unit_interval" {
  // IS weights must be in [w_min, 1.0] with max == 1.0.
  let buf = PrioritizedReplayBuffer::new(8, 0.6F, 0.4F, 0.01F)
  buf.push(0, 0, 0.0F, 1, false)
  buf.push(1, 0, 0.0F, 2, false)
  buf.push(2, 0, 0.0F, 3, false)
  buf.push(3, 0, 0.0F, 0, false)
  buf.update_priorities([0, 1, 2, 3], [10.0F, 5.0F, 2.0F, 1.0F])
  let rng = Xoshiro::from_state(1UL, 2UL, 3UL, 4UL)
  let (_ss, _aa, _rr, _nss, _dd, _idx, w, _probs) = buf.sample(4, rng)
  // Find max.
  let mut m = w[0]
  for k in 1..<4 {
    if w[k] > m {
      m = w[k]
    }
  }
  // Max should be 1.0 (or very close — allow 1e-4 floating-point noise).
  let diff_max = if m > 1.0F { m - 1.0F } else { 1.0F - m }
  assert_true(diff_max < 1.0e-4F)
  // All weights should be in (0, 1].
  for k in 0..<4 {
    assert_true(w[k] > 0.0F)
    assert_true(w[k] <= 1.0001F)
  }
}

///|
test "per_update_priorities_changes_total" {
  let buf = PrioritizedReplayBuffer::new(4, 0.6F, 0.4F, 0.01F)
  buf.push(0, 0, 0.0F, 1, false)
  buf.push(1, 1, 0.0F, 2, false)
  buf.push(2, 2, 0.0F, 3, false)
  buf.push(3, 3, 0.0F, 0, false)
  // All priorities start at 1.0; total = 4.
  let total_before = buf.total_priority()
  let diff_before = if total_before > 4.0F {
    total_before - 4.0F
  } else {
    4.0F - total_before
  }
  assert_true(diff_before < 1.0e-5F)
  // Bump slot 2 to priority 100.
  buf.update_priorities([2], [99.99F])
  let total_after = buf.total_priority()
  // New total = 1 + 1 + (99.99 + 0.01) + 1 = 103.
  let diff_after = if total_after > 103.0F {
    total_after - 103.0F
  } else {
    103.0F - total_after
  }
  assert_true(diff_after < 1.0e-3F)
  assert_true(buf.max_priority() >= 100.0F)
}

///|
test "per_sample_uses_overwritten_slots_after_wrap" {
  // After wrap-around, slots 0..capacity-1 hold the *latest* pushes.
  let buf = PrioritizedReplayBuffer::new(4, 1.0F, 0.0F, 0.0F)
  // Slot 0 → push state 10 with high priority.
  buf.push(10, 0, 0.0F, 11, false)
  buf.push(20, 0, 0.0F, 21, false)
  buf.push(30, 0, 0.0F, 31, false)
  buf.push(40, 0, 0.0F, 41, false)
  // Slot 0 now overwritten with state 50 (priority 1, max).
  buf.push(50, 0, 0.0F, 51, false)
  // Sample repeatedly with alpha=1: should never reach state 10 because slot 0's
  // priority was reset to max (and no further priority bumps).
  let rng = Xoshiro::from_state(99UL, 100UL, 101UL, 102UL)
  let (ss, _aa, _rr, _nss, _dd, _idx, _w, _p) = buf.sample(4, rng)
  // Verify every state is in {20, 30, 40, 50} — none of {10}.
  for k in 0..<4 {
    let s = ss[k]
    assert_true(s == 20 || s == 30 || s == 40 || s == 50)
  }
}

///|
test "per_max_priority_increases_after_bump" {
  let buf = PrioritizedReplayBuffer::new(4, 0.6F, 0.4F, 0.01F)
  buf.push(0, 0, 0.0F, 1, false)
  buf.push(1, 0, 0.0F, 2, false)
  buf.push(2, 0, 0.0F, 3, false)
  buf.push(3, 0, 0.0F, 0, false)
  assert_eq(buf.max_priority(), 1.0F)
  buf.update_priorities([0], [42.0F])
  let mp = buf.max_priority()
  let diff = if mp > 42.01F { mp - 42.01F } else { 42.01F - mp }
  assert_true(diff < 1.0e-3F)
}

///|
test "per_sample_empty_buffer_does_not_panic" {
  // Sampling from an empty buffer must not crash. With total = 0 the call
  // returns the default-initialised arrays; we only require it to terminate.
  let buf = PrioritizedReplayBuffer::new(4, 0.6F, 0.4F, 0.01F)
  let rng = Xoshiro::from_state(1UL, 2UL, 3UL, 4UL)
  let (ss, aa, rr, nss, dd, idx, w, probs) = buf.sample(4, rng)
  assert_eq(ss.length(), 4)
  assert_eq(aa.length(), 4)
  assert_eq(rr.length(), 4)
  assert_eq(nss.length(), 4)
  assert_eq(dd.length(), 4)
  assert_eq(idx.length(), 4)
  assert_eq(w.length(), 4)
  assert_eq(probs.length(), 4)
  assert_eq(buf.len(), 0)
}

///|
test "per_sample_returns_finite_weights" {
  let buf = PrioritizedReplayBuffer::new(8, 0.6F, 0.6F, 0.01F)
  for i in 0..<8 {
    buf.push(i, i % 4, Float::from_int(i), (i + 1) % 8, false)
  }
  buf.update_priorities([0, 1, 2, 3], [5.0F, 0.5F, 50.0F, 0.1F])
  let rng = Xoshiro::from_state(13UL, 17UL, 19UL, 23UL)
  let (_ss, _aa, _rr, _nss, _dd, _idx, w, _probs) = buf.sample(4, rng)
  for k in 0..<4 {
    let wk = w[k]
    // No NaN or Inf.
    assert_true(wk == wk)
    assert_true(wk < 1.0e10F)
  }
}