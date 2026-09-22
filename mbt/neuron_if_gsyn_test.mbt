// neuron_if_gsyn_test.mbt — tests for the IFParameterGsyn variant
// (Duarte2019 / LKD2014-style per-population synaptic conductance
// scaling).

///|
test "IFParameterGsyn::new — fields are set" {
  let base = IFParameter::new()
  let gs = IFParameterGsyn::new(base, 1.04F, 0.84F)
  // Underlying IFParameter unchanged.
  assert_eq(gs.base.vr, base.vr)
  assert_eq(gs.base.el, base.el)
  // Gsyn scales set.
  assert_eq(gs.gsyn_e, 1.04F)
  assert_eq(gs.gsyn_i, 0.84F)
}

///|
test "IFParameterGsyn::from_base — uses supplied base" {
  let base : IFParameter = IFParameter::with_el(-64.33F)
  let gs = IFParameterGsyn::from_base(base, 1.04F, 0.84F)
  assert_eq(gs.base.el, -64.33F)
  assert_eq(gs.gsyn_e, 1.04F)
}

///|
test "IFParameterGsyn::apply — sets every neuron's gsyn arrays" {
  // Build a 5-neuron IF and apply a uniform gsyn scale.
  let base = IFParameter::new()
  let rng = Xoshiro::new(42UL)
  let p = IF::new(5, base, rng)
  // Default: gsyn_e[i] = gsyn_i[i] = 1.0.
  assert_eq(p.gsyn_e[0], 1.0F)
  assert_eq(p.gsyn_i[4], 1.0F)
  let gs = IFParameterGsyn::from_base(base, 1.04F, 0.84F)
  gs.apply(p)
  // Now all 5 neurons have the population-wide scale.
  let mut i = 0
  while i < 5 {
    assert_eq(p.gsyn_e[i], 1.04F)
    assert_eq(p.gsyn_i[i], 0.84F)
    i = i + 1
  }
}

///|
test "IF::with_gsyn — convenience constructor applies scales" {
  let base = IFParameter::new()
  let rng = Xoshiro::new(42UL)
  let p = IF::with_gsyn(8, base, 1.5F, 2.0F, rng)
  assert_eq(p.n, 8)
  assert_eq(p.gsyn_e[0], 1.5F)
  assert_eq(p.gsyn_e[7], 1.5F)
  assert_eq(p.gsyn_i[3], 2.0F)
}

///|
test "synaptic_current — gsyn_e scales the excitatory drive" {
  // With gsyn_e = 2.0, the excitatory synaptic current is doubled
  // compared to the default gsyn_e = 1.0.
  let base = IFParameter::new()
  let rng = Xoshiro::new(42UL)
  let dt = 0.125F
  let p_baseline = IF::with_gsyn(1, base, 1.0F, 1.0F, rng)
  let p_scaled = IF::with_gsyn(1, base, 2.0F, 1.0F, rng)
  // Drive a single spike in.
  p_baseline.glu[0] = 1.0F
  p_scaled.glu[0] = 1.0F
  // Step synapses.
  step_synapses(p_baseline, dt)
  step_synapses(p_scaled, dt)
  // Step voltage / integrate (need to check what helpers exist).
  // For this test, just call synaptic_current to compute the drive.
  p_baseline.syn_curr[0] = 0.0F
  p_scaled.syn_curr[0] = 0.0F
  synaptic_current(p_baseline)
  synaptic_current(p_scaled)
  // Scaled should be 2x baseline (same ge, but gsyn_e = 2x).
  // After step_synapses, ge[0] is the same in both cases (dt = 0.125 ms,
  // initial he[0] = glu[0] = 1.0; ge accumulates from he).
  // The drive scales linearly with gsyn_e.
  let ratio = p_scaled.syn_curr[0] / p_baseline.syn_curr[0]
  let diff = (ratio - 2.0F).abs()
  if diff > 0.1F {
    fail("gsyn_e=2 should double the current: ratio = \{ratio}")
  }
}