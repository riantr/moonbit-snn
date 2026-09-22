// Tests for the BallAndStick multi-compartment neuron (soma + 1 dendrite).

///|
test "BallAndStick::new — initial state" {
  let soma_param = AdExParameter::new()
  let rng = Xoshiro::default()
  let p = BallAndStick::new(3, soma_param, rng)
  assert_eq(p.n, 3)
  assert_eq(p.v_s.length(), 3)
  assert_eq(p.v_d.length(), 3)
  assert_eq(p.w_s.length(), 3)
  assert_eq(p.fire.length(), 3)
  assert_eq(p.threshold.length(), 3)
  assert_eq(p.tabs.length(), 3)
  assert_eq(p.dv.length(), 9)  // 3 neurons × 3 (v_s, v_d, w_s)
  assert_eq(p.dv_temp.length(), 9)
  // v_s and v_d in [vr, vt].
  for k in 0..<3 {
    if p.v_s[k] < soma_param.vr || p.v_s[k] > soma_param.vt {
      fail("v_s[\{k}] = \{p.v_s[k]} outside [vr, vt]")
    }
    if p.v_d[k] < soma_param.vr || p.v_d[k] > soma_param.vt {
      fail("v_d[\{k}] = \{p.v_d[k]} outside [vr, vt]")
    }
    if p.threshold[k] != soma_param.vt {
      fail("threshold[\{k}] = \{p.threshold[k]} != vt")
    }
    if p.tabs[k] != 1 {
      fail("tabs[\{k}] = \{p.tabs[k]} != 1")
    }
    if p.fire[k] {
      fail("fire[\{k}] should be false at init")
    }
    if p.w_s[k] != 0.0F {
      fail("w_s[\{k}] = \{p.w_s[k]} should be 0 at init")
    }
  }
  // Synapse defaults match AdExSinExp.
  assert_eq(p.tau_e, 6.0F)
  assert_eq(p.tau_i, 2.0F)
  assert_eq(p.e_e, 0.0F)
  assert_eq(p.e_i, -75.0F)
  assert_eq(p.gsyn_e, 1.0F)
  assert_eq(p.gsyn_i, 1.0F)
}

///|
test "BallAndStick — no input, v_s and v_d decay toward el" {
  let soma_param = AdExParameter::new()
  let rng = Xoshiro::default()
  let p = BallAndStick::new(1, soma_param, rng)
  // Start both soma and dendrite at rest.
  p.v_s[0] = soma_param.el
  p.v_d[0] = soma_param.el
  let dt = 0.125F
  for _ in 0..<2000 {
    step_ballandstick(p, dt)
  }
  // After 250ms with no input, v_s and v_d should be close to el.
  let vs = p.v_s[0]
  let vd = p.v_d[0]
  let el = soma_param.el
  let diff_s = if vs > el { vs - el } else { el - vs }
  let diff_d = if vd > el { vd - el } else { el - vd }
  if diff_s > 1.0F {
    fail("v_s[0] = \{vs} did not decay to el = \{el} (diff = \{diff_s})")
  }
  if diff_d > 1.0F {
    fail("v_d[0] = \{vd} did not decay to el = \{el} (diff = \{diff_d})")
  }
}

///|
test "BallAndStick — soma spike fires when threshold reached" {
  let soma_param = AdExParameter::with_vr(-60.0F)  // closer to LKD Vr
  let rng = Xoshiro::default()
  let p = BallAndStick::new(1, soma_param, rng)
  // Drive the soma strongly with 1500 pA (default AdEx params need a lot).
  p.i_s[0] = 1500.0F
  let dt = 0.125F
  let mut fired = false
  let mut steps = 0
  while not(fired) && steps < 5000 {
    step_ballandstick(p, dt)
    if p.fire[0] {
      fired = true
    }
    steps = steps + 1
  }
  if not(fired) {
    fail("BallAndStick soma never fired under 1500 pA drive within 5000 steps")
  }
  // v_s should be at AP_membrane (10mV per Julia's PostSpike default;
  // note: AdEx hardcodes 20mV — this is an existing inconsistency).
  assert_eq(p.v_s[0], 10.0F)
}

///|
test "BallAndStick — single-exp synapse step on soma" {
  let soma_param = AdExParameter::new()
  let rng = Xoshiro::default()
  let p = BallAndStick::new(1, soma_param, rng)
  // Inject a spike into glu_s.
  p.glu_s[0] = 1.0F
  let dt = 0.125F
  ballandstick_soma_step_synapses(p, dt)
  // glu_s consumed, ge_s > 0.
  assert_eq(p.glu_s[0], 0.0F)
  if p.ge_s[0] <= 0.0F {
    fail("ge_s[0] = \{p.ge_s[0]} should be > 0 after step")
  }
  let expected : Float = 1.0F - dt / p.tau_e
  if p.ge_s[0] != expected {
    fail("ge_s[0] = \{p.ge_s[0]} != expected = \{expected}")
  }
}

///|
test "BallAndStick — single-exp synapse step on dendrite" {
  let soma_param = AdExParameter::new()
  let rng = Xoshiro::default()
  let p = BallAndStick::new(1, soma_param, rng)
  p.gaba_d[0] = 1.0F
  let dt = 0.125F
  ballandstick_dend_step_synapses(p, dt)
  assert_eq(p.gaba_d[0], 0.0F)
  let expected : Float = 1.0F - dt / p.tau_i
  if p.gi_d[0] != expected {
    fail("gi_d[0] = \{p.gi_d[0]} != expected = \{expected}")
  }
}

///|
test "BallAndStick — Heun integration runs without NaN" {
  let soma_param = AdExParameter::with_vr(-60.0F)
  let rng = Xoshiro::default()
  let p = BallAndStick::new(2, soma_param, rng)
  p.i_s[0] = 1000.0F
  p.i_s[1] = -100.0F  // hyperpolarizing
  let dt = 0.125F
  for _ in 0..<200 {
    step_ballandstick(p, dt)
  }
  // No NaN, no infinities.
  for k in 0..<2 {
    if p.v_s[k].is_nan() {
      fail("v_s[\{k}] is NaN")
    }
    if p.v_d[k].is_nan() {
      fail("v_d[\{k}] is NaN")
    }
    if p.w_s[k].is_nan() {
      fail("w_s[\{k}] is NaN")
    }
  }
}