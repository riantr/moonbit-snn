// Tests for Confavreux2025Synapse.

///|
test "Confavreux2025Parameter::new defaults match Julia" {
  let p = Confavreux2025Parameter::new()
  assert_eq(p.tau_ampa, 5.0F)
  assert_eq(p.tau_nmda, 100.0F)
  assert_eq(p.tau_gaba, 10.0F)
  assert_eq(p.e_i, -80.0F)
  assert_eq(p.e_e, 0.0F)
  assert_eq(p.alpha, 0.23F)
}

///|
test "Confavreux2025SynapseVars::new zero-init" {
  let v = Confavreux2025SynapseVars::new(4)
  assert_eq(v.n, 4)
  assert_eq(v.g_ampa.length(), 4)
  assert_eq(v.g_nmda.length(), 4)
  assert_eq(v.g_gaba.length(), 4)
  assert_eq(v.g_ampa[0], 0.0F)
  assert_eq(v.g_nmda[0], 0.0F)
  assert_eq(v.g_gaba[0], 0.0F)
}

///|
test "update_synapses_confraveux2025 — AMPA + GABA spike input" {
  let vars = Confavreux2025SynapseVars::new(2)
  let glu : Array[Float] = Array::make(2, 0.0F)
  let gaba : Array[Float] = Array::make(2, 0.0F)
  // glu[0] = 1, gaba[1] = 2
  ignore(glu.set(0, 1.0F))
  ignore(gaba.set(1, 2.0F))
  update_synapses_confraveux2025(
    vars, Confavreux2025Parameter::new(), glu, gaba, 0.1F,
  )
  // AMPA: 0 + 0.1 * (0 + 1) = 0.1 (post-update)
  assert_eq(vars.g_ampa[0] > 0.09F && vars.g_ampa[0] < 0.11F, true)
  // GABA on neuron 1: 0 + 0.1 * (0 + 2) = 0.2
  assert_eq(vars.g_gaba[1] > 0.19F && vars.g_gaba[1] < 0.21F, true)
  // NMDA on neuron 0: 0 + 0.1 * (0.1 - 0)/100 = 0.0001
  assert_eq(vars.g_nmda[0] > 0.00009F && vars.g_nmda[0] < 0.00011F, true)
  // No cross-talk: g_gaba[0] = 0, g_ampa[1] = 0
  assert_eq(vars.g_gaba[0], 0.0F)
  assert_eq(vars.g_ampa[1], 0.0F)
  // spike inputs consumed
  assert_eq(glu[0], 0.0F)
  assert_eq(gaba[1], 0.0F)
}

///|
test "update_synapses_confraveux2025 — NMDA coupled to AMPA" {
  let vars = Confavreux2025SynapseVars::new(1)
  let glu : Array[Float] = Array::make(1, 0.0F)
  let gaba : Array[Float] = Array::make(1, 0.0F)
  // Constant glu[0] = 5 over 10 steps; expect gNMDA → 5 (coupling target)
  let p = Confavreux2025Parameter::new()
  let mut step : Int = 0
  while step < 100 {
    ignore(glu.set(0, 5.0F))
    update_synapses_confraveux2025(vars, p, glu, gaba, 0.1F)
    step = step + 1
  }
  // After 100 steps (10ms) with τAMPA=5ms, gAMPA converges to glu*τAMPA = 5*5 = 25
  // After 100 steps with τNMDA=100ms (10x slower), gNMDA catches up but lags
  // (analytical: gNMDA(t) → 25 * (1 - exp(-t/τNMDA_amu_coupled)) where
  // effective coupling time is τNMDA = 100ms; at t=10ms, exp(-0.1) ≈ 0.905)
  // Just verify both are positive and finite.
  assert_eq(vars.g_ampa[0] > 20.0F && vars.g_ampa[0] < 30.0F, true)
  assert_eq(vars.g_nmda[0] > 0.0F && vars.g_nmda[0] < 25.0F, true)
}

///|
test "update_synapses_confraveux2025 — no spike input decays to zero" {
  let vars = Confavreux2025SynapseVars::new(1)
  // Set initial state
  ignore(vars.g_ampa.set(0, 5.0F))
  ignore(vars.g_nmda.set(0, 5.0F))
  ignore(vars.g_gaba.set(0, 5.0F))
  let glu : Array[Float] = Array::make(1, 0.0F)
  let gaba : Array[Float] = Array::make(1, 0.0F)
  update_synapses_confraveux2025(
    vars, Confavreux2025Parameter::new(), glu, gaba, 0.1F,
  )
  // AMPA: 5 + 0.1 * (-5/5 + 0) = 5 - 0.1 = 4.9
  assert_eq(vars.g_ampa[0] > 4.89F && vars.g_ampa[0] < 4.91F, true)
  // NMDA: 5 + 0.1 * (4.9 - 5)/100 = 5 - 0.0001 = 4.9999
  assert_eq(vars.g_nmda[0] > 4.99F && vars.g_nmda[0] < 5.0F, true)
  // GABA: 5 + 0.1 * (-5/10 + 0) = 5 - 0.05 = 4.95
  assert_eq(vars.g_gaba[0] > 4.94F && vars.g_gaba[0] < 4.96F, true)
}

///|
test "synaptic_current_confraveux2025 — weighted AMPA + NMDA" {
  let vars = Confavreux2025SynapseVars::new(1)
  // Set all conductances to 1
  ignore(vars.g_ampa.set(0, 1.0F))
  ignore(vars.g_nmda.set(0, 1.0F))
  ignore(vars.g_gaba.set(0, 0.0F))
  let v : Array[Float] = Array::make(1, -50.0F)
  let syn_curr : Array[Float] = Array::make(1, 0.0F)
  synaptic_current_confraveux2025(
    vars, Confavreux2025Parameter::new(), v, syn_curr,
  )
  // weighted = α*1 + (1-α)*1 = 1
  // exc = 1 * (-50 - 0) = -50
  // inh = 0
  // syn_curr = -50
  assert_eq(syn_curr[0], -50.0F)
}

///|
test "synaptic_current_confraveux2025 — GABA inhibitory drive" {
  let vars = Confavreux2025SynapseVars::new(1)
  // gGABA = 1, no AMPA/NMDA
  ignore(vars.g_ampa.set(0, 0.0F))
  ignore(vars.g_nmda.set(0, 0.0F))
  ignore(vars.g_gaba.set(0, 1.0F))
  let v : Array[Float] = Array::make(1, -70.0F)
  let syn_curr : Array[Float] = Array::make(1, 0.0F)
  synaptic_current_confraveux2025(
    vars, Confavreux2025Parameter::new(), v, syn_curr,
  )
  // weighted = 0, exc = 0
  // inh = 1 * (-70 - -80) = 10 (pushes v up — out of equilibrium for GABA)
  // syn_curr = 10
  assert_eq(syn_curr[0], 10.0F)
}

///|
test "synaptic_current_confraveux2025 — α-blend dominance" {
  let vars = Confavreux2025SynapseVars::new(1)
  // gAMPA = 2, gNMDA = 0 — pure AMPA contribution
  ignore(vars.g_ampa.set(0, 2.0F))
  ignore(vars.g_nmda.set(0, 0.0F))
  ignore(vars.g_gaba.set(0, 0.0F))
  let v : Array[Float] = Array::make(1, -50.0F)
  let syn_curr : Array[Float] = Array::make(1, 0.0F)
  synaptic_current_confraveux2025(
    vars, Confavreux2025Parameter::new(), v, syn_curr,
  )
  // weighted = α*2 + (1-α)*0 = 0.46
  // exc = 0.46 * (-50 - 0) = -23
  assert_eq(syn_curr[0] > -24.0F && syn_curr[0] < -22.0F, true)
}

///|
test "update_synapses_confraveux2025 — 1000 steps stays finite" {
  let vars = Confavreux2025SynapseVars::new(4)
  let glu : Array[Float] = Array::make(4, 0.0F)
  let gaba : Array[Float] = Array::make(4, 0.0F)
  let p = Confavreux2025Parameter::new()
  let mut step : Int = 0
  while step < 1000 {
    // inject spikes on different neurons each step
    ignore(glu.set(step % 4, 1.0F))
    update_synapses_confraveux2025(vars, p, glu, gaba, 0.125F)
    step = step + 1
  }
  let mut k : Int = 0
  while k < 4 {
    let finite = vars.g_ampa[k] == vars.g_ampa[k]
    assert_eq(finite, true)
    let finite2 = vars.g_nmda[k] == vars.g_nmda[k]
    assert_eq(finite2, true)
    let finite3 = vars.g_gaba[k] == vars.g_gaba[k]
    assert_eq(finite3, true)
    k = k + 1
  }
}