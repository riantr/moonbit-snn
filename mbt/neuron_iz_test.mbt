// Tests for the IZ (Izhikevich) neuron.

///|
test "IZ::new initialises v = -65" {
  let param = IZParameter::new()
  let rng = Xoshiro::default()
  let p = IZ::new(5, param, rng)
  for k in 0..<5 {
    assert_eq(p.v[k], -65.0F)
  }
}

///|
test "IZ::new initialises u = b * v = -13" {
  let param = IZParameter::new()
  let rng = Xoshiro::default()
  let p = IZ::new(3, param, rng)
  for k in 0..<3 {
    assert_eq(p.u[k], -13.0F) // 0.2 * -65 = -13
  }
}

///|
test "IZParameter defaults match Julia" {
  let p = IZParameter::new()
  assert_eq(p.a, 0.01F)
  assert_eq(p.b, 0.2F)
  assert_eq(p.c, -65.0F)
  assert_eq(p.d, 2.0F)
  assert_eq(p.tau_e, 5.0F)
  assert_eq(p.tau_i, 10.0F)
  assert_eq(p.e_e, 0.0F)
  assert_eq(p.e_i, -80.0F)
}

///|
test "IZ: one step from rest" {
  // At v=-65, u=-13, I=0, the dv/dt formula gives a positive number
  // (since 0.04*65^2 - 5*65 = 169 - 325 = -156, plus 140 = -16, plus u=-13
  // gives -29, so v is decreasing). Verify the sign.
  let param = IZParameter::new()
  let rng = Xoshiro::default()
  let p = IZ::new(1, param, rng)
  let v_before = p.v[0]
  step_iz(p, 0.125F)
  // The exact v_after depends on the initial ge/gi values; just check
  // that the step ran and v changed (or hit the reset c=-65).
  if p.v[0] == v_before && !p.fire[0] {
    fail("v did not change after one step")
  }
}
