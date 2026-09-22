// Tests for SingleExpSynapse.

///|
test "SingleExpParameter::new defaults match Julia" {
  let p = SingleExpParameter::new()
  assert_eq(p.tau_e, 6.0F)
  assert_eq(p.tau_i, 0.5F)
  assert_eq(p.e_i, -75.0F)
  assert_eq(p.e_e, 0.0F)
  assert_eq(p.gsyn_e, 1.0F)
  assert_eq(p.gsyn_i, 1.0F)
}

///|
test "SingleExpSynapseVars::new zero-init" {
  let v = SingleExpSynapseVars::new(4)
  assert_eq(v.n, 4)
  assert_eq(v.ge.length(), 4)
  assert_eq(v.gi.length(), 4)
  assert_eq(v.ge[0], 0.0F)
  assert_eq(v.gi[0], 0.0F)
}

///|
test "update_synapses_single_exp — spike input + decay" {
  let vars = SingleExpSynapseVars::new(2)
  let glu : Array[Float] = Array::make(2, 0.0F)
  let gaba : Array[Float] = Array::make(2, 0.0F)
  // Single exc spike on neuron 0: glu[0] = 2
  ignore(glu.set(0, 2.0F))
  update_synapses_single_exp(
    vars, SingleExpParameter::new(), glu, gaba, 0.1F,
  )
  // ge[0] = 0 + 2 + 0.1 * (0 - 0)/6 = 2
  assert_eq(vars.ge[0], 2.0F)
  // gi[0] = 0 + 0 + 0.1 * (0)/0.5 = 0
  assert_eq(vars.gi[0], 0.0F)
  // neuron 1 untouched
  assert_eq(vars.ge[1], 0.0F)
  // spike input consumed
  assert_eq(glu[0], 0.0F)
}

///|
test "update_synapses_single_exp — no spike input decays to zero" {
  let vars = SingleExpSynapseVars::new(1)
  ignore(vars.ge.set(0, 5.0F))
  let glu : Array[Float] = Array::make(1, 0.0F)
  let gaba : Array[Float] = Array::make(1, 0.0F)
  update_synapses_single_exp(
    vars, SingleExpParameter::new(), glu, gaba, 0.1F,
  )
  // ge[0] = 5 + 0 + 0.1 * (-5/6) = 5 - 0.0833 ≈ 4.917
  assert_eq(vars.ge[0] > 4.91F && vars.ge[0] < 4.92F, true)
}

///|
test "synaptic_current_single_exp — conductance current" {
  let vars = SingleExpSynapseVars::new(1)
  ignore(vars.ge.set(0, 3.0F))
  ignore(vars.gi.set(0, 1.0F))
  let v : Array[Float] = Array::make(1, 0.0F)
  ignore(v.set(0, -70.0F))
  let syn_curr : Array[Float] = Array::make(1, 0.0F)
  synaptic_current_single_exp(
    vars, SingleExpParameter::new(), v, syn_curr,
  )
  // syn_curr[0] = 1*3*(-70 - 0) + 1*1*(-70 - -75) = -210 + 5 = -205
  assert_eq(syn_curr[0], -205.0F)
}

///|
test "synaptic_current_single_exp — pure inhibitory" {
  let vars = SingleExpSynapseVars::new(1)
  ignore(vars.gi.set(0, 4.0F))
  let v : Array[Float] = Array::make(1, 0.0F)
  ignore(v.set(0, -70.0F))
  let syn_curr : Array[Float] = Array::make(1, 0.0F)
  synaptic_current_single_exp(
    vars, SingleExpParameter::new(), v, syn_curr,
  )
  // syn_curr[0] = 0 + 1*4*(-70 - -75) = 20
  assert_eq(syn_curr[0], 20.0F)
}

///|
test "synaptic_current_single_exp — pure excitatory" {
  let vars = SingleExpSynapseVars::new(1)
  ignore(vars.ge.set(0, 2.0F))
  let v : Array[Float] = Array::make(1, 0.0F)
  ignore(v.set(0, -50.0F))
  let syn_curr : Array[Float] = Array::make(1, 0.0F)
  synaptic_current_single_exp(
    vars, SingleExpParameter::new(), v, syn_curr,
  )
  // syn_curr[0] = 1*2*(-50 - 0) + 0 = -100
  assert_eq(syn_curr[0], -100.0F)
}

///|
test "update_synapses_single_exp — 1000 steps stays finite" {
  let vars = SingleExpSynapseVars::new(4)
  let glu : Array[Float] = Array::make(4, 0.0F)
  let gaba : Array[Float] = Array::make(4, 0.0F)
  let p = SingleExpParameter::new()
  let mut step : Int = 0
  while step < 1000 {
    ignore(glu.set(step % 4, 1.0F))
    update_synapses_single_exp(vars, p, glu, gaba, 0.125F)
    step = step + 1
  }
  let mut k : Int = 0
  while k < 4 {
    let finite = vars.ge[k] == vars.ge[k]
    assert_eq(finite, true)
    k = k + 1
  }
}