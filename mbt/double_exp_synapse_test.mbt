// double_exp_synapse_test.mbt — tests for v0.40.5 DoubleExpSynapse.
//
// Validates the conductance-based double-exp synapse implemented
// in `synapse_params.mbt` (already existed as a stub, now promoted
// to pub(all) with the Julia-reference Euler step).
//
// Tests cover:
//   - default parameter shape
//   - zero-initialised vars
//   - step zeros the glu/gaba inputs (consumes them)
//   - step drives ge / gi positive on input
//   - current computation (sign / magnitude / sum)

///|
test "double_exp_synapse_default_param" {
  let p = DoubleExpSynapse::new()
  assert_true(p.tau_re > 0.0F && p.tau_de > 0.0F)
  assert_true(p.tau_ri > 0.0F && p.tau_di > 0.0F)
  assert_true(p.gsyn_e > 0.0F && p.gsyn_i > 0.0F)
  // Defaults match Julia: tau_re=1ms, tau_de=6ms, tau_ri=0.5ms, tau_di=2ms.
  assert_true((p.tau_re - 1.0F).abs() < 1.0e-6F)
  assert_true((p.tau_de - 6.0F).abs() < 1.0e-6F)
  assert_true((p.tau_ri - 0.5F).abs() < 1.0e-6F)
  assert_true((p.tau_di - 2.0F).abs() < 1.0e-6F)
}

///|
test "double_exp_synapse_vars_init_zero" {
  let v = DoubleExpSynapseVars::new(5)
  assert_eq(v.n, 5)
  for i in 0..<5 {
    assert_true(v.ge[i] == 0.0F)
    assert_true(v.gi[i] == 0.0F)
    assert_true(v.he[i] == 0.0F)
    assert_true(v.hi[i] == 0.0F)
  }
}

///|
test "double_exp_synapse_step_uses_julia_euler" {
  // Julia reference for one dt=0.1ms step with glu=1, gaba=0,
  // starting from all-zero state:
  //   ge[0] += 0.1 * (0 - 0) / 6 = 0
  //   he[0] += 0.1 * (0) / 1 + 1 = 1.0      <- impulse on he
  // After step: ge = 0, he = 1.0.
  let p = DoubleExpSynapse::new()
  let vars = DoubleExpSynapseVars::new(1)
  let glu : Array[Float] = [1.0F]
  let gaba : Array[Float] = [0.0F]
  double_exp_synapse_step(vars, p, glu, gaba, 0.1F)
  assert_true(vars.he[0] > 0.99F && vars.he[0] < 1.01F)
  // ge reads *old* he=0, so ge stays 0.
  assert_true(vars.ge[0] == 0.0F)
}

///|
test "double_exp_synapse_step_drives_ge_after_second_step" {
  // Two-step trace (dt=0.1, glu=1 only on first step):
  //   step1: ge += 0.1*(0-0)/6 = 0
  //          he += 0.1*(0)/1 + 1 = 1.0
  //   step2: glu is consumed by caller, so set glu=0 first:
  //          ge += 0.1*(1.0 - 0)/6 = 0.01667
  //          he += 0.1*(-1.0)/1 + 0 = 0.9
  let p = DoubleExpSynapse::new()
  let vars = DoubleExpSynapseVars::new(1)
  let glu : Array[Float] = [1.0F]
  let gaba : Array[Float] = [0.0F]
  double_exp_synapse_step(vars, p, glu, gaba, 0.1F)
  // Caller clears glu between steps (the step does NOT consume).
  glu[0] = 0.0F
  double_exp_synapse_step(vars, p, glu, gaba, 0.1F)
  assert_true(vars.ge[0] > 0.015F && vars.ge[0] < 0.018F)
  assert_true(vars.he[0] > 0.89F && vars.he[0] < 0.91F)
}

///|
test "double_exp_synapse_step_deposits_gaba_into_hi" {
  let p = DoubleExpSynapse::new()
  let vars = DoubleExpSynapseVars::new(1)
  let glu : Array[Float] = [0.0F]
  let gaba : Array[Float] = [1.0F]
  double_exp_synapse_step(vars, p, glu, gaba, 0.1F)
  // After step: hi += 0 + 1 = 1.0 (gi stays 0 since gi reads *old* hi=0).
  assert_true(vars.hi[0] > 0.99F && vars.hi[0] < 1.01F)
  assert_true(vars.gi[0] == 0.0F)
}

///|
test "double_exp_synapse_current_positive_when_v_above_e_e" {
  let p = DoubleExpSynapse::new()
  let vars = DoubleExpSynapseVars::new(1)
  vars.ge[0] = 1.0F
  vars.gi[0] = 0.0F
  // v = 30 mV, E_e = 0 mV → excitatory drive = 1 * (30 - 0) * 1 = 30.
  let i_syn = double_exp_synapse_current(vars, p, [30.0F])
  assert_true(i_syn[0] > 0.0F)
  assert_true((i_syn[0] - 30.0F).abs() < 0.001F)
}

///|
test "double_exp_synapse_current_positive_when_v_above_e_i" {
  let p = DoubleExpSynapse::new()
  let vars = DoubleExpSynapseVars::new(1)
  vars.ge[0] = 0.0F
  vars.gi[0] = 1.0F
  // v = -50 mV, E_i = -75 mV → v > E_i → I_inh = 1 * (-50 - (-75)) * 1 = +25.
  let i_syn = double_exp_synapse_current(vars, p, [-50.0F])
  assert_true(i_syn[0] > 0.0F)
  assert_true((i_syn[0] - 25.0F).abs() < 0.001F)
}

///|
test "double_exp_synapse_current_sum_exc_inh" {
  let p = DoubleExpSynapse::new()
  let vars = DoubleExpSynapseVars::new(1)
  vars.ge[0] = 1.0F
  vars.gi[0] = 1.0F
  // v = -50: exc = 1·(-50 - 0)·1 = -50, inh = 1·(-50 - (-75))·1 = +25. Sum = -25.
  let i_syn = double_exp_synapse_current(vars, p, [-50.0F])
  assert_true((i_syn[0] - -25.0F).abs() < 0.001F)
}

///|
test "double_exp_synapse_no_spike_no_current_change" {
  let p = DoubleExpSynapse::new()
  let vars = DoubleExpSynapseVars::new(2)
  let glu : Array[Float] = [0.0F, 0.0F]
  let gaba : Array[Float] = [0.0F, 0.0F]
  // All state zero, no spike → still zero after step.
  double_exp_synapse_step(vars, p, glu, gaba, 0.1F)
  let i_syn = double_exp_synapse_current(vars, p, [10.0F, -50.0F])
  for i in 0..<2 {
    assert_true(i_syn[i] == 0.0F)
  }
}

///|
test "double_exp_synapse_steady_state_ge_for_constant_glu" {
  // With glu=1 every step (impulse rate 1/ms), Julia-Euler:
  //   he_new = he + dt*(-he)/tau_re + glu
  // Steady state: 0 = -he_ss*dt/tau_re + 1 ⇒ he_ss = tau_re/dt = 1/0.1 = 10.
  //   ge_new = ge + dt*(he - ge)/tau_de
  // Steady state: ge_ss = he_ss = 10.
  let p = DoubleExpSynapse::new()
  let vars = DoubleExpSynapseVars::new(1)
  let glu : Array[Float] = [1.0F]
  let gaba : Array[Float] = [0.0F]
  for _ in 0..<2000 {
    glu[0] = 1.0F
    double_exp_synapse_step(vars, p, glu, gaba, 0.1F)
  }
  // Both should converge to ~10.0 after many iterations.
  let he_ss = p.tau_re / 0.1F
  let ge_ss = he_ss
  assert_true((vars.he[0] - he_ss).abs() < 0.5F)
  assert_true((vars.ge[0] - ge_ss).abs() < 0.5F)
}