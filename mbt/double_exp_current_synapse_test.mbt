// Tests for DoubleExpCurrentSynapse.

///|
test "DoubleExpCurrentParameter::new defaults match Julia" {
  let p = DoubleExpCurrentParameter::new()
  assert_eq(p.tau_re, 1.0F)
  assert_eq(p.tau_de, 6.0F)
  assert_eq(p.tau_ri, 0.5F)
  assert_eq(p.tau_di, 2.0F)
}

///|
test "DoubleExpCurrentSynapseVars::new zero-init" {
  let v = DoubleExpCurrentSynapseVars::new(4)
  assert_eq(v.n, 4)
  assert_eq(v.ge.length(), 4)
  assert_eq(v.gi.length(), 4)
  assert_eq(v.he.length(), 4)
  assert_eq(v.hi.length(), 4)
  assert_eq(v.ge[0], 0.0F)
  assert_eq(v.gi[0], 0.0F)
  assert_eq(v.he[0], 0.0F)
  assert_eq(v.hi[0], 0.0F)
}

///|
test "update_synapses_double_exp_current — Euler 2-state ODE from spike input" {
  let vars = DoubleExpCurrentSynapseVars::new(2)
  let glu : Array[Float] = Array::make(2, 0.0F)
  let gaba : Array[Float] = Array::make(2, 0.0F)
  // Single excitatory spike on neuron 0: glu[0] = 1.0
  ignore(glu.set(0, 1.0F))
  update_synapses_double_exp_current(
    vars, DoubleExpCurrentParameter::new(), glu, gaba, 0.1F,
  )
  // After update order:
  //   he[0] += glu[0] = 0 + 1 = 1
  //   ge[0] += dt * (-0 + he[0]) = 0.1 * 1 = 0.1
  //   he[0] += dt * (-he[0]/τre) = 1 - 0.1 = 0.9 (Float32 → 0.8999...)
  assert_eq(vars.he[0] > 0.89F && vars.he[0] < 0.91F, true)
  assert_eq(vars.gi[0], 0.0F)
  // ge[0] = 0.1 (Float32 exact)
  assert_eq(vars.ge[0] > 0.09F && vars.ge[0] < 0.11F, true)
  // glu consumed (reset to 0)
  assert_eq(glu[0], 0.0F)
}

///|
test "update_synapses_double_exp_current — excitatory vs inhibitory routing" {
  let vars = DoubleExpCurrentSynapseVars::new(2)
  let glu : Array[Float] = Array::make(2, 0.0F)
  let gaba : Array[Float] = Array::make(2, 0.0F)
  // glu[0] = 2, gaba[1] = 3
  ignore(glu.set(0, 2.0F))
  ignore(gaba.set(1, 3.0F))
  update_synapses_double_exp_current(
    vars, DoubleExpCurrentParameter::new(), glu, gaba, 0.1F,
  )
  // he[0] = 2 - 0.1*0.1 = 1.8 (after spike bump then decay)
  assert_eq(vars.he[0] > 1.79F && vars.he[0] < 1.81F, true)
  // hi[1] = 3 + 0.1 * (-3 / 0.5) = 3 - 0.6 = 2.4 (τri=0.5ms; larger decay)
  assert_eq(vars.hi[1] > 2.39F && vars.hi[1] < 2.41F, true)
  // he[1] = 0 (no glu[1] input)
  assert_eq(vars.he[1], 0.0F)
  // hi[0] = 0 (no gaba[0] input)
  assert_eq(vars.hi[0], 0.0F)
}

///|
test "synaptic_current_double_exp — current = -(ge - gi)" {
  let vars = DoubleExpCurrentSynapseVars::new(1)
  ignore(vars.ge.set(0, 5.0F))
  ignore(vars.gi.set(0, 2.0F))
  let syn_curr : Array[Float] = Array::make(1, 0.0F)
  synaptic_current_double_exp(vars, syn_curr)
  // syn_curr[0] = -(5 - 2) = -3
  assert_eq(syn_curr[0], -3.0F)
}

///|
test "synaptic_current_double_exp — pure excitatory drive (gi=0)" {
  let vars = DoubleExpCurrentSynapseVars::new(1)
  ignore(vars.ge.set(0, 3.0F))
  let syn_curr : Array[Float] = Array::make(1, 0.0F)
  synaptic_current_double_exp(vars, syn_curr)
  // syn_curr[0] = -(3 - 0) = -3 (positive drive)
  assert_eq(syn_curr[0], -3.0F)
}

///|
test "synaptic_current_double_exp — pure inhibitory drive (ge=0)" {
  let vars = DoubleExpCurrentSynapseVars::new(1)
  ignore(vars.gi.set(0, 4.0F))
  let syn_curr : Array[Float] = Array::make(1, 0.0F)
  synaptic_current_double_exp(vars, syn_curr)
  // syn_curr[0] = -(0 - 4) = 4 (negative drive)
  assert_eq(syn_curr[0], 4.0F)
}

///|
test "update_synapses — 100 steps stays finite" {
  let vars = DoubleExpCurrentSynapseVars::new(4)
  let glu : Array[Float] = Array::make(4, 0.0F)
  let gaba : Array[Float] = Array::make(4, 0.0F)
  let param = DoubleExpCurrentParameter::new()
  let mut step : Int = 0
  while step < 100 {
    // inject spike on each step
    ignore(glu.set(step % 4, 1.0F))
    update_synapses_double_exp_current(vars, param, glu, gaba, 0.125F)
    step = step + 1
  }
  // All values must be finite
  let mut k : Int = 0
  while k < 4 {
    let finite = vars.ge[k] == vars.ge[k]
    assert_eq(finite, true)
    let finite2 = vars.he[k] == vars.he[k]
    assert_eq(finite2, true)
    k = k + 1
  }
}