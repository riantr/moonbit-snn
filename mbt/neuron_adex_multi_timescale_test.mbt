// Tests for AdExMultiTimescale (multi-timescale AdEx + dynamic threshold).

///|
test "AdExMultiTimescaleParameter::new defaults match Julia" {
  let p = AdExMultiTimescaleParameter::new()
  assert_eq(p.tm > 7.0F && p.tm < 7.05F, true)
  assert_eq(p.vt, -50.0F)
  assert_eq(p.vr, -70.6F)
  assert_eq(p.el, -70.6F)
  assert_eq(p.r, 0.025F)
  assert_eq(p.dt_slope, 2.0F)
  assert_eq(p.v_spike, 20.0F)
  assert_eq(p.tau_w, 144.0F)
  assert_eq(p.a, 4.0F)
  assert_eq(p.b, 80.5F)
  assert_eq(p.tau_abs, 1.0F)
  assert_eq(p.tau_r.length(), 2)
  assert_eq(p.tau_d.length(), 2)
  assert_eq(p.tau_r[0], 1.0F)
  assert_eq(p.tau_d[1], 2.0F)
  assert_eq(p.glu_receptors.length(), 1)
  assert_eq(p.gaba_receptors.length(), 1)
  assert_eq(p.glu_receptors[0], 0)
  assert_eq(p.gaba_receptors[0], 1)
  assert_eq(p.e_e, 0.0F)
  assert_eq(p.e_i, -75.0F)
  assert_eq(p.gsyn_e, 1.0F)
  assert_eq(p.gsyn_i, 1.0F)
  assert_eq(p.at, 10.0F)
  assert_eq(p.tau_t, 30.0F)
}

///|
test "AdExMultiTimescale::new — shapes and zero-initialised state" {
  let p : AdExMultiTimescale = AdExMultiTimescale::new(n=4)
  assert_eq(p.n, 4)
  assert_eq(p.n_receptors, 2)
  assert_eq(p.v.length(), 4)
  assert_eq(p.w.length(), 4)
  assert_eq(p.fire.length(), 4)
  assert_eq(p.theta.length(), 4)
  assert_eq(p.tabs.length(), 4)
  assert_eq(p.i.length(), 4)
  assert_eq(p.syn_curr.length(), 4)
  assert_eq(p.g_buf.length(), 8) // 4 * 2 receptors
  assert_eq(p.h_buf.length(), 8)
  // initial state
  assert_eq(p.v[0], -70.6F)
  assert_eq(p.w[0], 0.0F)
  assert_eq(p.fire[0], false)
  assert_eq(p.theta[0], -50.0F) // starts at Vt
  assert_eq(p.tabs[0], 0.0F)
  // all conductances zero
  assert_eq(p.g_buf[0], 0.0F)
  assert_eq(p.g_buf[7], 0.0F)
  assert_eq(p.h_buf[0], 0.0F)
  assert_eq(p.h_buf[7], 0.0F)
}

///|
test "update_synapses_adex_multi — 2-state ODE decay" {
  let p : AdExMultiTimescale = AdExMultiTimescale::new(n=2)
  // Set g[0,0] = 1, h[0,0] = 0 (no input spike)
  ignore(p.g_buf.set(0, 1.0F))
  update_synapses_adex_multi(p, p.param, 0.1F)
  // g decays by exp(-dt/τd) when h=0; τd=6ms, dt=0.1
  // g_new = exp(-0.1/6) * (1 + 0.1*0) ≈ exp(-0.0167) * 1 ≈ 0.9835
  let g0_after = p.g_buf[0]
  assert_eq(g0_after > 0.98F && g0_after < 0.99F, true)
}

///|
test "update_synapses_adex_multi — independent per-receptor buffers" {
  let p : AdExMultiTimescale = AdExMultiTimescale::new(n=2)
  // Set receptor 0 g[0,0]=1 and receptor 1 g[0,1]=2
  ignore(p.g_buf.set(0, 1.0F))
  ignore(p.g_buf.set(2, 2.0F)) // index 2 = i=0, receptor=1
  update_synapses_adex_multi(p, p.param, 0.1F)
  // Both decay independently with their respective τd
  let g0_r0 = p.g_buf[0] // receptor 0, τd=6
  let g0_r1 = p.g_buf[2] // receptor 1, τd=2
  // g0_r0 ≈ 0.9835, g0_r1 ≈ exp(-0.05) * 2 ≈ 0.9512 * 2 ≈ 1.9025
  assert_eq(g0_r0 > 0.98F && g0_r0 < 0.99F, true)
  assert_eq(g0_r1 > 1.8F && g0_r1 < 2.0F, true)
}

///|
test "synaptic_current_adex_multi — glu receptor uses E_e" {
  let p : AdExMultiTimescale = AdExMultiTimescale::new(n=2)
  // Set g[0, 0] = 5 (exc receptor), v[0] = -70
  ignore(p.g_buf.set(0, 5.0F))
  ignore(p.v.set(0, -70.0F))
  synaptic_current_adex_multi(p, p.param)
  // syn_curr[0] = gsyn_e * g * (v - e_e) = 1.0 * 5 * (-70 - 0) = -350
  assert_eq(p.syn_curr[0], -350.0F)
}

///|
test "synaptic_current_adex_multi — gaba receptor uses E_i" {
  let p : AdExMultiTimescale = AdExMultiTimescale::new(n=2)
  // Set g[0, 1] = 4 (inh receptor, index 2)
  ignore(p.g_buf.set(2, 4.0F))
  ignore(p.v.set(0, -70.0F))
  synaptic_current_adex_multi(p, p.param)
  // syn_curr[0] = gsyn_i * g * (v - e_i) = 1.0 * 4 * (-70 - -75) = 20
  assert_eq(p.syn_curr[0], 20.0F)
}

///|
test "synaptic_current_adex_multi — zero g_buf stays zero" {
  let p : AdExMultiTimescale = AdExMultiTimescale::new(n=2)
  synaptic_current_adex_multi(p, p.param)
  assert_eq(p.syn_curr[0], 0.0F)
  assert_eq(p.syn_curr[1], 0.0F)
}

///|
test "update_soma_adex_multi — fire when v > theta" {
  let p : AdExMultiTimescale = AdExMultiTimescale::new(n=1)
  ignore(p.v.set(0, -45.0F)) // above theta=-50
  update_soma_adex_multi(p, p.param, 0.125F)
  assert_eq(p.fire[0], true)
  assert_eq(p.v[0], -70.6F)
  // tabs = round(1/0.125) = 8
  assert_eq(p.tabs[0], 8.0F)
  // theta incremented by At=10 then decays: theta_final = -40 + 0.125*(Vt-(-40))/30
  //    = -40 + 0.125*(-50 - -40)/30 = -40 + 0.125*-10/30 = -40 - 0.0417 = -40.04
  let theta_after = p.theta[0]
  assert_eq(theta_after > -40.1F && theta_after < -40.0F, true)
  // w bumped by b=80.5 then decayed
  //    = 80.5 + 0.125*(4*(-70.6 - -70.6) - 80.5)/144 = 80.5 - 0.0699 = 80.43
  assert_eq(p.w[0] > 80.0F && p.w[0] < 81.0F, true)
}

///|
test "update_soma_adex_multi — no fire when v <= theta" {
  let p : AdExMultiTimescale = AdExMultiTimescale::new(n=1)
  ignore(p.v.set(0, -55.0F)) // below theta=-50
  update_soma_adex_multi(p, p.param, 0.125F)
  assert_eq(p.fire[0], false)
  // v updates: dv = dt/tm * (-(v-el) + R*(-w + I) - R*syn_curr)
  //        = 0.125/7.025 * (-(-55 - -70.6)) = 0.01779 * (-15.6) = -0.2775
  // v_new = -55 - 0.2775 = -55.28 (leak pulls v toward El=-70.6)
  assert_eq(p.v[0] < -55.0F && p.v[0] > -55.5F, true)
  // theta decays toward Vt=-50 from -50 (no change since equals vt)
  // theta_new = -50 + 0.125*(-50 - -50)/30 = -50
  assert_eq(p.theta[0], -50.0F)
  // w decays toward 0 from a*(v-el) drive: w_new = 0 + dt*(a*(v-el) - 0)/τw
  //            = 0.125 * 4 * (-55 - -70.6) / 144 = 0.125 * 62.4 / 144 ≈ 0.0542
  assert_eq(p.w[0] > 0.0F && p.w[0] < 0.1F, true)
}

///|
test "integrate_adex_multi — full step is finite" {
  let p : AdExMultiTimescale = AdExMultiTimescale::new(n=4)
  ignore(p.i.set(0, 1000.0F))
  let mut step : Int = 0
  while step < 100 {
    integrate_adex_multi(p, p.param, 0.125F)
    step = step + 1
  }
  // All values finite
  let mut k : Int = 0
  while k < 4 {
    let finite = p.v[k] == p.v[k]
    assert_eq(finite, true)
    k = k + 1
  }
}