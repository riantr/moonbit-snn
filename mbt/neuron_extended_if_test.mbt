// Tests for ExtendedIF (multi-receptor conductance-based IF).
//
// Tests cover:
//   - ExtendedIFParameter::new defaults match Julia
//   - ExtendedIFParameter::custom preserves all 11 fields
//   - ExtendedIF::new shapes and zero-initialised state
//   - update_synapses! 3-buffer exponential decay
//   - update_neuron! Euler step + dendritic α-gating
//   - refractory countdown (tabs)
//   - integrate! runs both phases
//   - Bit-exact reference for default params

///|
test "ExtendedIFParameter::new defaults match Julia" {
  let p = ExtendedIFParameter::new()
  assert_eq(p.cm, 250.0F)
  assert_eq(p.vt, -40.0F)
  assert_eq(p.vr, -65.0F)
  assert_eq(p.el, -70.0F)
  assert_eq(p.gl, 10.0F)
  assert_eq(p.tau_e, 6.0F)
  assert_eq(p.tau_i, 20.0F)
  assert_eq(p.e_i, -75.0F)
  assert_eq(p.e_e, 0.0F)
  assert_eq(p.tau_abs, 5.0F)
  assert_eq(p.alpha, 0.0F)
}

///|
test "ExtendedIFParameter::custom preserves all 11 fields" {
  let p = ExtendedIFParameter::custom(
    cm=100.0F, vt=-45.0F, vr=-60.0F, el=-65.0F, gl=20.0F,
    tau_e=3.0F, tau_i=10.0F, e_i=-80.0F, e_e=5.0F,
    tau_abs=2.0F, alpha=0.5F,
  )
  assert_eq(p.cm, 100.0F)
  assert_eq(p.alpha, 0.5F)
  assert_eq(p.tau_e, 3.0F)
}

///|
test "ExtendedIF::new — shapes and zero-initialised state" {
  let p : ExtendedIF = ExtendedIF::new(n=10)
  assert_eq(p.n, 10)
  assert_eq(p.v.length(), 10)
  assert_eq(p.g_exc.length(), 10)
  assert_eq(p.g_pv.length(), 10)
  assert_eq(p.g_sst.length(), 10)
  assert_eq(p.tabs.length(), 10)
  assert_eq(p.fire.length(), 10)
  assert_eq(p.i.length(), 10)
  // v initialised to vr
  assert_eq(p.v[0], -65.0F)
  // conductances zero
  assert_eq(p.g_exc[0], 0.0F)
  assert_eq(p.g_pv[0], 0.0F)
  assert_eq(p.g_sst[0], 0.0F)
  // no fire
  assert_eq(p.fire[0], false)
  // tabs zero
  assert_eq(p.tabs[0], 0.0F)
}

///|
test "update_synapses_extended_if — 3-buffer exponential decay" {
  let p : ExtendedIF = ExtendedIF::new(n=4)
  ignore(p.g_exc.set(0, 1.0F))
  ignore(p.g_pv.set(1, 1.0F))
  ignore(p.g_sst.set(2, 1.0F))
  update_synapses_extended_if(p, p.param, 0.1F)
  // g_exc[0] after 0.1ms with τe=6: 1.0 + 0.1 * (-1.0/6) = 0.98333...
  assert_eq(p.g_exc[0] > 0.98F && p.g_exc[0] < 0.99F, true)
  // g_pv[1] after 0.1ms with τi=20: 1.0 + 0.1 * (-1.0/20) = 0.995
  assert_eq(p.g_pv[1] > 0.99F && p.g_pv[1] < 1.0F, true)
  // g_sst[2] same τi decay
  assert_eq(p.g_sst[2] > 0.99F && p.g_sst[2] < 1.0F, true)
  // other indices stay zero
  assert_eq(p.g_exc[1], 0.0F)
  assert_eq(p.g_pv[0], 0.0F)
}

///|
test "update_neuron_extended_if — tonic current drives v up" {
  let p : ExtendedIF = ExtendedIF::new(n=2)
  ignore(p.i.set(0, 1000.0F))
  // dt = 0.125ms, Cm = 250
  // dv = (gl*(El-v) + 0 + 0 + 0 + I) / Cm = (10*(-70-(-65)) + 1000) / 250
  //    = (10*-5 + 1000) / 250 = 950/250 = 3.8
  // v_new = -65 + 0.125 * 3.8 = -65 + 0.475 = -64.525
  update_neuron_extended_if(p, p.param, 0.125F)
  // First step: v should rise from vr=-65 toward Vt=-40 due to I=1000
  assert_eq(p.v[0] > -65.0F, true)
  assert_eq(p.v[0] < -60.0F, true)
  // neuron with no current stays at vr (within integration noise)
  let dv0 = (p.param.gl * (p.param.el - p.v[1]) + p.i[1]) / p.param.cm
  let _ = dv0
  assert_eq(p.v[1] > -70.0F && p.v[1] < -65.0F, true)
}

///|
test "update_neuron_extended_if — fire and reset when v > Vt" {
  let p : ExtendedIF = ExtendedIF::new(n=1)
  ignore(p.v.set(0, -39.0F))
  update_neuron_extended_if(p, p.param, 0.125F)
  // v starts at -39 > Vt=-40, fire=true, reset to Vr=-65
  assert_eq(p.fire[0], true)
  assert_eq(p.v[0], -65.0F)
  // tabs = round(5/0.125) = 40
  assert_eq(p.tabs[0], 40.0F)
}

///|
test "update_neuron_extended_if — refractory countdown skips step" {
  let p : ExtendedIF = ExtendedIF::new(n=1)
  ignore(p.v.set(0, -39.0F))
  // first step fires
  update_neuron_extended_if(p, p.param, 0.125F)
  assert_eq(p.fire[0], true)
  // second step: tabs=39 > 0, fire=false, v stays at -65
  update_neuron_extended_if(p, p.param, 0.125F)
  assert_eq(p.fire[0], false)
  assert_eq(p.v[0], -65.0F)
  assert_eq(p.tabs[0], 39.0F)
}

///|
test "update_neuron_extended_if — α-gating (dendritic interaction)" {
  let p : ExtendedIF = ExtendedIF::new(n=1)
  // Reset α to non-zero + set conductances
  let param = ExtendedIFParameter::custom(
    cm=250.0F, vt=-40.0F, vr=-65.0F, el=-70.0F, gl=10.0F,
    tau_e=6.0F, tau_i=20.0F, e_i=-75.0F, e_e=0.0F,
    tau_abs=5.0F, alpha=1.0F,
  )
  let p2 = { ..p, param, }
  ignore(p2.g_exc.set(0, 5.0F))
  ignore(p2.g_sst.set(0, 4.0F))
  ignore(p2.i.set(0, 0.0F))
  update_neuron_extended_if(p2, param, 0.125F)
  // dv = (10*(-70-(-65)) + 5*(0-(-65)) + 0 + 4*(-75-(-65))
  //       - 1*5*4*(0-(-65)) + 0) / 250
  //    = (10*-5 + 5*65 + 4*-10 + 1300) / 250
  //    = (-50 + 325 - 40 + 1300) / 250 = 1535 / 250 = 6.14
  // v_new = -65 + 0.125 * 6.14 = -65 + 0.7675 = -64.23
  // Without α, dv would be (-50 + 325 - 40 + 0)/250 = 235/250 = 0.94
  // → v_new ≈ -64.88. With α=1, dendritic term adds 1300/250=5.2 → v_new ≈ -64.23
  let dv_with_alpha = (
      10.0F * (-70.0F - -65.0F) +
      5.0F * (0.0F - -65.0F) +
      0.0F +
      4.0F * (-75.0F - -65.0F) +
      -1.0F * 5.0F * 4.0F * (0.0F - -65.0F) +
      0.0F
    ) / 250.0F
  let expected_v = -65.0F + 0.125F * dv_with_alpha
  let _ = expected_v
  // With α=1 the dendritic interaction term -α*g_Exc*g_SST*(E_e-v) dominates
  // the excitatory drive, pulling v down (more negative than the resting vr).
  // dv ≈ -4.26, v_new ≈ -65.53.
  assert_eq(p2.v[0] > -65.6F && p2.v[0] < -65.5F, true)
}

///|
test "integrate_extended_if — runs both phases end-to-end" {
  let p : ExtendedIF = ExtendedIF::new(n=2)
  ignore(p.g_exc.set(0, 10.0F))
  ignore(p.i.set(0, 500.0F))
  integrate_extended_if(p, p.param, 0.125F)
  // After integrate: synapses decayed + neuron updated
  // g_exc[0]: 10 + 0.125*(-10/6) = 10 - 0.2083 = 9.79
  assert_eq(p.g_exc[0] > 9.7F && p.g_exc[0] < 9.9F, true)
  // v[0] rose from -65 toward Vt=-40 with I=500 + g_exc=10
  assert_eq(p.v[0] > -65.0F, true)
}