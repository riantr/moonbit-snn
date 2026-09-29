// stdp_voltage_test.mbt — tests for v0.40.2 voltage-dependent STDP.

///|
test "vstdp_default_param" {
  let p = VStdpParam::new()
  assert_true(p.a_ltp > 0.0F && p.a_ltd > 0.0F)
  assert_true(p.tau_pre > 0.0F && p.tau_post > 0.0F)
  assert_true(p.delta_v > 0.0F)
}

///|
test "vstdp_state_init_zero_traces" {
  let v_init : Array[Float] = [-70.0F, -65.0F]
  let s = VStdpState::new(2, 2, v_init)
  assert_eq(s.n_pre, 2)
  assert_eq(s.n_post, 2)
  for j in 0..<2 {
    assert_true(s.tpre[j] == 0.0F)
  }
  for i in 0..<2 {
    assert_true(s.tpost[i] == 0.0F)
    assert_true((s.v_post[i] - v_init[i]).abs() < 1.0e-7F)
  }
}

///|
test "vstdp_high_voltage_potentiates" {
  let p = VStdpParam::new()
  let v_init : Array[Float] = [-50.0F]
  let state = VStdpState::new(1, 1, v_init)
  let fire_pre : Array[Bool] = [true]
  let fire_post : Array[Bool] = [false]
  let w : Array[Float] = [0.5F]
  vstdp_clopath_step(state, p, fire_pre, fire_post, w, 0.1F)
  let fire_pre2 : Array[Bool] = [false]
  let fire_post2 : Array[Bool] = [true]
  vstdp_clopath_step(state, p, fire_pre2, fire_post2, w, 0.1F)
  assert_true(w[0] > 0.5F)
}

///|
test "vstdp_low_voltage_depresses" {
  let p = VStdpParam::new()
  let v_init : Array[Float] = [-85.0F]
  let state = VStdpState::new(1, 1, v_init)
  let fire_pre : Array[Bool] = [true]
  let fire_post : Array[Bool] = [false]
  let w : Array[Float] = [0.5F]
  vstdp_clopath_step(state, p, fire_pre, fire_post, w, 0.1F)
  let fire_pre2 : Array[Bool] = [false]
  let fire_post2 : Array[Bool] = [true]
  vstdp_clopath_step(state, p, fire_pre2, fire_post2, w, 0.1F)
  assert_true(w[0] < 0.5F)
}

///|
test "vstdp_rest_voltage_no_change" {
  let p = VStdpParam::new()
  let v_init : Array[Float] = [-70.0F]
  let state = VStdpState::new(1, 1, v_init)
  let fire_pre : Array[Bool] = [true]
  let fire_post : Array[Bool] = [false]
  let w : Array[Float] = [0.5F]
  vstdp_clopath_step(state, p, fire_pre, fire_post, w, 0.1F)
  let fire_pre2 : Array[Bool] = [false]
  let fire_post2 : Array[Bool] = [true]
  vstdp_clopath_step(state, p, fire_pre2, fire_post2, w, 0.1F)
  assert_true((w[0] - 0.5F).abs() < 1.0e-7F)
}

///|
test "vstdp_trace_decay" {
  let p = VStdpParam::new()
  let v_init : Array[Float] = [-70.0F]
  let state = VStdpState::new(1, 1, v_init)
  let fire_pre : Array[Bool] = [true]
  let fire_post : Array[Bool] = [false]
  let w : Array[Float] = [0.5F]
  vstdp_clopath_step(state, p, fire_pre, fire_post, w, 0.1F)
  for _ in 0..<100 {
    let fire_pre_n : Array[Bool] = [false]
    let fire_post_n : Array[Bool] = [false]
    vstdp_clopath_step(state, p, fire_pre_n, fire_post_n, w, 0.1F)
  }
  assert_true(state.tpre[0] > 0.0F && state.tpre[0] < 1.0F)
}

///|
test "vstdp_clamp_wmin" {
  let p : VStdpParam = {
    a_ltp: 1.0F, a_ltd: 1.0F,
    v_rest: -70.0F, delta_v: 5.0F,
    tau_pre: 100.0F, tau_post: 100.0F,
    w_min: 0.0F, w_max: 1.0F,
  }
  let v_init : Array[Float] = [-90.0F]
  let state = VStdpState::new(1, 1, v_init)
  let fire_pre : Array[Bool] = [true]
  let fire_post : Array[Bool] = [true]
  let w : Array[Float] = [0.5F]
  for _ in 0..<10 {
    vstdp_clopath_step(state, p, fire_pre, fire_post, w, 0.1F)
  }
  assert_true(w[0] <= 0.5F)
  assert_true(w[0] >= 0.0F)
}