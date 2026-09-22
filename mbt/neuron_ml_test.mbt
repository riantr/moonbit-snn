// Tests for the Morris-Lecar neuron.

///|
test "MorrisLecarParameter defaults" {
  let p = MorrisLecarParameter::new()
  assert_eq(p.cm, 6.69F)
  assert_eq(p.el, -50.0F)
  assert_eq(p.ek, -70.0F)
  assert_eq(p.eca, 100.0F)
  assert_eq(p.gl, 0.5F)
  assert_eq(p.gk, 2.0F)
  assert_eq(p.gca, 1.1F)
  assert_eq(p.tau_e, 5.0F)
  assert_eq(p.tau_i, 10.0F)
  assert_eq(p.v1, 30.0F)
  assert_eq(p.v2, 15.0F)
  assert_eq(p.v3, 0.0F)
  assert_eq(p.v4, 30.0F)
  // phi = 25 Hz = 0.025 in internal units
  assert_eq(p.phi, 0.025F)
}

///|
test "MorrisLecar::new initial state" {
  let param = MorrisLecarParameter::new()
  let rng = Xoshiro::default()
  let p = MorrisLecar::new(3, param, rng)
  for k in 0..<3 {
    assert_eq(p.v[k], -52.14F)
    assert_eq(p.w[k], 0.2F)
    assert_eq(p.fire[k], false)
    assert_eq(p.ge[k], 0.0F)
    assert_eq(p.gi[k], 0.0F)
  }
}

///|
test "MorrisLecar one step is stable" {
  let param = MorrisLecarParameter::new()
  let rng = Xoshiro::default()
  let p = MorrisLecar::new(1, param, rng)
  p.i[0] = 100.0F
  step_ml(p, 0.125F)
  // Just check v changed and didn't blow up
  if p.v[0] == -52.14F {
    fail("v did not change after one step")
  }
  if p.v[0].is_nan() {
    fail("v is NaN")
  }
}
