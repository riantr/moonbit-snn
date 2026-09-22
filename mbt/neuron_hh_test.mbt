// Tests for the HH (Hodgkin-Huxley) neuron.

///|
test "HHParameter defaults" {
  let p = HHParameter::new()
  // Cm = 1uF * cm^(-2) * 20000um^2 = 1e6 * 1 * 2e-4 = 200
  assert_eq(p.cm, 200.0F)
  // gl = 5e-5siemens * cm^(-2) * 20000um^2 = 5e4 nS * 2e-4 = 10
  assert_eq(p.gl, 10.0F)
  assert_eq(p.el, -65.0F)
  assert_eq(p.ek, -90.0F)
  assert_eq(p.en, 50.0F)
  // gn = 100msiemens * cm^(-2) * 20000um^2 = 1e8 nS * 2e-4 = 20000
  assert_eq(p.gn, 20000.0F)
  // gk = 30msiemens * cm^(-2) * 20000um^2 = 3e7 nS * 2e-4 = 6000
  assert_eq(p.gk, 6000.0F)
  assert_eq(p.vt, -63.0F)
  assert_eq(p.tau_e, 5.0F)
  assert_eq(p.tau_i, 10.0F)
}

///|
test "HH::new initial state" {
  let param = HHParameter::new()
  let rng = Xoshiro::default()
  let p = HH::new(3, param, rng)
  for k in 0..<3 {
    assert_eq(p.m[k], 0.0F)
    assert_eq(p.n_gate[k], 0.0F)
    assert_eq(p.h[k], 1.0F)
    assert_eq(p.ge[k], 0.0F)
    assert_eq(p.gi[k], 0.0F)
    assert_eq(p.fire[k], false)
  }
  // v should be near el (mean = el - 5)
  for k in 0..<3 {
    let v = p.v[k]
    if (v - param.el).abs() > 20.0F {
      fail("v[\{k}] = \{v} not within 20 of El = \{param.el}")
    }
  }
}
