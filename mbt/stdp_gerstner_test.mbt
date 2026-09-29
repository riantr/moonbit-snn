// stdp_gerstner_test.mbt — tests for v0.40.0 STDP Gerstner.

///|
test "stdp_gerstner_default_param" {
  let p = StdpGerstnerParam::new()
  assert_true(p.tau_pre > 0.0F && p.tau_post > 0.0F)
  assert_true(p.w_min <= p.w_max)
  assert_true(p.a_pre > 0.0F && p.a_post > 0.0F)
}

///|
test "stdp_gerstner_state_init_zero" {
  let s = StdpGerstnerState::new(3, 2)
  assert_eq(s.n_pre, 3)
  assert_eq(s.n_post, 2)
  for j in 0..<3 {
    assert_true(s.tpre[j] == 0.0F)
  }
  for i in 0..<2 {
    assert_true(s.tpost[i] == 0.0F)
  }
}

///|
test "stdp_gerstner_post_spike_increases_weight" {
  let p = StdpGerstnerParam::new()
  let state = StdpGerstnerState::new(2, 1)
  let fire_pre : Array[Bool] = [true, false]
  let fire_post : Array[Bool] = [false]
  let w : Array[Float] = [0.5F, 0.5F]
  stdp_gerstner_step(state, p, fire_pre, fire_post, w, 0.0F)
  let fire_pre2 : Array[Bool] = [false, false]
  let fire_post2 : Array[Bool] = [true]
  stdp_gerstner_step(state, p, fire_pre2, fire_post2, w, 10.0F)
  assert_true(w[0] > 0.5F)
  for s in 0..<2 {
    assert_true(w[s] >= 0.5F)
  }
}

///|
test "stdp_gerstner_pre_spike_increases_weight" {
  let p = StdpGerstnerParam::new()
  let state = StdpGerstnerState::new(2, 1)
  let fire_pre : Array[Bool] = [false, false]
  let fire_post : Array[Bool] = [true]
  let w : Array[Float] = [0.5F, 0.5F]
  stdp_gerstner_step(state, p, fire_pre, fire_post, w, 0.0F)
  let fire_pre2 : Array[Bool] = [true, false]
  let fire_post2 : Array[Bool] = [false]
  stdp_gerstner_step(state, p, fire_pre2, fire_post2, w, 10.0F)
  assert_true(w[0] > 0.5F)
  assert_true(w[1] == 0.5F)
}

///|
test "stdp_gerstner_no_spike_no_change" {
  let p = StdpGerstnerParam::new()
  let state = StdpGerstnerState::new(2, 2)
  let fire_pre : Array[Bool] = [false, false]
  let fire_post : Array[Bool] = [false, false]
  let w : Array[Float] = [0.5F, 0.5F, 0.5F, 0.5F]
  stdp_gerstner_step(state, p, fire_pre, fire_post, w, 5.0F)
  for s in 0..<4 {
    assert_true((w[s] - 0.5F).abs() < 1.0e-7F)
  }
}

///|
test "stdp_gerstner_clamp_to_wmax" {
  let p : StdpGerstnerParam = {
    a_pre: 10.0F, a_post: 10.0F,
    tau_pre: 100.0F, tau_post: 100.0F,
    w_min: 0.0F, w_max: 1.0F,
  }
  let state = StdpGerstnerState::new(1, 1)
  let fire_pre : Array[Bool] = [true]
  let fire_post : Array[Bool] = [true]
  let w : Array[Float] = [0.5F]
  for t in 0..<20 {
    stdp_gerstner_step(state, p, fire_pre, fire_post, w, Float::from_int(t))
  }
  assert_true(w[0] <= 1.0F)
}

///|
test "stdp_gerstner_trace_decays" {
  let p = StdpGerstnerParam::new()
  let state = StdpGerstnerState::new(1, 1)
  let fire_pre : Array[Bool] = [true]
  let fire_post : Array[Bool] = [false]
  let w : Array[Float] = [0.0F]
  stdp_gerstner_step(state, p, fire_pre, fire_post, w, 0.0F)
  let fire_pre2 : Array[Bool] = [false]
  let fire_post2 : Array[Bool] = [false]
  stdp_gerstner_step(state, p, fire_pre2, fire_post2, w, 200.0F)
  assert_true(state.tpre[0] < 1.0e-3F)
}