// Tests for AdExSinExp (Adaptive Exponential IF with single-exp synapses).
//
// Verifies:
//   1. Default parameter values match Julia's AdExSinExpParameter().
//   2. LKD 2014-style parameter helper (lkd_adex) sets El=-70mV, Vt=-52mV.
//   3. New population: v spread [vr, vt], threshold=vt, tabs=1.
//   4. SinExp step function: with no input, v decays to el (steady state).
//   5. SinExp step: with strong current, v reaches threshold and fires.

///|
test "AdExSinExpParameter::new — defaults match AdExParameter" {
  let p = AdExSinExpParameter::new()
  assert_eq(p.c, 281.0F)
  assert_eq(p.gl, 40.0F)
  assert_eq(p.vt, -50.0F)
  assert_eq(p.vr, -70.6F)
  assert_eq(p.el, -70.6F)
  // tm = c/gl = 281/40 = 7.025 (Float32)
  let expected_tm : Float = 281.0F / 40.0F
  assert_eq(p.tm, expected_tm)
  let expected_r : Float = 1.0F / 40.0F
  assert_eq(p.r, expected_r)
  assert_eq(p.dt_slope, 2.0F)
  assert_eq(p.tw, 144.0F)
  assert_eq(p.a, 4.0F)
  assert_eq(p.b, 80.5F)
}

///|
test "AdExSinExpParameter::lkd_adex — LKD 2014 values" {
  let p = AdExSinExpParameter::lkd_adex()
  assert_eq(p.c, 300.0F)
  assert_eq(p.gl, 15.0F)
  assert_eq(p.vt, -52.0F)
  assert_eq(p.vr, -60.0F)
  assert_eq(p.el, -70.0F)
  // tm = c/gl = 300/15 = 20.0
  assert_eq(p.tm, 20.0F)
  // r = 1/gl = 1/15
  let expected_r : Float = 1.0F / 15.0F
  assert_eq(p.r, expected_r)
}

///|
test "AdExSinExp::new — initial state" {
  let p = AdExSinExpParameter::new()
  let rng = Xoshiro::default()
  let pop = AdExSinExp::new(5, p, rng)
  assert_eq(pop.n, 5)
  assert_eq(pop.v.length(), 5)
  assert_eq(pop.w.length(), 5)
  assert_eq(pop.fire.length(), 5)
  assert_eq(pop.threshold.length(), 5)
  assert_eq(pop.tabs.length(), 5)
  assert_eq(pop.ge.length(), 5)
  assert_eq(pop.gi.length(), 5)
  assert_eq(pop.glu.length(), 5)
  assert_eq(pop.gaba.length(), 5)
  // No he/hi in SinExp variant.
  assert_eq(pop.tau_e, 6.0F)
  assert_eq(pop.tau_i, 2.0F)
  assert_eq(pop.e_e, 0.0F)
  assert_eq(pop.e_i, -75.0F)
  // All v in [vr, vt]
  for k in 0..<5 {
    if pop.v[k] < p.vr || pop.v[k] > p.vt {
      fail("v[\{k}] = \{pop.v[k]} outside [vr, vt]")
    }
    if pop.threshold[k] != p.vt {
      fail("threshold[\{k}] != vt")
    }
    if pop.tabs[k] != 1 {
      fail("tabs[\{k}] != 1")
    }
    if pop.fire[k] {
      fail("fire[\{k}] should be false at init")
    }
  }
}

///|
test "AdExSinExp — no input, v decays toward el (steady state)" {
  let p = AdExSinExpParameter::new()
  let rng = Xoshiro::default()
  let pop = AdExSinExp::new(1, p, rng)
  // Start with v at the resting potential (skip first-step init noise).
  pop.v[0] = -70.6F
  let dt = 0.125F
  for _ in 0..<2000 {
    adex_sinexp_step_synapses(pop, dt)
    adex_sinexp_synaptic_current(pop)
    step_adex_sinexp(pop, dt)
  }
  // After 250ms with no input, v should be very close to el.
  // We don't require exact equality (w may have small contribution),
  // but |v - el| should be < 0.1mV.
  let v0 = pop.v[0]
  let diff = if v0 > p.el { v0 - p.el } else { p.el - v0 }
  if diff > 0.1F {
    fail("v[0] = \{v0} did not decay to el = \{p.el} (diff = \{diff})")
  }
}

///|
test "AdExSinExp — sin-exp synapse step: glu is consumed into ge" {
  let p = AdExSinExpParameter::new()
  let rng = Xoshiro::default()
  let pop = AdExSinExp::new(1, p, rng)
  // Inject a spike into the glu buffer (simulating a pre-synaptic spike).
  pop.glu[0] = 1.0F
  let dt = 0.125F
  adex_sinexp_step_synapses(pop, dt)
  // glu is consumed.
  if pop.glu[0] != 0.0F {
    fail("glu[0] = \{pop.glu[0]} should be reset to 0 after step")
  }
  // ge should be > 0 (received 1 nS then decayed slightly).
  if pop.ge[0] <= 0.0F {
    fail("ge[0] = \{pop.ge[0]} should be > 0 after step")
  }
  // With dt=0.125 and τe=6, decay factor is dt/τe = 0.0208.
  // ge = 1.0 * (1 - dt/τe) ≈ 0.9792
  let expected = 1.0F - dt / pop.tau_e
  if pop.ge[0] != expected {
    fail("ge[0] = \{pop.ge[0]} != expected = \{expected}")
  }
}

///|
test "AdExSinExp — strong current drives v to threshold and fires" {
  // Use LKD-style params (tm=20ms, El=-70, Vt=-52, Vr=-60): with
  // I=350pA, v_ss = -70 + 350/15 ≈ -46.7mV, which is above Vt=-52
  // and the exponential term pushes it over 0 quickly.
  let p = AdExSinExpParameter::lkd_adex()
  let rng = Xoshiro::default()
  let pop = AdExSinExp::new(1, p, rng)
  pop.v[0] = p.el  // start at rest
  pop.i[0] = 350.0F  // tonic current
  let dt = 0.125F
  let mut fired = false
  let mut steps = 0
  while not(fired) && steps < 5000 {
    adex_sinexp_step_synapses(pop, dt)
    adex_sinexp_synaptic_current(pop)
    step_adex_sinexp(pop, dt)
    if pop.fire[0] {
      fired = true
    }
    steps = steps + 1
  }
  if not(fired) {
    fail("AdExSinExp never fired under 350 pA drive within 5000 steps")
  }
  // v should be at AP_membrane (20mV) after the spike.
  assert_eq(pop.v[0], 20.0F)
}

///|
test "AdExSinExp — gaba injection into inhibitory channel" {
  // Verify the gi side of the sin-exp synapse (gaba → gi with τi decay).
  let p = AdExSinExpParameter::new()
  let rng = Xoshiro::default()
  let pop = AdExSinExp::new(1, p, rng)
  pop.gaba[0] = 1.0F
  let dt = 0.125F
  adex_sinexp_step_synapses(pop, dt)
  // gaba consumed; gi > 0.
  if pop.gaba[0] != 0.0F {
    fail("gaba[0] = \{pop.gaba[0]} should be 0 after step")
  }
  if pop.gi[0] <= 0.0F {
    fail("gi[0] = \{pop.gi[0]} should be > 0 after step")
  }
  // With dt=0.125 and τi=2, decay factor is dt/τi = 0.0625.
  // gi = 1.0 * (1 - dt/τi) ≈ 0.9375
  let expected = 1.0F - dt / pop.tau_i
  if pop.gi[0] != expected {
    fail("gi[0] = \{pop.gi[0]} != expected = \{expected}")
  }
}
