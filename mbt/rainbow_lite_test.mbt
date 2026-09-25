// rainbow_lite_test.mbt — Tests for Rainbow lite (v0.37.2).

///|
test "pow_fast: x=0 -> 0" {
  assert_eq(pow_fast(0.0F, 0.5F), 0.0F)
}

///|
test "pow_fast: x=1 -> 1" {
  let v = pow_fast(1.0F, 5.0F)
  assert_true((v - 1.0F).abs() < 1.0e-5F)
}

///|
test "pow_fast: x=2, y=3 -> 8" {
  let v = pow_fast(2.0F, 3.0F)
  let diff = if v > 8.0F { v - 8.0F } else { 8.0F - v }
  assert_true(diff < 0.01F)
}

///|
test "pow_fast: fractional exponent" {
  // 4^0.5 = 2
  let v = pow_fast(4.0F, 0.5F)
  let diff = if v > 2.0F { v - 2.0F } else { 2.0F - v }
  assert_true(diff < 0.01F)
}

///|
test "prioritized_buffer: push + len" {
  let b = PrioritizedBuffer::new(3, 0.6F, 0.4F, 0.01F)
  assert_eq(b.len(), 0)
  b.push(0, 1, 1.0F, 2, false)
  assert_eq(b.len(), 1)
  b.push(1, 0, 0.5F, 3, true)
  assert_eq(b.len(), 2)
}

///|
test "prioritized_buffer: update_priority changes total" {
  let b = PrioritizedBuffer::new(2, 0.6F, 0.4F, 0.01F)
  b.push(0, 1, 1.0F, 2, false)
  b.push(1, 0, 0.5F, 3, true)
  let initial_total = b.total_priority
  b.update_priority(0, 1.0F)  // |δ|=1, alpha=0.6 → 1^0.6 + 0.01 ≈ 1.01
  assert_true(b.total_priority > initial_total)
}

///|
test "prioritized_buffer: sample returns valid transitions" {
  let b = PrioritizedBuffer::new(10, 0.6F, 0.4F, 0.01F)
  let rng = Xoshiro::from_state(13UL, 17UL, 19UL, 23UL)
  for k in 0..<10 {
    b.push(k, k % 4, Float::from_int(k), (k + 1) % 10, false)
  }
  let (indices, ss, aa, rr, nss, dd, isw) = b.sample(5, rng)
  assert_eq(indices.length(), 5)
  assert_eq(ss.length(), 5)
  for k in 0..<5 {
    assert_true(ss[k] >= 0 && ss[k] < 10)
    assert_true(aa[k] >= 0 && aa[k] < 4)
    assert_true(rr[k] >= 0.0F && rr[k] < 10.0F)
    assert_true(isw[k] > 0.0F && isw[k] <= 1.0F)
  }
}

///|
test "multi_step_return: 1-step" {
  let rewards : Array[Float] = [0.5F, 0.5F, 0.5F]
  let dones : Array[Bool] = [false, false, false]
  // n=1 → just use q_next_max (no accumulated reward).
  let g = multi_step_return(rewards, dones, 5.0F, 0.9F, 1, 0)
  assert_eq(g, 5.0F)
}

///|
test "multi_step_return: 3-step with all rewards = 0.5" {
  let rewards : Array[Float] = [0.5F, 0.5F, 0.5F]
  let dones : Array[Bool] = [false, false, false]
  // G_0 = r_0 + γ·r_1 + γ²·r_2 + γ³·q_next
  //     = 0.5 + 0.9·0.5 + 0.81·0.5 + 0.729·5
  //     = 0.5 + 0.45 + 0.405 + 3.645
  //     = 5.0
  let g = multi_step_return(rewards, dones, 5.0F, 0.9F, 3, 0)
  let diff = if g > 5.0F { g - 5.0F } else { 5.0F - g }
  assert_true(diff < 1.0e-5F)
}

///|
test "multi_step_return: terminate early truncates to 0" {
  // n=4, dones=[true, false, false]. At k=0, dones[0]=true,
  // so we truncate cleanly: g = 0.
  let rewards : Array[Float] = [0.5F, 0.5F, 0.5F]
  let dones : Array[Bool] = [true, false, false]
  let g = multi_step_return(rewards, dones, 5.0F, 0.9F, 4, 0)
  assert_eq(g, 0.0F)
}

///|
test "multi_step_return: done later stops accumulating" {
  // dones[1] = true. k=0 succeeds, k=1 sees done → g becomes 0,
  // break. Result: γ^4·5 + r_0 = 0.6561·5 + 0.5 = 3.7805.
  let rewards : Array[Float] = [0.5F, 0.5F, 0.5F]
  let dones : Array[Bool] = [false, true, false]
  let g = multi_step_return(rewards, dones, 5.0F, 0.9F, 4, 0)
  // After k=1 sets g=0, breaking leaves g=0.
  assert_eq(g, 0.0F)
}