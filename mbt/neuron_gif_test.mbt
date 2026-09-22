// Tests for GIF (Generalized Integrate-and-Fire).

///|
test "GIFParameter::new defaults match pure LIF" {
  let p = GIFParameter::new()
  assert_eq(p.vt, -50.0F)
  assert_eq(p.vr, -60.0F)
  assert_eq(p.el, -70.0F)
  assert_eq(p.tm, 15.0F)
  assert_eq(p.r, 0.06F)
  assert_eq(p.dt_slope, 2.0F)
  assert_eq(p.a, 0.0F)
  assert_eq(p.b, 0.0F)
  assert_eq(p.tw, 0.0F)
}

///|
test "GIFParameter::adex — Brette-Gerstner 2005 defaults" {
  let p = GIFParameter::adex()
  assert_eq(p.c, 281.0F)
  assert_eq(p.gl, 40.0F)
  // tm = c/gl = 281/40 ≈ 7.025 (Float32 → 7.024999618530273)
  assert_eq(p.tm > 7.0F && p.tm < 7.05F, true)
  assert_eq(p.r > 0.024F && p.r < 0.0251F, true)
  assert_eq(p.vt, -50.0F)
  assert_eq(p.vr, -70.6F)
  assert_eq(p.el, -70.6F)
  assert_eq(p.dt_slope, 2.0F)
  assert_eq(p.a, 4.0F)
  assert_eq(p.b, 80.5F)
  assert_eq(p.tw, 144.0F)
}

///|
test "GIF::new — shapes and zero-initialised state" {
  let p : GIF = GIF::new(n=10)
  assert_eq(p.n, 10)
  assert_eq(p.v.length(), 10)
  assert_eq(p.w.length(), 10)
  assert_eq(p.fire.length(), 10)
  assert_eq(p.tabs.length(), 10)
  assert_eq(p.i.length(), 10)
  assert_eq(p.syn_curr.length(), 10)
  assert_eq(p.glu.length(), 10)
  assert_eq(p.gaba.length(), 10)
  // initial state
  assert_eq(p.v[0], -60.0F)
  assert_eq(p.w[0], 0.0F)
  assert_eq(p.fire[0], false)
  assert_eq(p.tabs[0], 0.0F)
}

///|
test "update_neuron_gif — pure LIF (τw=0): no adaptation" {
  let p : GIF = GIF::new(n=1, param=GIFParameter::new())
  ignore(p.i.set(0, 1000.0F))
  update_neuron_gif(p, p.param, 0.125F)
  // v rose from -60 toward Vt=-50 (positive drive, default LIF eq)
  assert_eq(p.v[0] > -60.0F, true)
  // w stays zero (τw=0 branch skipped)
  assert_eq(p.w[0], 0.0F)
}

///|
test "update_neuron_gif — AdEx mode: adaptation w increments on fire" {
  let p : GIF = GIF::new(n=1, param=GIFParameter::adex())
  // Force a fire: set v above Vt=-50
  ignore(p.v.set(0, -45.0F))
  update_neuron_gif(p, p.param, 0.125F)
  // fire=true, v reset to -70.6
  assert_eq(p.fire[0], true)
  assert_eq(p.v[0], -70.6F)
  // w += b on fire (=80.5), then w += dt*(a*(v-el) - w)/τw
  // = 80.5 + 0.125 * (4*(-70.6 - -70.6) - 80.5) / 144
  // = 80.5 + 0.125 * (0 - 80.5) / 144
  // = 80.5 - 0.0699 = 80.43
  let w_expected = 80.5F + 0.125F * (4.0F * (-70.6F - -70.6F) - 80.5F) / 144.0F
  let _ = w_expected
  assert_eq(p.w[0] > 80.0F && p.w[0] < 81.0F, true)
}

///|
test "update_neuron_gif — refractory countdown" {
  let p : GIF = GIF::new(n=1, param=GIFParameter::new())
  // Fire once
  ignore(p.v.set(0, -45.0F))
  update_neuron_gif(p, p.param, 0.125F)
  assert_eq(p.fire[0], true)
  // tabs = round(5/0.125) = 40
  assert_eq(p.tabs[0], 40.0F)
  // Next step: tabs > 0, fire=false, v stays at Vr
  update_neuron_gif(p, p.param, 0.125F)
  assert_eq(p.fire[0], false)
  assert_eq(p.v[0], -60.0F)
  assert_eq(p.tabs[0], 39.0F)
}

///|
test "update_neuron_gif — w sub-threshold decay (AdEx mode)" {
  let p : GIF = GIF::new(n=1, param=GIFParameter::adex())
  // w starts at 10.0 (artificial initial condition)
  ignore(p.w.set(0, 10.0F))
  // v below Vt, no fire
  ignore(p.v.set(0, -65.0F))
  update_neuron_gif(p, p.param, 0.125F)
  // w += dt*(a*(v-el) - w)/τw = 0.125*(4*(-65 - -70.6) - 10)/144
  //    = 0.125*(22.4 - 10)/144 = 0.125*12.4/144 = 0.01076
  // w = 10 + 0.01076 = 10.01076
  assert_eq(p.w[0] > 10.0F && p.w[0] < 10.02F, true)
  // fire=false
  assert_eq(p.fire[0], false)
}

///|
test "integrate_gif — full step end-to-end" {
  let p : GIF = GIF::new(n=4, param=GIFParameter::new())
  ignore(p.i.set(0, 800.0F))
  ignore(p.i.set(1, 400.0F))
  // 100 steps at dt=0.125ms = 12.5ms sim
  let mut step : Int = 0
  while step < 100 {
    integrate_gif(p, p.param, 0.125F)
    step = step + 1
  }
  // All values must be finite
  let mut k : Int = 0
  while k < 4 {
    let finite = p.v[k] == p.v[k]
    assert_eq(finite, true)
    k = k + 1
  }
}