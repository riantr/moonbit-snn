// n_step_buffer_test.mbt — Unit tests for the n-step buffer.

///|
test "n_step_buffer_new_is_empty" {
  let buf = NStepBuffer::new(4)
  assert_eq(buf.len(), 0)
  assert_eq(buf.capacity(), 4)
}

///|
test "n_step_buffer_push_grows_until_full" {
  let buf = NStepBuffer::new(3)
  let r0 = buf.push(0, 0, 0.0F, 1, false)
  assert_eq(r0, false)
  assert_eq(buf.len(), 1)
  let r1 = buf.push(1, 1, 1.0F, 2, false)
  assert_eq(r1, false)
  assert_eq(buf.len(), 2)
  let r2 = buf.push(2, 2, 2.0F, 3, false)
  assert_eq(r2, true)
  assert_eq(buf.len(), 3)
}

///|
test "n_step_buffer_push_past_full_returns_true_each_time" {
  let buf = NStepBuffer::new(2)
  let _ = buf.push(0, 0, 0.0F, 1, false)
  let _ = buf.push(1, 0, 0.0F, 2, false)
  let r2 = buf.push(2, 0, 0.0F, 3, false)
  assert_eq(r2, true)
  let r3 = buf.push(3, 0, 0.0F, 4, false)
  assert_eq(r3, true)
  assert_eq(buf.len(), 2)
}

///|
test "n_step_buffer_at_returns_records_in_order" {
  let buf = NStepBuffer::new(3)
  let _ = buf.push(10, 1, 0.5F, 11, false)
  let _ = buf.push(20, 2, 0.6F, 21, false)
  let _ = buf.push(30, 3, 0.7F, 31, true)
  let r0 = buf.at(0)
  let r1 = buf.at(1)
  let r2 = buf.at(2)
  assert_eq(r0.s, 10)
  assert_eq(r0.a, 1)
  let r0r = r0.r
  let r0r_diff = if r0r > 0.5F { r0r - 0.5F } else { 0.5F - r0r }
  assert_true(r0r_diff < 1.0e-5F)
  assert_eq(r1.s, 20)
  assert_eq(r2.s, 30)
  assert_true(r2.done)
}

///|
test "n_step_buffer_overwrites_oldest_in_fifo_order" {
  let buf = NStepBuffer::new(2)
  let _ = buf.push(10, 0, 0.0F, 11, false)
  let _ = buf.push(20, 0, 0.0F, 21, false)
  let _ = buf.push(30, 0, 0.0F, 31, false) // overwrites 10
  let r0 = buf.at(0)
  let r1 = buf.at(1)
  assert_eq(r0.s, 20)
  assert_eq(r1.s, 30)
}

///|
test "n_step_buffer_reset_clears_count" {
  let buf = NStepBuffer::new(3)
  let _ = buf.push(0, 0, 0.0F, 1, false)
  let _ = buf.push(1, 0, 0.0F, 2, false)
  buf.reset()
  assert_eq(buf.len(), 0)
}

///|
test "n_step_return_basic_3_step" {
  let buf = NStepBuffer::new(3)
  let _ = buf.push(0, 1, 1.0F, 1, false)
  let _ = buf.push(1, 1, 2.0F, 2, false)
  let _ = buf.push(2, 1, 3.0F, 3, false)
  // G_3 = 1 + γ·2 + γ²·3, no truncation, h=3, s_lookahead=3.
  let gamma = 0.9F
  let (g, h, s_root, s_look, trunc) = buf.n_step_return(0, gamma)
  let g_expected = 1.0F + 0.9F * 2.0F + 0.9F * 0.9F * 3.0F
  let diff = if g > g_expected { g - g_expected } else { g_expected - g }
  assert_true(diff < 1.0e-4F)
  assert_eq(h, 3)
  assert_eq(s_root, 0)
  assert_eq(s_look, 3)
  assert_eq(trunc, false)
}

///|
test "n_step_return_truncated_at_terminal" {
  let buf = NStepBuffer::new(4)
  let _ = buf.push(0, 1, 1.0F, 1, false)
  let _ = buf.push(1, 1, 2.0F, 2, true) // terminal at index 1
  let _ = buf.push(99, 99, 99.0F, 99, false) // after-terminal: still in buffer but won't be summed
  let _ = buf.push(99, 99, 99.0F, 99, false)
  let gamma = 0.9F
  let (g, h, s_root, s_look, trunc) = buf.n_step_return(0, gamma)
  // Sum should be 1 + γ·2 = 2.8; h=2; truncated=true.
  let g_expected = 1.0F + 0.9F * 2.0F
  let diff = if g > g_expected { g - g_expected } else { g_expected - g }
  assert_true(diff < 1.0e-5F)
  assert_eq(h, 2)
  assert_eq(s_root, 0)
  assert_eq(s_look, 2)
  assert_eq(trunc, true)
}

///|
test "n_step_return_partial_window" {
  // Buffer of capacity 4 but only 2 records pushed.
  let buf = NStepBuffer::new(4)
  let _ = buf.push(0, 1, 1.0F, 1, false)
  let _ = buf.push(1, 1, 2.0F, 2, false)
  let gamma = 0.5F
  let (g, h, _s_root, s_look, trunc) = buf.n_step_return(0, gamma)
  let g_expected = 1.0F + 0.5F * 2.0F
  let diff = if g > g_expected { g - g_expected } else { g_expected - g }
  assert_true(diff < 1.0e-5F)
  assert_eq(h, 2)
  assert_eq(s_look, 2)
  assert_eq(trunc, false)
}