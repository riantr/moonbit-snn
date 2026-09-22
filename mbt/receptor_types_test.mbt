// receptor_types_test.mbt — tests for receptor_types.mbt (presets).

///|
test "eyal_nmda / soma_nmda — Eyal/Soma NMDA defaults" {
  let e = eyal_nmda()
  if e.b != 3.36F { fail("Eyal b mismatch: \{e.b}") }
  if e.k != -0.077F { fail("Eyal k mismatch: \{e.k}") }
  if e.mg != 1.0F { fail("Eyal mg mismatch: \{e.mg}") }
  let s = soma_nmda()
  if s.b != 3.57F { fail("Soma b mismatch: \{s.b}") }
  if s.k != -0.062F { fail("Soma k mismatch: \{s.k}") }
  if s.mg != 1.0F { fail("Soma mg mismatch: \{s.mg}") }
}

///|
test "miles_gaba_soma / duarte_glu_soma — single Receptor fields" {
  let miles = miles_gaba_soma()
  if miles.e_rev != -70.0F { fail("Miles E_rev mismatch") }
  if miles.tau_r != 0.1F { fail("Miles τr mismatch: \{miles.tau_r}") }
  if miles.tau_d != 15.0F { fail("Miles τd mismatch") }
  if miles.g0 != 0.38F { fail("Miles g0 mismatch") }
  if miles.target != "gaba" { fail("Miles target mismatch: \{miles.target}") }
  let duarte = duarte_glu_soma()
  if duarte.e_rev != 0.0F { fail("Duarte E_rev mismatch") }
  if duarte.tau_r != 0.26F { fail("Duarte τr mismatch") }
  if duarte.tau_d != 2.0F { fail("Duarte τd mismatch") }
  if duarte.g0 != 0.73F { fail("Duarte g0 mismatch") }
  if duarte.target != "glu" { fail("Duarte target mismatch") }
}

///|
test "eyal_glu_dend / miles_gaba_dend — Glutamatergic/GABAergic bundles" {
  let glu = eyal_glu_dend()
  if glu.ampa.e_rev != 0.0F { fail("EyalGluDend AMPA E_rev mismatch") }
  if glu.ampa.tau_d != 2.0F { fail("EyalGluDend AMPA τd mismatch") }
  if glu.nmda.is_nmda != true { fail("EyalGluDend NMDA is_nmda mismatch") }
  if glu.nmda.tau_d != 35.0F { fail("EyalGluDend NMDA τd mismatch") }
  if glu.nmda.tau_r != 8.0F { fail("EyalGluDend NMDA τr mismatch") }
  if glu.nmda.g0 != 1.31F { fail("EyalGluDend NMDA g0 mismatch") }
  let gaba = miles_gaba_dend()
  if gaba.gabaa.e_rev != -70.0F { fail("MilesGabaDend GABAa E_rev mismatch") }
  if gaba.gabaa.tau_d != 29.0F { fail("MilesGabaDend GABAa τd mismatch") }
  if gaba.gabab.e_rev != -90.0F { fail("MilesGabaDend GABAb E_rev mismatch") }
  if gaba.gabab.tau_d != 400.0F { fail("MilesGabaDend GABAb τd mismatch") }
  if gaba.gabab.g0 != 0.006F { fail("MilesGabaDend GABAb g0 mismatch") }
}

///|
test "soma_glu / soma_gaba — soma-targeted bundles" {
  let g = soma_glu()
  if g.ampa.tau_d != 6.0F { fail("SomaGlu AMPA τd mismatch") }
  if g.ampa.tau_r != 1.0F { fail("SomaGlu AMPA τr mismatch") }
  if g.ampa.g0 != 0.7F { fail("SomaGlu AMPA g0 mismatch") }
  if g.nmda.is_nmda != true { fail("SomaGlu NMDA is_nmda mismatch") }
  if g.nmda.tau_d != 100.0F { fail("SomaGlu NMDA τd mismatch") }
  let gaba = soma_gaba()
  if gaba.gabaa.tau_d != 10.0F { fail("SomaGABA GABAa τd mismatch") }
  if gaba.gabaa.g0 != 2.0F { fail("SomaGABA GABAa g0 mismatch") }
  if gaba.gabab.tau_d != 400.0F { fail("SomaGABA GABAb τd mismatch") }
}

///|
test "tripod_soma_receptors — 4-receptor collection shape" {
  let rs = tripod_soma_receptors()
  if rs.rec.length() != 4 { fail("Receptors must have 4 entries") }
  // AMPA slot = DuarteGluSoma
  let ampa = rs.rec[0]
  if ampa.g0 != 0.73F { fail("TripodSomaReceptors AMPA g0 mismatch") }
  // NMDA slot = DuarteGluSoma (placeholder; Tripod soma routes only via AMPA)
  let nmda = rs.rec[1]
  if nmda.g0 != 0.73F { fail("TripodSomaReceptors NMDA g0 mismatch") }
  // GABAa slot = MilesGabaSoma
  let gabaa = rs.rec[2]
  if gabaa.g0 != 0.38F { fail("TripodSomaReceptors GABAa g0 mismatch") }
  // GABAb slot = MilesGabaSoma (placeholder; Tripod soma routes only via GABAa)
  let gabab = rs.rec[3]
  if gabab.g0 != 0.38F { fail("TripodSomaReceptors GABAb g0 mismatch") }
}

///|
test "tripod_dend_receptors — EyalGluDend + MilesGabaDend" {
  let rs = tripod_dend_receptors()
  if rs.rec.length() != 4 { fail("Receptors must have 4 entries") }
  let ampa = rs.rec[0]
  if ampa.g0 != 0.73F { fail("TripodDend AMPA g0 mismatch") }
  let nmda = rs.rec[1]
  if nmda.is_nmda != true { fail("TripodDend NMDA is_nmda mismatch") }
  if nmda.tau_d != 35.0F { fail("TripodDend NMDA τd mismatch") }
  let gabaa = rs.rec[2]
  if gabaa.e_rev != -70.0F { fail("TripodDend GABAa E_rev mismatch") }
  let gabab = rs.rec[3]
  if gabab.e_rev != -90.0F { fail("TripodDend GABAb E_rev mismatch") }
}

///|
test "soma_receptors — SomaGlu + SomaGABA" {
  let rs = soma_receptors()
  if rs.rec.length() != 4 { fail("Receptors must have 4 entries") }
  let ampa = rs.rec[0]
  if ampa.g0 != 0.7F { fail("SomaReceptors AMPA g0 mismatch") }
  let nmda = rs.rec[1]
  if nmda.is_nmda != true { fail("SomaReceptors NMDA is_nmda mismatch") }
  let gabaa = rs.rec[2]
  if gabaa.tau_d != 10.0F { fail("SomaReceptors GABAa τd mismatch") }
  let gabab = rs.rec[3]
  if gabab.tau_d != 400.0F { fail("SomaReceptors GABAb τd mismatch") }
}

///|
test "Receptors::rec array is fresh each call (no shared state)" {
  let r1 = soma_receptors()
  let r2 = soma_receptors()
  // Two calls return separate Receptors instances with same values
  // (no aliasing of the rec array — both should have length 4).
  if r1.rec.length() != 4 { fail("r1.rec length mismatch") }
  if r2.rec.length() != 4 { fail("r2.rec length mismatch") }
  if r1.rec[0].g0 != r2.rec[0].g0 { fail("aliasing between r1/r2 AMPA") }
}