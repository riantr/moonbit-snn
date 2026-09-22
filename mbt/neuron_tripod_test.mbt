// Tests for the Tripod multi-compartment neuron (soma + 2 dendrites).

///|
test "Tripod::new — initial state" {
  let soma_param = AdExParameter::new()
  let rng = Xoshiro::default()
  let p = Tripod::new(3, soma_param, rng)
  assert_eq(p.n, 3)
  assert_eq(p.v_s.length(), 3)
  assert_eq(p.v_d1.length(), 3)
  assert_eq(p.v_d2.length(), 3)
  assert_eq(p.w_s.length(), 3)
  assert_eq(p.fire.length(), 3)
  assert_eq(p.threshold.length(), 3)
  assert_eq(p.tabs.length(), 3)
  assert_eq(p.dv.length(), 12)  // 3 neurons × 4 (v_s, v_d1, v_d2, w_s)
  assert_eq(p.dv_temp.length(), 12)
  for k in 0..<3 {
    if p.v_s[k] < soma_param.vr || p.v_s[k] > soma_param.vt {
      fail("v_s[\{k}] = \{p.v_s[k]} outside [vr, vt]")
    }
    if p.v_d1[k] < soma_param.vr || p.v_d1[k] > soma_param.vt {
      fail("v_d1[\{k}] = \{p.v_d1[k]} outside [vr, vt]")
    }
    if p.v_d2[k] < soma_param.vr || p.v_d2[k] > soma_param.vt {
      fail("v_d2[\{k}] = \{p.v_d2[k]} outside [vr, vt]")
    }
    if p.threshold[k] != soma_param.vt {
      fail("threshold[\{k}] != vt")
    }
    if p.tabs[k] != 1 {
      fail("tabs[\{k}] != 1")
    }
    if p.fire[k] {
      fail("fire[\{k}] should be false at init")
    }
    if p.w_s[k] != 0.0F {
      fail("w_s[\{k}] should be 0 at init")
    }
  }
  // Synapse defaults.
  assert_eq(p.tau_e, 6.0F)
  assert_eq(p.tau_i, 2.0F)
  assert_eq(p.e_e, 0.0F)
  assert_eq(p.e_i, -75.0F)
}

///|
test "Tripod — no input, all compartments decay toward el" {
  let soma_param = AdExParameter::new()
  let rng = Xoshiro::default()
  let p = Tripod::new(1, soma_param, rng)
  p.v_s[0] = soma_param.el
  p.v_d1[0] = soma_param.el
  p.v_d2[0] = soma_param.el
  let dt = 0.125F
  for _ in 0..<2000 {
    step_tripod(p, dt)
  }
  let el = soma_param.el
  let vs = p.v_s[0]
  let vd1 = p.v_d1[0]
  let vd2 = p.v_d2[0]
  let diff_s = if vs > el { vs - el } else { el - vs }
  let diff_d1 = if vd1 > el { vd1 - el } else { el - vd1 }
  let diff_d2 = if vd2 > el { vd2 - el } else { el - vd2 }
  if diff_s > 1.0F {
    fail("v_s[0] = \{vs} did not decay to el = \{el} (diff = \{diff_s})")
  }
  if diff_d1 > 1.0F {
    fail("v_d1[0] = \{vd1} did not decay to el = \{el} (diff = \{diff_d1})")
  }
  if diff_d2 > 1.0F {
    fail("v_d2[0] = \{vd2} did not decay to el = \{el} (diff = \{diff_d2})")
  }
}

///|
test "Tripod — soma spike fires under strong current" {
  let soma_param = AdExParameter::with_vr(-60.0F)
  let rng = Xoshiro::default()
  let p = Tripod::new(1, soma_param, rng)
  p.i_s[0] = 1500.0F
  let dt = 0.125F
  let mut fired = false
  let mut steps = 0
  while not(fired) && steps < 5000 {
    step_tripod(p, dt)
    if p.fire[0] {
      fired = true
    }
    steps = steps + 1
  }
  if not(fired) {
    fail("Tripod soma never fired under 1500 pA drive within 5000 steps")
  }
  // v_s should be at AP_membrane (10mV per Julia's PostSpike default).
  assert_eq(p.v_s[0], 10.0F)
}

///|
test "Tripod — single-exp synapse step on soma" {
  let soma_param = AdExParameter::new()
  let rng = Xoshiro::default()
  let p = Tripod::new(1, soma_param, rng)
  p.glu_s[0] = 1.0F
  let dt = 0.125F
  tripod_soma_step_synapses(p, dt)
  assert_eq(p.glu_s[0], 0.0F)
  let expected : Float = 1.0F - dt / p.tau_e
  if p.ge_s[0] != expected {
    fail("ge_s[0] = \{p.ge_s[0]} != expected = \{expected}")
  }
}

///|
test "Tripod — single-exp synapse step on both dendrites" {
  let soma_param = AdExParameter::new()
  let rng = Xoshiro::default()
  let p = Tripod::new(1, soma_param, rng)
  p.glu_d1[0] = 1.0F
  p.gaba_d2[0] = 1.0F
  let dt = 0.125F
  tripod_dend_step_synapses(p, dt)
  // d1: exc consumed.
  if p.glu_d1[0] != 0.0F {
    fail("glu_d1[0] not reset")
  }
  let expected_e : Float = 1.0F - dt / p.tau_e
  if p.ge_d1[0] != expected_e {
    fail("ge_d1[0] = \{p.ge_d1[0]} != expected = \{expected_e}")
  }
  // d2: inh consumed.
  if p.gaba_d2[0] != 0.0F {
    fail("gaba_d2[0] not reset")
  }
  let expected_i : Float = 1.0F - dt / p.tau_i
  if p.gi_d2[0] != expected_i {
    fail("gi_d2[0] = \{p.gi_d2[0]} != expected = \{expected_i}")
  }
}

///|
test "Tripod — Heun integration runs without NaN" {
  let soma_param = AdExParameter::with_vr(-60.0F)
  let rng = Xoshiro::default()
  let p = Tripod::new(2, soma_param, rng)
  p.i_s[0] = 1000.0F
  p.i_s[1] = -100.0F
  // Inject some dendritic current.
  p.i_d1[0] = 50.0F
  p.i_d2[1] = -25.0F
  let dt = 0.125F
  for _ in 0..<200 {
    step_tripod(p, dt)
  }
  for k in 0..<2 {
    if p.v_s[k].is_nan() { fail("v_s[\{k}] is NaN") }
    if p.v_d1[k].is_nan() { fail("v_d1[\{k}] is NaN") }
    if p.v_d2[k].is_nan() { fail("v_d2[\{k}] is NaN") }
    if p.w_s[k].is_nan() { fail("w_s[\{k}] is NaN") }
  }
}