// sequence_replay_buffer_test.mbt — Round-trip tests for the
// per-step continuous replay buffer with sequence-rollout sampling.
//
// Coverage:
//   * constructor shape + len starts at 0
//   * push increments len up to capacity (then FIFO wrap)
//   * reset() returns len to 0 without releasing storage
//   * sample_seq_batch shape + dones / rewards in window
//   * terminal flag propagation across done boundary
//   * sample_seq_batch safe with empty buffer (zero-padded)
//   * sample_seq_batch window crossing done boundary zero-pads rest
//   * deterministic reproduction under same RNG seed

///|
test "sequence_replay_buffer_new_is_empty" {
  let buf : SequenceReplayBuffer = SequenceReplayBuffer::new(8, 3, 2)
  assert_eq(buf.len(), 0)
}

///|
test "sequence_replay_buffer_push_increments_len" {
  let buf : SequenceReplayBuffer = SequenceReplayBuffer::new(4, 2, 1)
  buf.push([0.1F, 0.2F], [0.5F], 1.0F, [0.3F, 0.4F], false)
  assert_eq(buf.len(), 1)
  buf.push([0.5F, 0.6F], [-0.5F], 2.0F, [0.7F, 0.8F], true)
  assert_eq(buf.len(), 2)
}

///|
test "sequence_replay_buffer_capacity_overflow_wraps" {
  let buf : SequenceReplayBuffer = SequenceReplayBuffer::new(3, 2, 1)
  for i in 0..<5 {
    buf.push([Float::from_int(i), 0.0F], [0.0F], Float::from_int(i), [0.0F, 0.0F], false)
  }
  // len clamped at capacity=3 even though we pushed 5.
  assert_eq(buf.len(), 3)
}

///|
test "sequence_replay_buffer_reset_returns_len_to_zero" {
  let buf : SequenceReplayBuffer = SequenceReplayBuffer::new(4, 2, 1)
  buf.push([0.1F, 0.2F], [0.5F], 1.0F, [0.3F, 0.4F], false)
  buf.push([0.5F, 0.6F], [-0.5F], 2.0F, [0.7F, 0.8F], false)
  assert_eq(buf.len(), 2)
  buf.reset()
  assert_eq(buf.len(), 0)
}

///|
test "sequence_replay_buffer_sample_seq_batch_shape" {
  let buf : SequenceReplayBuffer = SequenceReplayBuffer::new(16, 3, 2)
  // Push 10 transitions; seq_len=4 means max_start = 10 - 4 = 6
  // start indices drawn from [0, 6).
  for i in 0..<10 {
    buf.push(
      [Float::from_int(i), Float::from_int(i + 100), Float::from_int(i + 200)],
      [Float::from_int(i + 300), Float::from_int(i + 400)],
      Float::from_int(i),
      [Float::from_int(i + 1), Float::from_int(i + 101), Float::from_int(i + 201)],
      false,
    )
  }
  let rng = Xoshiro::from_state(1UL, 2UL, 3UL, 4UL)
  let (obs_seq, action_seq, reward_seq, next_obs_seq, done_seq, term_mask, hidden_init) = buf.sample_seq_batch(3, 4, rng)
  assert_eq(obs_seq.length(), 3 * 4 * 3)  // 36
  assert_eq(action_seq.length(), 3 * 4 * 2)  // 24
  assert_eq(reward_seq.length(), 3 * 4)  // 12
  assert_eq(next_obs_seq.length(), 3 * 4 * 3)  // 36
  assert_eq(done_seq.length(), 3 * 4)  // 12
  assert_eq(term_mask.length(), 3 * 4)  // 12
  assert_eq(hidden_init.length(), 3)  // batch_size (hidden_dim caller convention)
}

///|
test "sequence_replay_buffer_sample_seq_batch_empty_buffer_zero_pads" {
  // Buffer has 0 transitions; sampling should return all-zero arrays
  // without crashing. With safe_max=0, every sequence is zero-padded
  // and the terminal_mask is set for every step.
  let buf : SequenceReplayBuffer = SequenceReplayBuffer::new(8, 2, 1)
  let rng = Xoshiro::from_state(7UL, 11UL, 13UL, 17UL)
  let (obs_seq, _act_seq, _r_seq, _next_seq, _done_seq, term_mask, hidden_init) = buf.sample_seq_batch(2, 3, rng)
  assert_eq(obs_seq.length(), 2 * 3 * 2)
  // Every slot is zero-initialised.
  assert_eq(obs_seq[0], 0.0F)
  assert_eq(obs_seq[5], 0.0F)
  assert_eq(obs_seq[11], 0.0F)
  assert_eq(term_mask[0], 1.0F)
  assert_eq(term_mask[5], 1.0F)
  assert_eq(hidden_init[0], 0.0F)
  assert_eq(hidden_init[1], 0.0F)
}

///|
test "sequence_replay_buffer_sample_seq_batch_window_crosses_done" {
  // 6 transitions: index 3 is terminal. seq_len=4, max_start = 6 - 4 = 2.
  // Pick start_idx=2: window covers indices [2, 3, 4, 5].
  // index 3 is done => terminal_mask[1] (next slot) should be 1.0F;
  // slots 2,3 in the window stay zero-padded.
  let buf : SequenceReplayBuffer = SequenceReplayBuffer::new(8, 1, 1)
  for i in 0..<6 {
    let done = i == 3
    buf.push(
      [Float::from_int(i)],
      [Float::from_int(i + 10)],
      Float::from_int(i),
      [Float::from_int(i + 100)],
      done,
    )
  }
  // Loop over many seeds until we draw start_idx=2; or skip and
  // just check that at least one of the first 50 draws produces a
  // sequence whose terminal_mask differs from all-zero.
  let mut found_terminal = false
  let mut draw_seed : UInt64 = 42UL
  while draw_seed < 60UL && not(found_terminal) {
    let rng = Xoshiro::from_state(draw_seed, draw_seed + 1UL, draw_seed + 2UL, draw_seed + 3UL)
    let (_o, _a, _r, _n, _d, term_mask, _h) = buf.sample_seq_batch(8, 4, rng)
    // Scan for any non-zero terminal_mask slot.
    for j in 0..<term_mask.length() {
      if term_mask[j] > 0.5F {
        found_terminal = true
        break
      }
    }
    draw_seed = draw_seed + 1UL
  }
  if not(found_terminal) {
    abort("no terminal mask observed across 18 seeded draws")
  }
}

///|
test "sequence_replay_buffer_sample_seq_batch_reproducible_under_seed" {
  let buf1 : SequenceReplayBuffer = SequenceReplayBuffer::new(8, 2, 1)
  let buf2 : SequenceReplayBuffer = SequenceReplayBuffer::new(8, 2, 1)
  for i in 0..<6 {
    let s = [Float::from_int(i), Float::from_int(i + 1)]
    let a = [Float::from_int(i + 10)]
    let ns = [Float::from_int(i + 100), Float::from_int(i + 101)]
    buf1.push(s, a, Float::from_int(i), ns, false)
    buf2.push(s, a, Float::from_int(i), ns, false)
  }
  let rng1 = Xoshiro::from_state(99UL, 100UL, 101UL, 102UL)
  let rng2 = Xoshiro::from_state(99UL, 100UL, 101UL, 102UL)
  let (o1, a1, r1, n1, d1, m1, h1) = buf1.sample_seq_batch(2, 3, rng1)
  let (o2, a2, r2, n2, d2, m2, h2) = buf2.sample_seq_batch(2, 3, rng2)
  // Element-wise equality across the two batches.
  for i in 0..<o1.length() {
    assert_eq(o1[i], o2[i])
  }
  for i in 0..<a1.length() {
    assert_eq(a1[i], a2[i])
  }
  for i in 0..<r1.length() {
    assert_eq(r1[i], r2[i])
  }
  for i in 0..<n1.length() {
    assert_eq(n1[i], n2[i])
  }
  for i in 0..<d1.length() {
    assert_eq(d1[i], d2[i])
  }
  for i in 0..<m1.length() {
    assert_eq(m1[i], m2[i])
  }
  for i in 0..<h1.length() {
    assert_eq(h1[i], h2[i])
  }
}

///|
test "sequence_replay_buffer_dones_propagate_in_window" {
  // Push 8 transitions with dones at indices 3 and 6.
  let buf : SequenceReplayBuffer = SequenceReplayBuffer::new(16, 1, 1)
  for i in 0..<8 {
    buf.push([Float::from_int(i)], [Float::from_int(i + 10)], Float::from_int(i), [Float::from_int(i + 100)], i == 3 || i == 6)
  }
  // Use seed that we expect to land on at least one terminal slot.
  // Loop 50 seeds, count any draw where any slot has done_seq > 0.5.
  let mut found_done : Bool = false
  let mut draw_seed : UInt64 = 100UL
  while draw_seed < 150UL && not(found_done) {
    let rng = Xoshiro::from_state(draw_seed, draw_seed + 1UL, draw_seed + 2UL, draw_seed + 3UL)
    let (_o, _a, _r, _n, done_seq, _m, _h) = buf.sample_seq_batch(4, 5, rng)
    for j in 0..<done_seq.length() {
      if done_seq[j] > 0.5F {
        found_done = true
        break
      }
    }
    draw_seed = draw_seed + 1UL
  }
  if not(found_done) {
    abort("no done=1 slot observed in any window across 50 seeds")
  }
}

// ============================================================================
// Terminal-mask boundary tests for BPTT cutoff (v0.57.0 follow-up).
//
// Semantics: terminal_mask[t] = 1.0F means "the previous slot (t-1) was a
// terminal; reset the recurrent hidden state BEFORE processing slot t".
// Slot t=0 always has terminal_mask=0 (episode starts fresh). When a slot
// itself is the terminal within a window, done_seq[t] = 1.0F (so the
// caller knows via done_seq directly without needing terminal_mask).
//
// These tests use `sample_seq_batch_at` to force specific start indices
// (one per test) instead of looping over RNG seeds. The RNG-based
// `sample_seq_batch` uses `box_muller + |z|` which produces a half-normal
// distribution concentrated near 0, so hitting a specific start_idx
// requires many draws — fragile in unit tests. `sample_seq_batch_at` is
// the deterministic counterpart and is used here for boundary coverage.
// ============================================================================

///|
test "sequence_replay_buffer_terminal_mask_binary_values_only" {
  // Every terminal_mask slot must be exactly 0.0F or 1.0F (no
  // fractional values; this is what callers test for with `> 0.5F`).
  // Sample multiple windows deterministically and check every slot.
  let buf : SequenceReplayBuffer = SequenceReplayBuffer::new(16, 1, 1)
  for i in 0..<12 {
    let done = i == 4 || i == 8
    buf.push([Float::from_int(i)], [Float::from_int(i)], Float::from_int(i), [Float::from_int(i + 1)], done)
  }
  // Force start indices that exercise all branches: done boundary,
  // post-terminal zero-pad, trailing zero-pad.
  let starts : Array[Int] = [0, 2, 4, 6]
  let (_o, _a, _r, _n, _d, term_mask, _h) = buf.sample_seq_batch_at(4, 5, starts)
  for j in 0..<term_mask.length() {
    let v = term_mask[j]
    if v != 0.0F && v != 1.0F {
      abort("terminal_mask[" + j.to_string() + "] = " + v.to_string() + " (must be 0.0F or 1.0F)")
    }
  }
}

///|
test "sequence_replay_buffer_terminal_mask_semantics_next_slot_after_done" {
  // Hand-crafted: buffer with done=1 at exactly index 5. Sample a
  // sequence that starts at index 3 (window covers [3,4,5,6,7]).
  // Done is at slot 2 of the window (global_idx=5 = start+2).
  // Expected terminal_mask: [0, 0, 0, 1, 1]
  //            (slot 3 needs reset because slot 2 was terminal;
  //             slots 4 and beyond are post-terminal zero-padded,
  //             so term_mask=1 for ALL of them — the caller treats
  //             them as undefined and resets hidden state.)
  let buf : SequenceReplayBuffer = SequenceReplayBuffer::new(16, 1, 1)
  for i in 0..<12 {
    let done = i == 5
    buf.push([Float::from_int(i)], [Float::from_int(i)], Float::from_int(i), [Float::from_int(i + 1)], done)
  }
  let starts : Array[Int] = [3]
  let (_o, _a, _r, _n, done_seq, term_mask, _h) = buf.sample_seq_batch_at(1, 5, starts)
  // start_idx=3 → window [3,4,5,6,7]. Done at slot 2 (index 5).
  assert_eq(done_seq[2], 1.0F)
  assert_eq(term_mask[0], 0.0F)
  assert_eq(term_mask[1], 0.0F)
  assert_eq(term_mask[2], 0.0F)
  // Slot 3: marked by the `if t+1<seq_len` branch on slot 2.
  assert_eq(term_mask[3], 1.0F)
  // Slot 4: post-terminal zero-pad → term_mask=1, done_seq=0.
  assert_eq(term_mask[4], 1.0F)
  assert_eq(done_seq[4], 0.0F)
}

///|
test "sequence_replay_buffer_terminal_mask_done_at_first_slot" {
  // Done=1 at index 0. Window starting at 0 covers [0,1,2,3]. The
  // FIRST slot is the terminal (done_seq[0]=1), so term_mask[1]=1
  // (reset needed before slot 1 since slot 0 ended the episode).
  // Slots 2..3 are past the terminal: also term_mask=1 (zero-padded
  // region) — caller must treat them as undefined and reset hidden
  // state.
  let buf : SequenceReplayBuffer = SequenceReplayBuffer::new(8, 1, 1)
  for i in 0..<6 {
    let done = i == 0
    buf.push([Float::from_int(i)], [Float::from_int(i)], Float::from_int(i), [Float::from_int(i + 1)], done)
  }
  let starts : Array[Int] = [0]
  let (_o, _a, _r, _n, done_seq, term_mask, _h) = buf.sample_seq_batch_at(1, 4, starts)
  assert_eq(done_seq[0], 1.0F)
  assert_eq(done_seq[1], 0.0F)
  assert_eq(done_seq[2], 0.0F)
  assert_eq(done_seq[3], 0.0F)
  assert_eq(term_mask[0], 0.0F)
  assert_eq(term_mask[1], 1.0F)
  assert_eq(term_mask[2], 1.0F)
  assert_eq(term_mask[3], 1.0F)
}

///|
test "sequence_replay_buffer_terminal_mask_done_at_last_slot_of_window" {
  // Done=1 at exactly index 3 (the last position of a seq_len=4 window
  // starting at index 0). Expected: done_seq[3]=1, but term_mask[3]=0
  // (no next slot within window to mark). Caller must detect the
  // terminal via done_seq directly; term_mask at the last slot is 0
  // because there is no in-window successor to flag.
  let buf : SequenceReplayBuffer = SequenceReplayBuffer::new(8, 1, 1)
  for i in 0..<6 {
    let done = i == 3
    buf.push([Float::from_int(i)], [Float::from_int(i)], Float::from_int(i), [Float::from_int(i + 1)], done)
  }
  let starts : Array[Int] = [0]
  let (_o, _a, _r, _n, done_seq, term_mask, _h) = buf.sample_seq_batch_at(1, 4, starts)
  assert_eq(done_seq[0], 0.0F)
  assert_eq(done_seq[1], 0.0F)
  assert_eq(done_seq[2], 0.0F)
  assert_eq(done_seq[3], 1.0F)
  // term_mask: [0,0,0,0] — done is at the last slot, no successor to mark.
  assert_eq(term_mask[0], 0.0F)
  assert_eq(term_mask[1], 0.0F)
  assert_eq(term_mask[2], 0.0F)
  assert_eq(term_mask[3], 0.0F)
}

///|
test "sequence_replay_buffer_terminal_mask_consecutive_dones" {
  // Dones at indices 4 AND 5 (consecutive). Window starting at 3
  // covers [3,4,5,6]. The FIRST done (at slot 1, index 4) flips
  // crossed_done=true; all subsequent slots are zero-padded regardless
  // of their original done flag. So:
  //   - term_mask[0] = 0   (slot 0 has no terminal prior)
  //   - term_mask[1] = 0   (slot 1 is the terminal; current slot stays 0)
  //   - term_mask[2] = 1   (slot 1's t+1 successor, AND crossed_done)
  //   - term_mask[3] = 1   (post-terminal zero-pad)
  //   - done_seq[2] = 0    (overridden by zero-pad; original was 1
  //                          but we don't expose it after the FIRST done
  //                          in the window)
  // This locks in the "first done wins" rule.
  let buf : SequenceReplayBuffer = SequenceReplayBuffer::new(16, 1, 1)
  for i in 0..<10 {
    let done = i == 4 || i == 5
    buf.push([Float::from_int(i)], [Float::from_int(i)], Float::from_int(i), [Float::from_int(i + 1)], done)
  }
  let starts : Array[Int] = [3]
  let (_o, _a, _r, _n, done_seq, term_mask, _h) = buf.sample_seq_batch_at(1, 4, starts)
  assert_eq(done_seq[0], 0.0F)
  assert_eq(done_seq[1], 1.0F)  // first done, surfaced
  assert_eq(done_seq[2], 0.0F)  // post-terminal zero-pad overrides original done=1
  assert_eq(done_seq[3], 0.0F)
  assert_eq(term_mask[0], 0.0F)
  assert_eq(term_mask[1], 0.0F)  // terminal slot itself: term_mask=0
  assert_eq(term_mask[2], 1.0F)
  assert_eq(term_mask[3], 1.0F)
}

///|
test "sequence_replay_buffer_terminal_mask_zero_when_no_done_in_window" {
  // Dones at indices 2 and 7. Window starting at 4 covers [4,5,6,7].
  // Done at slot 3 (index 7) — the LAST slot in the window. There is
  // no in-window successor to flag, so term_mask stays all zero even
  // though done_seq[3]=1. Caller detects the terminal via done_seq
  // directly.
  let buf : SequenceReplayBuffer = SequenceReplayBuffer::new(16, 1, 1)
  for i in 0..<12 {
    let done = i == 2 || i == 7
    buf.push([Float::from_int(i)], [Float::from_int(i)], Float::from_int(i), [Float::from_int(i + 1)], done)
  }
  let starts : Array[Int] = [4]
  let (_o, _a, _r, _n, done_seq, term_mask, _h) = buf.sample_seq_batch_at(1, 4, starts)
  assert_eq(done_seq[0], 0.0F)
  assert_eq(done_seq[1], 0.0F)
  assert_eq(done_seq[2], 0.0F)
  assert_eq(done_seq[3], 1.0F)
  assert_eq(term_mask[0], 0.0F)
  assert_eq(term_mask[1], 0.0F)
  assert_eq(term_mask[2], 0.0F)
  assert_eq(term_mask[3], 0.0F)  // terminal slot itself, no successor to mark
}

///|
test "sequence_replay_buffer_terminal_mask_no_done_in_window_all_zero" {
  // Dones at indices 2, 5, 8 (separated). Window starting at 6 covers
  // [6,7,8,9]. Done at slot 2 (index 8). Expected: term_mask[0..1]=0
  // (no prior done in window), term_mask[2]=0 (terminal slot itself,
  // current slot stays 0), term_mask[3]=1 (post-terminal zero-pad —
  // slot 9 is past the first done in window).
  let buf : SequenceReplayBuffer = SequenceReplayBuffer::new(16, 1, 1)
  for i in 0..<12 {
    let done = i == 2 || i == 5 || i == 8
    buf.push([Float::from_int(i)], [Float::from_int(i)], Float::from_int(i), [Float::from_int(i + 1)], done)
  }
  let starts : Array[Int] = [6]
  let (_o, _a, _r, _n, done_seq, term_mask, _h) = buf.sample_seq_batch_at(1, 4, starts)
  assert_eq(done_seq[0], 0.0F)
  assert_eq(done_seq[1], 0.0F)
  assert_eq(done_seq[2], 1.0F)  // the terminal itself
  assert_eq(done_seq[3], 0.0F)  // post-terminal zero-pad
  assert_eq(term_mask[0], 0.0F)
  assert_eq(term_mask[1], 0.0F)
  assert_eq(term_mask[2], 0.0F)  // terminal slot itself: term_mask=0
  assert_eq(term_mask[3], 1.0F)  // post-terminal zero-pad
}

///|
test "sequence_replay_buffer_terminal_mask_at_buffer_end_no_done" {
  // Window that crosses the buffer boundary (zero-padded trailing
  // slots) but no done=1 in the populated portion. Expected: populated
  // slots have term_mask=0; zero-padded trailing slots have
  // term_mask=1 (caller knows data is undefined there).
  let buf : SequenceReplayBuffer = SequenceReplayBuffer::new(16, 1, 1)
  for i in 0..<3 {
    // No dones at all in the buffer.
    buf.push([Float::from_int(i)], [Float::from_int(i)], Float::from_int(i), [Float::from_int(i + 1)], false)
  }
  // safe_max = 3 - 4 = -1 → 0 → start_idx is forced to 0. Window
  // [0,1,2] is real data, slot 3 is past the end.
  let starts : Array[Int] = [0]
  let (_o, _a, _r, _n, done_seq, term_mask, _h) = buf.sample_seq_batch_at(1, 4, starts)
  assert_eq(done_seq[0], 0.0F)
  assert_eq(done_seq[1], 0.0F)
  assert_eq(done_seq[2], 0.0F)
  assert_eq(done_seq[3], 0.0F)
  // Slots 0,1,2 are real data → term_mask=0.
  assert_eq(term_mask[0], 0.0F)
  assert_eq(term_mask[1], 0.0F)
  assert_eq(term_mask[2], 0.0F)
  // Slot 3 is past the end → term_mask=1.
  assert_eq(term_mask[3], 1.0F)
}

///|
test "sequence_replay_buffer_terminal_mask_at_fifo_wrap_boundary" {
  // Trigger FIFO wrap: cap=4, push 5 transitions with done=(i==3).
  // After 5 pushes: cursor has wrapped once. Live storage layout:
  //   index 0: data from i=3 (done=true)        ← overwritten by 5th push
  //   index 1: data from i=1 (done=false)
  //   index 2: data from i=2 (done=false)
  //   index 3: data from i=3 (done=true)        ← never overwritten
  // Wait — let me trace precisely. With cap=4, push order:
  //   i=0: cursor=0 → write at 0, cursor=1
  //   i=1: cursor=1 → write at 1, cursor=2
  //   i=2: cursor=2 → write at 2, cursor=3
  //   i=3: cursor=3 → write at 3, cursor=4→0 (wrap)
  //   i=4: cursor=0 → write at 0 (overwriting i=0), cursor=1
  // So after 5 pushes:
  //   index 0: data from i=4 (done=false, since i=4 not in done set)
  //   index 1: data from i=1 (done=false)
  //   index 2: data from i=2 (done=false)
  //   index 3: data from i=3 (done=true)
  // safe_max = 4 - 3 = 1; start_idx in [0, 1).
  // start_idx=1 → window [1,2,3]. Done at slot 2 (index 3) — the LAST
  // slot. No in-window successor to mark → term_mask stays all zero.
  let buf : SequenceReplayBuffer = SequenceReplayBuffer::new(4, 1, 1)
  for i in 0..<5 {
    let done = i == 3
    buf.push([Float::from_int(i)], [Float::from_int(i)], Float::from_int(i), [Float::from_int(i + 1)], done)
  }
  let starts : Array[Int] = [1]
  let (_o, _a, _r, _n, done_seq, term_mask, _h) = buf.sample_seq_batch_at(1, 3, starts)
  assert_eq(done_seq[0], 0.0F)
  assert_eq(done_seq[1], 0.0F)
  assert_eq(done_seq[2], 1.0F)  // the terminal itself, last slot of window
  // term_mask stays all zero — done at last slot, no successor to mark.
  assert_eq(term_mask[0], 0.0F)
  assert_eq(term_mask[1], 0.0F)
  assert_eq(term_mask[2], 0.0F)
}

///|
test "sequence_replay_buffer_terminal_mask_multi_batch_consistent" {
  // Two batches in parallel: each gets its own slice of term_mask.
  // Force batch 0 to start at 2 (window [2,3,4,5], done at slot 2 → index 4),
  // batch 1 to start at 0 (window [0,1,2,3], done at index 4 is OUTSIDE
  // the window → batch 1 has no done).
  // Verify batch 0's term_mask[3]=1 and batch 1's term_mask stays all zero.
  let buf : SequenceReplayBuffer = SequenceReplayBuffer::new(16, 1, 1)
  for i in 0..<8 {
    let done = i == 4
    buf.push([Float::from_int(i)], [Float::from_int(i)], Float::from_int(i), [Float::from_int(i + 1)], done)
  }
  let starts : Array[Int] = [2, 0]
  let (_o, _a, _r, _n, done_seq, term_mask, _h) = buf.sample_seq_batch_at(2, 4, starts)
  // Batch 0 (starts at index 2, window [2,3,4,5]):
  //   done_seq[2] = 1 (index 4 is terminal)
  //   term_mask   = [0, 0, 0, 1]
  assert_eq(done_seq[0 * 4 + 0], 0.0F)
  assert_eq(done_seq[0 * 4 + 1], 0.0F)
  assert_eq(done_seq[0 * 4 + 2], 1.0F)
  assert_eq(done_seq[0 * 4 + 3], 0.0F)
  assert_eq(term_mask[0 * 4 + 0], 0.0F)
  assert_eq(term_mask[0 * 4 + 1], 0.0F)
  assert_eq(term_mask[0 * 4 + 2], 0.0F)
  assert_eq(term_mask[0 * 4 + 3], 1.0F)
  // Batch 1 (starts at index 0, window [0,1,2,3]):
  //   No done in window → done_seq all zero, term_mask all zero.
  assert_eq(done_seq[1 * 4 + 0], 0.0F)
  assert_eq(done_seq[1 * 4 + 1], 0.0F)
  assert_eq(done_seq[1 * 4 + 2], 0.0F)
  assert_eq(done_seq[1 * 4 + 3], 0.0F)
  assert_eq(term_mask[1 * 4 + 0], 0.0F)
  assert_eq(term_mask[1 * 4 + 1], 0.0F)
  assert_eq(term_mask[1 * 4 + 2], 0.0F)
  assert_eq(term_mask[1 * 4 + 3], 0.0F)
}
