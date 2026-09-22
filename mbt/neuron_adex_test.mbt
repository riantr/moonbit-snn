// Tests for the AdEx neuron.

///|
test "AdEx::new initialises v in [Vr, Vt)" {
  let param = AdExParameter::new()
  let rng = Xoshiro::from_state(1UL, 2UL, 3UL, 4UL)
  let p = AdEx::new(5, param, rng)
  for k in 0..<5 {
    let v = p.v[k]
    if v < param.vr || v >= param.vt {
      fail("v[\{k}] = \{v} out of [Vr, Vt) = [\{param.vr}, \{param.vt})")
    }
  }
}

///|
test "AdEx::new initialises threshold to Vt" {
  let param = AdExParameter::new()
  let rng = Xoshiro::default()
  let p = AdEx::new(3, param, rng)
  for k in 0..<3 {
    assert_eq(p.threshold[k], param.vt)
  }
}

///|
test "AdEx::new initialises tabs to 1" {
  let param = AdExParameter::new()
  let rng = Xoshiro::default()
  let p = AdEx::new(3, param, rng)
  for k in 0..<3 {
    assert_eq(p.tabs[k], 1)
  }
}

///|
test "AdExParameter defaults match Julia" {
  let p = AdExParameter::new()
  assert_eq(p.c, 281.0F)
  assert_eq(p.gl, 40.0F)
  assert_eq(p.vt, -50.0F)
  assert_eq(p.vr, -70.6F)
  assert_eq(p.el, -70.6F)
  // tm = C/gl = 281/40 = 7.025 (Float32)
  assert_eq(p.tm, 7.025F)
  // R = 1/gl = 1/40 = 0.025 (Float32)
  assert_eq(p.r, 0.025F)
  assert_eq(p.dt_slope, 2.0F)
  assert_eq(p.tw, 144.0F)
  assert_eq(p.a, 4.0F)
  assert_eq(p.b, 80.5F)
}
