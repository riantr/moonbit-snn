// neuron_iz_custom_test.mbt — tests for IZParameter::custom.

///|
test "IZParameter::custom — arbitrary (a, b, c, d) round-trip" {
  let p = IZParameter::custom(0.05F, 0.15F, -55.0F, 5.0F)
  if p.a != 0.05F { fail("a expected 0.05, got \{p.a}") }
  if p.b != 0.15F { fail("b expected 0.15, got \{p.b}") }
  if p.c != -55.0F { fail("c expected -55.0, got \{p.c}") }
  if p.d != 5.0F { fail("d expected 5.0, got \{p.d}") }
  // Defaults preserved.
  if p.tau_e != 5.0F { fail("tau_e default expected 5.0") }
  if p.tau_i != 10.0F { fail("tau_i default expected 10.0") }
  if p.e_e != 0.0F { fail("e_e default expected 0.0") }
  if p.e_i != -80.0F { fail("e_i default expected -80.0") }
}

///|
test "IZParameter::custom — IZ::new accepts the custom params" {
  let p = IZParameter::custom(0.05F, 0.15F, -55.0F, 5.0F)
  let rng = Xoshiro::new(42UL)
  let pop = IZ::new(3, p, rng)
  // Julia source: u[k] = b * v[k], where v[k] is initialised to -65 (not c).
  // So u[k] = b * -65 = 0.15 * -65 = -9.75.
  let expected_u : Float = 0.15F * (-65.0F)
  if (pop.u[0] - expected_u).abs() > 1.0e-4 {
    fail("u[0] expected \{expected_u}, got \{pop.u[0]}")
  }
}

///|
test "IZParameter::custom — matches rs() for matching (a, b, c, d)" {
  // Both rs() and custom(0.02, 0.2, -65, 8) should give the same struct.
  let prs = IZParameter::rs()
  let pcu = IZParameter::custom(0.02F, 0.2F, -65.0F, 8.0F)
  if prs.a != pcu.a { fail("a mismatch: \{prs.a} vs \{pcu.a}") }
  if prs.b != pcu.b { fail("b mismatch: \{prs.b} vs \{pcu.b}") }
  if prs.c != pcu.c { fail("c mismatch: \{prs.c} vs \{pcu.c}") }
  if prs.d != pcu.d { fail("d mismatch: \{prs.d} vs \{pcu.d}") }
}