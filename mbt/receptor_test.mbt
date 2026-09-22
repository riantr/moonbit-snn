// Tests for the receptor primitives (NMDA / AMPA / GABAa / GABAb)
// in receptor.mbt. Mirrors SNNModels.jl/test/syn/receptors / julia
// ReceptorSynapse update_synapses! / synaptic_current! semantics.

///|
test "nmda_gating: B(v) ≈ 1 at very hyperpolarized v" {
  // At v = -100 mV, exp(k*v) = exp(7.7) ≈ 2200; mg/b = 0.298;
  // B ≈ 1 / (1 + 0.298 * 2200) ≈ 1.5e-3 (very small = full block).
  let nmda = NMDAVoltageDependency::eyal()
  let b = nmda_gating(-100.0F, nmda)
  if b > 0.01F {
    fail("B(-100) = \{b}, expected very small (full Mg block)")
  }
}

///|
test "nmda_gating: B(v) increases as v depolarizes" {
  let nmda = NMDAVoltageDependency::eyal()
  let b_low = nmda_gating(-80.0F, nmda)
  let b_high = nmda_gating(-40.0F, nmda)
  if b_high <= b_low {
    fail("B(-40) = \{b_high} should exceed B(-80) = \{b_low}")
  }
}

///|
test "nmda_gating: B(v) saturates to 1 at very depolarized v" {
  // At v = 50 mV, exp(-3.85) ≈ 0.021; B ≈ 1 / (1 + 0.298 * 0.021) ≈ 0.994.
  let nmda = NMDAVoltageDependency::eyal()
  let b = nmda_gating(50.0F, nmda)
  if b < 0.9F {
    fail("B(50) = \{b}, expected ~0.99 (released block)")
  }
}

///|
test "Receptor::simple: alpha + inv fields computed correctly" {
  let r = Receptor::simple(0.0F, 1.0F, 6.0F, 1.0F, "glu")
  // alpha = (τd - τr) / (τd * τr) = (6-1)/(6*1) = 5/6 ≈ 0.833
  if (r.alpha - 5.0F / 6.0F).abs() > 1.0e-4F {
    fail("alpha = \{r.alpha}, expected 0.833")
  }
  if (r.tau_r_inv - 1.0F).abs() > 1.0e-6F {
    fail("τr_inv = \{r.tau_r_inv}, expected 1.0")
  }
  if (r.tau_d_inv - 1.0F / 6.0F).abs() > 1.0e-4F {
    fail("τd_inv = \{r.tau_d_inv}, expected 0.1667")
  }
  if r.is_nmda {
    fail("simple receptor should not be NMDA")
  }
}

///|
test "Receptor::nmda: is_nmda flag set" {
  let r = Receptor::nmda(0.0F, 8.0F, 35.0F, 1.31F)
  if !r.is_nmda {
    fail("NMDA receptor should have is_nmda = true")
  }
}

///|
test "step_receptor: 2-state ODE decays g after pulse" {
  let g : Array[Float] = [0.0F]
  let h : Array[Float] = [0.0F]
  let target : Array[Float] = [10.0F]
  let r = Receptor::simple(0.0F, 1.0F, 6.0F, 1.0F, "glu")
  step_receptor(g, h, target, r, 0.125F)
  // After one step:
  //   h = 10 * α = 10 * 5/6 = 8.333
  //   g = exp(-dt/τd) * (0 + dt * h) = exp(-0.125/6) * (0.125 * 8.333)
  //     ≈ 0.9793 * 1.0417 ≈ 1.020
  //   h = exp(-dt/τr) * 8.333 = exp(-0.125) * 8.333 ≈ 0.8825 * 8.333 ≈ 7.354
  //   target = 0 (consumed)
  if g[0] < 0.5F || g[0] > 2.0F {
    fail("g[0] = \{g[0]}, expected ~1.0")
  }
  if h[0] < 6.0F || h[0] > 9.0F {
    fail("h[0] = \{h[0]}, expected ~7.4")
  }
  if target[0] != 0.0F {
    fail("target[0] = \{target[0]}, expected 0 (consumed)")
  }
}

///|
test "step_receptor: multiple steps decay to zero" {
  let g : Array[Float] = [0.0F]
  let h : Array[Float] = [0.0F]
  let target : Array[Float] = [10.0F]
  let r = Receptor::simple(0.0F, 0.5F, 3.0F, 1.0F, "glu")
  // Inject once, then run 1000 steps with no further input.
  step_receptor(g, h, target, r, 0.125F)
  let g_peak = g[0]
  for _ in 0..<1000 {
    let empty : Array[Float] = [0.0F]
    step_receptor(g, h, empty, r, 0.125F)
  }
  if g_peak <= 0.0F {
    fail("g_peak should be positive, got \{g_peak}")
  }
  if g[0] > 1.0e-3F {
    fail("g should decay to near 0 after 1000 steps, got \{g[0]}")
  }
}

///|
test "receptor_current: simple (non-NMDA) drives I = gsyn*g*(v-e_rev)" {
  let g : Array[Float] = [1.0F]
  let v : Array[Float] = [-50.0F]
  let out : Array[Float] = [0.0F]
  let r = Receptor::simple(0.0F, 1.0F, 6.0F, 1.0F, "glu")
  let nmda = NMDAVoltageDependency::eyal()
  receptor_current(g, v, r, nmda, out)
  // I = gsyn * g * (v - e_rev) = gsyn * 1.0 * (-50 - 0)
  let expected = r.gsyn * -50.0F
  if (out[0] - expected).abs() > 1.0e-3F {
    fail("I = \{out[0]}, expected \{expected}")
  }
}

///|
test "receptor_current: NMDA gate reduces current at hyperpolarized v" {
  let g : Array[Float] = [1.0F]
  let v_hyp : Array[Float] = [-80.0F]
  let v_dep : Array[Float] = [-20.0F]
  let out_hyp : Array[Float] = [0.0F]
  let out_dep : Array[Float] = [0.0F]
  let r = Receptor::nmda(0.0F, 8.0F, 35.0F, 1.31F)
  let nmda = NMDAVoltageDependency::eyal()
  receptor_current(g, v_hyp, r, nmda, out_hyp)
  receptor_current(g, v_dep, r, nmda, out_dep)
  // NMDA gate should reduce the current at -80 mV (still some drive
  // because v-e_rev = -80, but B(v=-80) is small).
  // At -20 mV, B is much larger.
  // Compare magnitudes (absolute values).
  let i_hyp_mag = if out_hyp[0] < 0.0F { -out_hyp[0] } else { out_hyp[0] }
  let i_dep_mag = if out_dep[0] < 0.0F { -out_dep[0] } else { out_dep[0] }
  if i_dep_mag <= i_hyp_mag {
    fail("I_dep = \{out_dep[0]} should have larger magnitude than I_hyp = \{out_hyp[0]}")
  }
}

///|
test "Receptors::new: 4 elements with correct ordering" {
  let ampa = Receptor::simple(0.0F, 0.26F, 2.0F, 0.73F, "glu")
  let nmda = Receptor::nmda(0.0F, 8.0F, 35.0F, 1.31F)
  let gabaa = Receptor::simple(-70.0F, 0.1F, 15.0F, 0.38F, "gaba")
  let gabab = Receptor::simple(-90.0F, 30.0F, 400.0F, 0.006F, "gaba")
  let rs = Receptors::new(ampa, nmda, gabaa, gabab)
  if rs.rec.length() != 4 {
    fail("rec.length = \{rs.rec.length()}, expected 4")
  }
  if rs.rec[0].e_rev != 0.0F {
    fail("rec[0].e_rev = \{rs.rec[0].e_rev}, expected 0 (AMPA)")
  }
  if !rs.rec[1].is_nmda {
    fail("rec[1] should be NMDA")
  }
  if rs.rec[2].e_rev != -70.0F {
    fail("rec[2].e_rev = \{rs.rec[2].e_rev}, expected -70 (GABAa)")
  }
}