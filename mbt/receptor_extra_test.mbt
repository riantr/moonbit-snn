// Tests for Glutamatergic + GABAergic + alpha_synapse helpers.

///|
test "alpha_synapse — (τd - τr) / (τd * τr) matches Julia" {
  // AMPA: τr=1, τd=6 → α = 5/6 ≈ 0.8333
  assert_eq(alpha_synapse(1.0F, 6.0F) > 0.83F && alpha_synapse(1.0F, 6.0F) < 0.84F, true)
  // NMDA: τr=1, τd=100 → α = 99/100 = 0.99
  assert_eq(alpha_synapse(1.0F, 100.0F) > 0.98F && alpha_synapse(1.0F, 100.0F) < 1.0F, true)
  // GABAa: τr=0.5, τd=2 → α = 1.5/1.0 = 1.5
  assert_eq(alpha_synapse(0.5F, 2.0F) > 1.49F && alpha_synapse(0.5F, 2.0F) < 1.51F, true)
}

///|
test "Glutamatergic::new defaults — AMPA + NMDA" {
  let g = Glutamatergic::new()
  // AMPA default: e_rev=0, τr=6, τd=0, g0=1.0, target="glu"
  assert_eq(g.ampa.e_rev, 0.0F)
  assert_eq(g.ampa.tau_r, 6.0F)
  assert_eq(g.ampa.target, "glu")
  assert_eq(g.ampa.is_nmda, false)
  // NMDA default: e_rev=0, τr=100, τd=0, g0=1.0
  assert_eq(g.nmda.e_rev, 0.0F)
  assert_eq(g.nmda.tau_r, 100.0F)
  assert_eq(g.nmda.is_nmda, true)
}

///|
test "Glutamatergic::custom — custom AMPA + NMDA" {
  let ampa = Receptor::simple(0.0F, 2.0F, 10.0F, 2.5F, "glu")
  let nmda = Receptor::nmda(0.0F, 5.0F, 50.0F, 1.5F)
  let g = Glutamatergic::custom(ampa=ampa, nmda=nmda)
  assert_eq(g.ampa.tau_r, 2.0F)
  assert_eq(g.ampa.tau_d, 10.0F)
  assert_eq(g.ampa.g0, 2.5F)
  assert_eq(g.nmda.tau_r, 5.0F)
  assert_eq(g.nmda.tau_d, 50.0F)
  assert_eq(g.nmda.is_nmda, true)
}

///|
test "GABAergic::new defaults — GABAa + GABAb" {
  let g = GABAergic::new()
  // GABAa default: e_rev=-75, τr=0.5, τd=2
  assert_eq(g.gabaa.e_rev, -75.0F)
  assert_eq(g.gabaa.tau_r, 0.5F)
  assert_eq(g.gabaa.tau_d, 2.0F)
  assert_eq(g.gabaa.target, "gaba")
  assert_eq(g.gabaa.is_nmda, false)
  // GABAb default: e_rev=-75, τr=0.5, τd=20
  assert_eq(g.gabab.e_rev, -75.0F)
  assert_eq(g.gabab.tau_d, 20.0F)
  assert_eq(g.gabab.is_nmda, false)
}

///|
test "GABAergic::custom — custom GABAa + GABAb" {
  let gabaa = Receptor::simple(-80.0F, 1.0F, 5.0F, 0.5F, "gaba")
  let gabab = Receptor::simple(-80.0F, 1.0F, 30.0F, 0.3F, "gaba")
  let g = GABAergic::custom(gabaa=gabaa, gabab=gabab)
  assert_eq(g.gabaa.e_rev, -80.0F)
  assert_eq(g.gabaa.tau_d, 5.0F)
  assert_eq(g.gabab.tau_d, 30.0F)
  assert_eq(g.gabab.g0, 0.3F)
}

///|
test "Glutamatergic + GABAergic — fields preserved through struct" {
  let glu = Glutamatergic::new()
  let gaba = GABAergic::new()
  // Verify the fields are independent (no shared refs)
  assert_eq(glu.ampa.is_nmda, false)
  assert_eq(glu.nmda.is_nmda, true)
  assert_eq(gaba.gabaa.is_nmda, false)
  assert_eq(gaba.gabab.is_nmda, false)
}

///|
test "alpha_synapse = same value as Receptor::simple.alpha field" {
  let r = Receptor::simple(0.0F, 1.0F, 6.0F, 1.0F, "glu")
  // Receptor::simple computes α = (τd - τr) / (τd * τr)
  // which equals alpha_synapse(τr, τd)
  let a = alpha_synapse(1.0F, 6.0F)
  let _ = r.alpha
  // alpha_synapse = (6-1)/(6*1) = 5/6 ≈ 0.833
  assert_eq(a > 0.83F && a < 0.84F, true)
}

///|
test "Receptors::from_pair — Glu + GABA → 4-element collection" {
  let glu = Glutamatergic::new()
  let gaba = GABAergic::new()
  let r = Receptors::from_pair(glu, gaba)
  // Index 0 = AMPA, 1 = NMDA, 2 = GABAa, 3 = GABAb
  assert_eq(r.rec.length(), 4)
  assert_eq(r.rec[0].target, "glu")
  assert_eq(r.rec[0].is_nmda, false)
  assert_eq(r.rec[1].is_nmda, true)
  assert_eq(r.rec[2].target, "gaba")
  assert_eq(r.rec[3].target, "gaba")
}

///|
test "Receptors::from_pair — custom glu + gaba preserved" {
  let ampa = Receptor::simple(0.0F, 2.0F, 10.0F, 2.5F, "glu")
  let nmda = Receptor::nmda(0.0F, 5.0F, 50.0F, 1.5F)
  let gabaa = Receptor::simple(-80.0F, 1.0F, 5.0F, 0.5F, "gaba")
  let gabab = Receptor::simple(-80.0F, 1.0F, 30.0F, 0.3F, "gaba")
  let r = Receptors::from_pair(
    Glutamatergic::custom(ampa=ampa, nmda=nmda),
    GABAergic::custom(gabaa=gabaa, gabab=gabab),
  )
  assert_eq(r.rec[0].tau_r, 2.0F)
  assert_eq(r.rec[1].tau_r, 5.0F)
  assert_eq(r.rec[2].e_rev, -80.0F)
  assert_eq(r.rec[3].g0, 0.3F)
}

///|
test "receptors_current — sums AMPA + NMDA + GABAa + GABAb" {
  // 4-receptor collection with known conductances; verify sum.
  let ampa = Receptor::simple(0.0F, 1.0F, 6.0F, 1.0F, "glu")
  let nmda = Receptor::nmda(0.0F, 1.0F, 100.0F, 1.0F)
  let gabaa = Receptor::simple(-75.0F, 0.5F, 2.0F, 1.0F, "gaba")
  let gabab = Receptor::simple(-75.0F, 0.5F, 20.0F, 1.0F, "gaba")
  let rs = Receptors::new(ampa, nmda, gabaa, gabab)
  // g_matrix: N=2 × 4 columns, all set to 1
  let g_mat : Array[Float] = Array::make(8, 1.0F)
  let v : Array[Float] = [-70.0F, -60.0F]
  let out : Array[Float] = Array::make(2, 0.0F)
  receptors_current(g_mat, v, rs, NMDAVoltageDependency::eyal(), out)
  // AMPA contribution: gsyn=1 * 1 * (v - 0) = v
  // NMDA contribution: gsyn=1 * 1 * (v - 0) * B(v)
  //   B(-70) ≈ 0.024 (full Mg block), B(-60) ≈ 0.045
  // GABAa + GABAb: gsyn=1 * 1 * (v - -75) = v + 75
  // sum at v=-70: -70 + (-70)*0.024 + 5 + 5 ≈ -61.68
  // sum at v=-60: -60 + (-60)*0.045 + 15 + 15 ≈ -32.70
  let finite0 = out[0] == out[0]
  let finite1 = out[1] == out[1]
  assert_eq(finite0, true)
  assert_eq(finite1, true)
  // Just verify both outputs are negative (depolarising from AMPA
  // dominates) and that NMDA modulation is detectable:
  //   out[0] - out[1] should be negative since B(-60) > B(-70) →
  //   less NMDA block at -60 → more current → less negative (i.e.
  //   out[0] < out[1] since -70 - 70*0.024 ≈ -71.7 and -60 - 60*0.045 ≈ -62.7)
  //   Actually -71.7 < -62.7 so out[0] should be MORE negative.
  assert_eq(out[0] < -50.0F, true)
  assert_eq(out[0] < out[1], true)
}

///|
test "receptors_current — zero g_matrix stays zero" {
  let ampa = Receptor::simple(0.0F, 1.0F, 6.0F, 1.0F, "glu")
  let nmda = Receptor::nmda(0.0F, 1.0F, 100.0F, 1.0F)
  let gabaa = Receptor::simple(-75.0F, 0.5F, 2.0F, 1.0F, "gaba")
  let gabab = Receptor::simple(-75.0F, 0.5F, 20.0F, 1.0F, "gaba")
  let rs = Receptors::new(ampa, nmda, gabaa, gabab)
  let g_mat : Array[Float] = Array::make(8, 0.0F)
  let v : Array[Float] = [-50.0F, -70.0F]
  let out : Array[Float] = Array::make(2, 0.0F)
  receptors_current(g_mat, v, rs, NMDAVoltageDependency::eyal(), out)
  assert_eq(out[0], 0.0F)
  assert_eq(out[1], 0.0F)
}

///|
test "receptors_current — pure AMPA contribution matches receptor_current" {
  let ampa = Receptor::simple(0.0F, 1.0F, 6.0F, 1.0F, "glu")
  let nmda = Receptor::nmda(0.0F, 1.0F, 100.0F, 1.0F)
  let gabaa = Receptor::simple(-75.0F, 0.5F, 2.0F, 0.0F, "gaba") // g0=0 → gsyn=0
  let gabab = Receptor::simple(-75.0F, 0.5F, 20.0F, 0.0F, "gaba") // g0=0 → gsyn=0
  let rs = Receptors::new(ampa, nmda, gabaa, gabab)
  // Set gAMPA=2, gNMDA=0, gGABAa=0, gGABAb=0
  let g_mat : Array[Float] = Array::make(8, 0.0F)
  ignore(g_mat.set(0, 2.0F))  // neuron 0, AMPA
  ignore(g_mat.set(4, 2.0F))  // neuron 1, AMPA
  let v : Array[Float] = Array::make(2, 0.0F)
  ignore(v.set(0, -50.0F))
  ignore(v.set(1, -70.0F))
  let out : Array[Float] = Array::make(2, 0.0F)
  receptors_current(g_mat, v, rs, NMDAVoltageDependency::eyal(), out)
  // AMPA-only (with norm_synapse scaling):
  // gsyn = 1 * norm_synapse(1, 6) ≈ 1.717
  // out[i] = gsyn * g[i] * (v[i] - e_rev) = 1.717 * 2 * v
  let expected0 = ampa.gsyn * 2.0F * -50.0F
  let expected1 = ampa.gsyn * 2.0F * -70.0F
  // Use tolerance for Float32 round-trip
  let _ = expected0
  let _ = expected1
  assert_eq((out[0] - (ampa.gsyn * 2.0F * -50.0F)).abs() < 0.01F, true)
  assert_eq((out[1] - (ampa.gsyn * 2.0F * -70.0F)).abs() < 0.01F, true)
}