// parameters_extra_test.mbt — port of
// `refs/SNNModels.jl/test/pop/parameters.jl` (subset).
//
// Julia has many parameter default + custom-constructor tests. Most
// are already covered by other test files (IFParameter defaults,
// AdExParameter defaults, IZParameter, MorrisLecarParameter,
// HetRecParameter default, PostSpike). This file ports the
// missing-from-our-testset cases:
//   - HetRecParameter custom (Nd / τm / overlap)
//   - NMDAVoltageDependency custom (mg / b / k)
//   - Receptors always has 4 entries (AMPA / NMDA / GABAa / GABAb)
//   - Receptor + Receptors custom with explicit values

///|
test "HetRecParameter::new — defaults" {
  let p = @neuron.HetRecParameter::new()
  // Julia defaults: Nd=2, overlap=0.5, τm=20, τabs=5, τrate=100.
  assert_eq(p.nd, 2)
  assert_eq(p.overlap, 0.5F)
  assert_eq(p.tau_m, 20.0F)
  assert_eq(p.tau_abs, 5.0F)
  assert_eq(p.tau_rate, 100.0F)
}

///|
test "HetRecParameter::custom — Nd + τm + overlap" {
  let p = @neuron.HetRecParameter::custom(4, 30.0F, 0.2F)
  // Custom: Nd=4, τm=30, overlap=0.2; other fields default.
  assert_eq(p.nd, 4)
  assert_eq(p.tau_m, 30.0F)
  assert_eq(p.overlap, 0.2F)
  // Defaults preserved.
  assert_eq(p.tau_abs, 5.0F)
  assert_eq(p.tau_rate, 100.0F)
}

///|
test "NMDAVoltageDependency::eyal — defaults" {
  let n = @snn_mbt.NMDAVoltageDependency::eyal()
  assert_eq(n.mg, 1.0F)
  assert_eq(n.b, 3.36F)
  assert_eq(n.k, -0.077F)
}

///|
test "NMDAVoltageDependency::custom — explicit mg / b / k" {
  let n = @snn_mbt.NMDAVoltageDependency::custom(1.0F, 3.36F, -0.077F)
  assert_eq(n.mg, 1.0F)
  assert_eq(n.b, 3.36F)
  assert_eq(n.k, -0.077F)
  // Gating function should be identical to eyal() default.
  let eyal = @snn_mbt.NMDAVoltageDependency::eyal()
  let b_at_minus_60_custom = @snn_mbt.nmda_gating(-60.0F, n)
  let b_at_minus_60_eyal = @snn_mbt.nmda_gating(-60.0F, eyal)
  let diff = (b_at_minus_60_custom - b_at_minus_60_eyal).abs()
  if diff > 1.0e-6F {
    fail("custom and eyal disagree at v=-60: \{b_at_minus_60_custom} vs \{b_at_minus_60_eyal}")
  }
}

///|
test "NMDAVoltageDependency::custom — different mg shifts gating" {
  // B(v) = 1 / (1 + (mg/b) * exp(k * v))
  // Increasing mg should reduce B(v) at hyperpolarized v.
  let n_low = @snn_mbt.NMDAVoltageDependency::custom(0.5F, 3.36F, -0.077F)
  let n_high = @snn_mbt.NMDAVoltageDependency::custom(2.0F, 3.36F, -0.077F)
  let b_low = @snn_mbt.nmda_gating(-80.0F, n_low)
  let b_high = @snn_mbt.nmda_gating(-80.0F, n_high)
  if b_low <= b_high {
    fail("Lower mg should yield higher B(v=-80): low=\{b_low}, high=\{b_high}")
  }
}

///|
test "Receptors::new — exactly 4 entries" {
  let ampa = @snn_mbt.Receptor::simple(0.0F, 0.26F, 2.0F, 0.73F, "glu")
  let nmda_rec = @snn_mbt.Receptor::nmda(0.0F, 8.0F, 35.0F, 1.31F)
  let gabaa = @snn_mbt.Receptor::simple(-70.0F, 0.1F, 15.0F, 0.38F, "gaba")
  let gabab = @snn_mbt.Receptor::simple(-90.0F, 30.0F, 400.0F, 0.006F, "gaba")
  let rs = @snn_mbt.Receptors::new(ampa, nmda_rec, gabaa, gabab)
  // Julia: Receptors always has 4 entries (AMPA, NMDA, GABAa, GABAb).
  assert_eq(rs.rec.length(), 4)
}

///|
test "Receptors::new — target field per entry" {
  let ampa = @snn_mbt.Receptor::simple(0.0F, 0.26F, 2.0F, 0.73F, "glu")
  let nmda_rec = @snn_mbt.Receptor::nmda(0.0F, 8.0F, 35.0F, 1.31F)
  let gabaa = @snn_mbt.Receptor::simple(-70.0F, 0.1F, 15.0F, 0.38F, "gaba")
  let gabab = @snn_mbt.Receptor::simple(-90.0F, 30.0F, 400.0F, 0.006F, "gaba")
  let rs = @snn_mbt.Receptors::new(ampa, nmda_rec, gabaa, gabab)
  // AMPA / NMDA → "glu"; GABAa / GABAb → "gaba".
  assert_eq(rs.rec[0].target, "glu")
  assert_eq(rs.rec[1].target, "glu")
  assert_eq(rs.rec[2].target, "gaba")
  assert_eq(rs.rec[3].target, "gaba")
  // Only NMDA has is_nmda = true.
  assert_eq(rs.rec[1].is_nmda, true)
  assert_eq(rs.rec[0].is_nmda, false)
  assert_eq(rs.rec[2].is_nmda, false)
  assert_eq(rs.rec[3].is_nmda, false)
}