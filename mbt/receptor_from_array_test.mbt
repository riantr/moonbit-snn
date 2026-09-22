// receptor_from_array_test.mbt — tests for Receptors::from_array.

///|
test "Receptors::from_array — length 4 array" {
  let ampa = Receptor::simple(0.0F, 0.26F, 2.0F, 0.73F, "glu")
  let nmda = Receptor::nmda(0.0F, 8.0F, 35.0F, 1.31F)
  let gabaa = Receptor::simple(-70.0F, 4.8F, 29.0F, 0.27F, "gaba")
  let gabab = Receptor::simple(-90.0F, 30.0F, 400.0F, 0.006F, "gaba")
  let rs = Receptors::from_array([ampa, nmda, gabaa, gabab])
  if rs.rec.length() != 4 { fail("expected 4 entries") }
  if rs.rec[0].g0 != 0.73F { fail("AMPA g0 mismatch") }
  if rs.rec[1].tau_d != 35.0F { fail("NMDA τd mismatch") }
  if rs.rec[2].e_rev != -70.0F { fail("GABAa E_rev mismatch") }
  if rs.rec[3].e_rev != -90.0F { fail("GABAb E_rev mismatch") }
}

///|
test "Receptors::from_array — same as Receptors::new with same args" {
  let ampa = Receptor::simple(0.0F, 0.26F, 2.0F, 0.73F, "glu")
  let nmda = Receptor::nmda(0.0F, 8.0F, 35.0F, 1.31F)
  let gabaa = Receptor::simple(-70.0F, 4.8F, 29.0F, 0.27F, "gaba")
  let gabab = Receptor::simple(-90.0F, 30.0F, 400.0F, 0.006F, "gaba")
  let rs_a = Receptors::from_array([ampa, nmda, gabaa, gabab])
  let rs_b = Receptors::new(ampa, nmda, gabaa, gabab)
  // Same field shape.
  let mut k = 0
  while k < 4 {
    if rs_a.rec[k].g0 != rs_b.rec[k].g0 {
      fail("rec[\{k}] g0 mismatch")
    }
    if rs_a.rec[k].e_rev != rs_b.rec[k].e_rev {
      fail("rec[\{k}] E_rev mismatch")
    }
    k = k + 1
  }
}

///|
test "Receptors::from_array — index ordering preserved" {
  // Verify index 0=AMPA, 1=NMDA, 2=GABAa, 3=GABAb.
  let r0 = Receptor::simple(0.0F, 0.1F, 1.0F, 1.0F, "glu")
  let r1 = Receptor::nmda(0.0F, 0.2F, 2.0F, 2.0F)
  let r2 = Receptor::simple(-70.0F, 0.3F, 3.0F, 3.0F, "gaba")
  let r3 = Receptor::simple(-90.0F, 0.4F, 4.0F, 4.0F, "gaba")
  let rs = Receptors::from_array([r0, r1, r2, r3])
  if rs.rec[0].g0 != 1.0F { fail("rec[0] expected g0=1.0") }
  if rs.rec[1].g0 != 2.0F { fail("rec[1] expected g0=2.0") }
  if rs.rec[2].g0 != 3.0F { fail("rec[2] expected g0=3.0") }
  if rs.rec[3].g0 != 4.0F { fail("rec[3] expected g0=4.0") }
}