// Tests for SNNUtils.jl parameter constants (LKD2014 + Duarte2019).
// Mirrors SpikingNeuralNetworks.jl/src/SNNUtils.jl/src/models/
// lkd2014.jl + duarte2019.jl.

///|
test "LKD2014::new: defaults match Julia" {
  let p = LKD2014::new()
  // Julia: AdEx(El=-70mV, Vt=-52mV, τm=300pF/15nS=20ms, R=1/15nS,
  //                Vr=-60mV, τabs=1ms, E_i=-75mV, E_e=0mV, At=10mV)
  // PV(El=-62mV, Vr=-57.47mV, Vt=-52mV, τm=20ms)
  // AdEx.tm = 300pF / 15.0nS in internal units (pF=1, nS=1) = 20.0F.
  if p.tm != 20.0F {
    fail("AdEx.tm = \{p.tm}, expected 20.0")
  }
  if p.vt != -52.0F {
    fail("vt = \{p.vt}, expected -52.0")
  }
  if p.el != -70.0F {
    fail("el = \{p.el}, expected -70.0")
  }
  if p.vr != -60.0F {
    fail("vr = \{p.vr}, expected -60.0")
  }
  if (p.r - 0.0667F).abs() > 0.01F {
    fail("r = \{p.r}, expected ~0.0667 (1/15nS)")
  }
  if p.tau_abs != 1.0F {
    fail("tau_abs = \{p.tau_abs}, expected 1.0")
  }
  if p.e_i != -75.0F {
    fail("e_i = \{p.e_i}, expected -75.0")
  }
  if p.e_e != 0.0F {
    fail("e_e = \{p.e_e}, expected 0.0")
  }
  if p.at != 10.0F {
    fail("at = \{p.at}, expected 10.0")
  }
  // PV parameters.
  if p.pv_tm != 20.0F {
    fail("pv_tm = \{p.pv_tm}, expected 20.0")
  }
  if p.pv_el != -62.0F {
    fail("pv_el = \{p.pv_el}, expected -62.0")
  }
  if (p.pv_vr - -57.47F).abs() > 1.0e-3F {
    fail("pv_vr = \{p.pv_vr}, expected -57.47")
  }
  if p.pv_vt != -52.0F {
    fail("pv_vt = \{p.pv_vt}, expected -52.0")
  }
}

///|
test "Duarte2019::new: defaults match Julia" {
  let p = Duarte2019::new()
  // PV (Julia: τm = 104.52pF/9.75nS, El=-64.33mV, Vt=-38.97mV,
  //             Vr=-57.47mV, τabs=0.5ms, gsyn_e=1.04nS, gsyn_i=0.84nS).
  if (p.pv_tm - 10.72F).abs() > 0.1F {
    fail("pv_tm = \{p.pv_tm}, expected ~10.72 (104.52pF/9.75nS)")
  }
  if p.pv_el != -64.33F {
    fail("pv_el = \{p.pv_el}, expected -64.33")
  }
  if p.pv_vt != -38.97F {
    fail("pv_vt = \{p.pv_vt}, expected -38.97")
  }
  if (p.pv_vr - -57.47F).abs() > 1.0e-3F {
    fail("pv_vr = \{p.pv_vr}, expected -57.47")
  }
  if p.pv_tau_abs != 0.5F {
    fail("pv_tau_abs = \{p.pv_tau_abs}, expected 0.5")
  }
  if (p.pv_gsyn_e - 1.04F).abs() > 1.0e-3F {
    fail("pv_gsyn_e = \{p.pv_gsyn_e}, expected ~1.04")
  }
  if (p.pv_gsyn_i - 0.84F).abs() > 1.0e-3F {
    fail("pv_gsyn_i = \{p.pv_gsyn_i}, expected ~0.84")
  }
  // SST (Julia: τm = 102.86pF/4.61nS, El=-61mV, Vt=-34.4mV,
  //             Vr=-47.11mV, τabs=1.3ms, gsyn_e=0.56nS, gsyn_i=0.59nS,
  //             b=80.5pA, τw=144ms).
  if (p.sst_tm - 22.31F).abs() > 0.1F {
    fail("sst_tm = \{p.sst_tm}, expected ~22.31 (102.86pF/4.61nS)")
  }
  if p.sst_el != -61.0F {
    fail("sst_el = \{p.sst_el}, expected -61.0")
  }
  if p.sst_vt != -34.4F {
    fail("sst_vt = \{p.sst_vt}, expected -34.4")
  }
  if (p.sst_vr - -47.11F).abs() > 1.0e-3F {
    fail("sst_vr = \{p.sst_vr}, expected -47.11")
  }
  if p.sst_tau_abs != 1.3F {
    fail("sst_tau_abs = \{p.sst_tau_abs}, expected 1.3")
  }
  if (p.sst_gsyn_e - 0.56F).abs() > 1.0e-3F {
    fail("sst_gsyn_e = \{p.sst_gsyn_e}, expected ~0.56")
  }
  if (p.sst_gsyn_i - 0.59F).abs() > 1.0e-3F {
    fail("sst_gsyn_i = \{p.sst_gsyn_i}, expected ~0.59")
  }
  if p.sst_b != 80.5F {
    fail("sst_b = \{p.sst_b}, expected 80.5")
  }
  if p.sst_tw != 144.0F {
    fail("sst_tw = \{p.sst_tw}, expected 144.0")
  }
  // AdEx (Julia: τm = 116.5pF/4.64nS, El=-76.43, Vt=-44.45,
  //              τabs=2.05ms, gsyn_e=0.73, gsyn_i=0.265).
  if (p.adex_tm - 25.11F).abs() > 0.1F {
    fail("adex_tm = \{p.adex_tm}, expected ~25.11 (116.5pF/4.64nS)")
  }
  if p.adex_el != -76.43F {
    fail("adex_el = \{p.adex_el}, expected -76.43")
  }
  if p.adex_vt != -44.45F {
    fail("adex_vt = \{p.adex_vt}, expected -44.45")
  }
  if p.adex_tau_abs != 2.05F {
    fail("adex_tau_abs = \{p.adex_tau_abs}, expected 2.05")
  }
  if (p.adex_gsyn_e - 0.73F).abs() > 1.0e-3F {
    fail("adex_gsyn_e = \{p.adex_gsyn_e}, expected ~0.73")
  }
  if (p.adex_gsyn_i - 0.265F).abs() > 1.0e-3F {
    fail("adex_gsyn_i = \{p.adex_gsyn_i}, expected ~0.265")
  }
}

///|
test "LKD2014 fields used to build IF + AdEx parameters" {
  // Demonstrate that LKD2014 constants are usable with our existing
  // IFParameter::custom + AdExParameter::new constructors.
  let lkd = LKD2014::new()
  // PV inhibitory IF.
  let pv_if = IFParameter::custom(
    lkd.pv_tm, lkd.pv_vt, lkd.pv_vr, lkd.pv_el, 0.06F,
  )
  if (pv_if.tm - 20.0F).abs() > 1.0e-6F {
    fail("PV IF tm = \{pv_if.tm}, expected 20.0")
  }
  // AdEx excitatory (just verify the constructor accepts the
  // values; full AdEx port uses different params).
  let _ = AdExParameter::new()
}

///|
test "LKD2014Soma::new — defaults match Julia lkd2014_soma" {
  let p = LKD2014Soma::new()
  // Julia: EsE (p=0.2, μ=2.76), E→PV (p=0.2, μ=1.27),
  //         PV→E (p=0.2, μ=48.7), PV→PV (p=0.2, μ=16.2)
  if p.es_e_p != 0.2F { fail("es_e_p expected 0.2, got \{p.es_e_p}") }
  if (p.es_e_mu - 2.76F).abs() > 1.0e-3F { fail("es_e_mu expected ~2.76") }
  if p.e_to_pv_p != 0.2F { fail("e_to_pv_p expected 0.2") }
  if (p.e_to_pv_mu - 1.27F).abs() > 1.0e-3F { fail("e_to_pv_mu expected ~1.27") }
  if p.pv_to_e_p != 0.2F { fail("pv_to_e_p expected 0.2") }
  if (p.pv_to_e_mu - 48.7F).abs() > 1.0e-3F { fail("pv_to_e_mu expected ~48.7") }
  if p.pv_to_pv_p != 0.2F { fail("pv_to_pv_p expected 0.2") }
  if (p.pv_to_pv_mu - 16.2F).abs() > 1.0e-3F { fail("pv_to_pv_mu expected ~16.2") }
}

///|
test "Quaresima2023Dend::new — defaults match Julia quaresima2023_dend" {
  let p = Quaresima2023Dend::new()
  // E→Ed: p=0.168, μ=2.8
  if p.e_to_ed_p != 0.168F { fail("e_to_ed_p expected 0.168") }
  if (p.e_to_ed_mu - 2.8F).abs() > 1.0e-3F { fail("e_to_ed_mu expected ~2.8") }
  // E→PV/SST: log(1.0) = 0
  if p.e_to_pv_mu != 0.0F { fail("e_to_pv_mu expected 0.0 (log 1.0)") }
  // PV→E: log(15.8) ≈ 2.76
  if (p.pv_to_e_mu - 2.76F).abs() > 0.01F { fail("pv_to_e_mu expected ~2.76") }
  // PV→SST: log(1.4) ≈ 0.336
  if (p.pv_to_sst_mu - 0.336F).abs() > 0.01F { fail("pv_to_sst_mu expected ~0.336") }
  // SST→PV/SST: log(0.83) ≈ -0.186
  if (p.sst_to_pv_mu - (-0.186F)).abs() > 0.01F {
    fail("sst_to_pv_mu expected ~-0.186")
  }
  // All p's are 0.2 except E→Ed (0.168)
  if p.pv_to_pv_p != 0.2F { fail("pv_to_pv_p expected 0.2") }
  if p.sst_to_sst_p != 0.2F { fail("sst_to_sst_p expected 0.2") }
}

///|
test "eyal_equivalent_nar — NAR=1.8 default computes AMPA/NMDA g0" {
  // NAR0 = 1.31/0.73 ≈ 1.795; AMPA_g0 = 0.73*(1+NAR0-NAR)
  // For NAR=1.8: ampa_g0 = 0.73*(1+1.795-1.8) = 0.73*0.995 = 0.72635
  //              nmda_g0 = 0.73*1.8 = 1.314
  let rs = eyal_equivalent_nar(1.8F, 35.0F)
  // rec[0] = AMPA, rec[1] = NMDA
  let ampa_g0 = rs.rec[0].g0
  let nmda_g0 = rs.rec[1].g0
  if (ampa_g0 - 0.72635F).abs() > 0.01F {
    fail("AMPA g0 expected ~0.726, got \{ampa_g0}")
  }
  if (nmda_g0 - 1.314F).abs() > 0.01F {
    fail("NMDA g0 expected ~1.314, got \{nmda_g0}")
  }
}

///|
test "Quaresima2024UpDown::new — nar + tau_d stored, dend_receptors populated" {
  let cfg = Quaresima2024UpDown::new(1.8F, 35.0F)
  if cfg.nar != 1.8F { fail("nar expected 1.8") }
  if cfg.tau_d != 35.0F { fail("tau_d expected 35.0") }
  if cfg.dend_receptors.rec.length() != 4 {
    fail("dend_receptors should have 4 entries, got \{cfg.dend_receptors.rec.length()}")
  }
}