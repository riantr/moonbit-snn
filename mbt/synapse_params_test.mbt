// Tests for the four synapse parameter types (DeltaSynapse /
// SingleExpSynapse / DoubleExpSynapse / CurrentSynapse). Mirrors
// SpikingNeuralNetworks.jl/test/pop/parameters.jl "Synaptic parameter
// types" testset.

///|
test "DeltaSynapse::new: empty struct (no parameters)" {
  let s = DeltaSynapse::new()
  // DeltaSynapse has no fields; just verify it constructs.
  let _ = s
  // isa check via pattern match is implicit; if construction works,
  // the type matches Julia's `DeltaSynapse <: AbstractDeltaParameter`.
}

///|
test "DeltaSynapseVars::new: zero-initialised" {
  let v = DeltaSynapseVars::new(5)
  if v.n != 5 {
    fail("n = \{v.n}, expected 5")
  }
  for k in 0..<5 {
    if v.ge[k] != 0.0F {
      fail("ge[\{k}] = \{v.ge[k]}, expected 0.0")
    }
    if v.gi[k] != 0.0F {
      fail("gi[\{k}] = \{v.gi[k]}, expected 0.0")
    }
  }
}

///|
test "delta_synapse_step: ge/gi accumulate from glu/gaba, then reset" {
  // DeltaSynapse is instantaneous — step writes glu → ge, gaba → gi.
  let vars = DeltaSynapseVars::new(2)
  let glu : Array[Float] = [1.0F, 2.0F]
  let gaba : Array[Float] = [0.5F, 1.5F]
  delta_synapse_step(vars, glu, gaba)
  if (vars.ge[0] - 1.0F).abs() > 1.0e-6F {
    fail("ge[0] = \{vars.ge[0]}, expected 1.0")
  }
  if (vars.ge[1] - 2.0F).abs() > 1.0e-6F {
    fail("ge[1] = \{vars.ge[1]}, expected 2.0")
  }
  if (vars.gi[0] - 0.5F).abs() > 1.0e-6F {
    fail("gi[0] = \{vars.gi[0]}, expected 0.5")
  }
  // synaptic_current: syn_curr = -(ge - gi), then reset.
  let syn_curr : Array[Float] = Array::make(2, 0.0F)
  delta_synapse_current(vars, syn_curr)
  if (syn_curr[0] - -(1.0F - 0.5F)).abs() > 1.0e-6F {
    fail("syn_curr[0] = \{syn_curr[0]}, expected -0.5")
  }
  if vars.ge[0] != 0.0F {
    fail("after current, ge[0] = \{vars.ge[0]}, expected 0 (reset)")
  }
}

///|
test "SingleExpSynapse::new: defaults match Julia" {
  let p = SingleExpSynapse::new()
  if p.tau_e != 6.0F {
    fail("tau_e = \{p.tau_e}, expected 6.0")
  }
  if p.tau_i != 0.5F {
    fail("tau_i = \{p.tau_i}, expected 0.5")
  }
  if p.e_i != -75.0F {
    fail("e_i = \{p.e_i}, expected -75.0")
  }
  if p.e_e != 0.0F {
    fail("e_e = \{p.e_e}, expected 0.0")
  }
  if p.gsyn_e != 1.0F {
    fail("gsyn_e = \{p.gsyn_e}, expected 1.0")
  }
  if p.gsyn_i != 1.0F {
    fail("gsyn_i = \{p.gsyn_i}, expected 1.0")
  }
}

///|
test "SingleExpSynapseVars::new: zero-init + step decay" {
  let vars = SingleExpSynapseVars::new(1)
  let p = SingleExpSynapse::new()
  // Force ge[0]=1, gi[0]=1, then call step with no spike input.
  vars.ge[0] = 1.0F
  vars.gi[0] = 1.0F
  let glu : Array[Float] = [0.0F]
  let gaba : Array[Float] = [0.0F]
  let dt = 0.125F
  single_exp_synapse_step(vars, p, glu, gaba, dt)
  // After step with no input: ge *= (1 - dt/tau_e) = 1 * (1 - 0.125/6)
  // = 1 - 0.0208 ≈ 0.9792.
  let decay_e : Float = 1.0F - dt / p.tau_e
  if (vars.ge[0] - decay_e).abs() > 1.0e-5F {
    fail("ge[0] = \{vars.ge[0]}, expected ~\{decay_e}")
  }
  let decay_i : Float = 1.0F - dt / p.tau_i
  if (vars.gi[0] - decay_i).abs() > 1.0e-5F {
    fail("gi[0] = \{vars.gi[0]}, expected ~\{decay_i}")
  }
}

///|
test "conductance_synapse_current: syn_curr = ge*(v-E_e)*gsyn_e + gi*(v-E_i)*gsyn_i" {
  let vars = SingleExpSynapseVars::new(2)
  let p = SingleExpSynapse::new()
  vars.ge[0] = 1.0F
  vars.gi[0] = 0.5F
  vars.ge[1] = 2.0F
  vars.gi[1] = 1.0F
  let v : Array[Float] = [-50.0F, 10.0F]
  let syn_curr : Array[Float] = Array::make(2, 0.0F)
  conductance_synapse_current(vars, p, v, syn_curr)
  // syn_curr[0] = 1 * (-50 - 0) * 1 + 0.5 * (-50 - (-75)) * 1
  //             = -50 + 12.5 = -37.5
  if (syn_curr[0] - -37.5F).abs() > 1.0e-4F {
    fail("syn_curr[0] = \{syn_curr[0]}, expected ~-37.5")
  }
  // syn_curr[1] = 2 * (10 - 0) * 1 + 1 * (10 - (-75)) * 1
  //             = 20 + 85 = 105
  if (syn_curr[1] - 105.0F).abs() > 1.0e-4F {
    fail("syn_curr[1] = \{syn_curr[1]}, expected ~105")
  }
}

///|
test "DoubleExpSynapse::new: defaults match Julia" {
  let p = DoubleExpSynapse::new()
  if p.tau_re != 1.0F {
    fail("tau_re = \{p.tau_re}, expected 1.0")
  }
  if p.tau_de != 6.0F {
    fail("tau_de = \{p.tau_de}, expected 6.0")
  }
  if p.tau_ri != 0.5F {
    fail("tau_ri = \{p.tau_ri}, expected 0.5")
  }
  if p.tau_di != 2.0F {
    fail("tau_di = \{p.tau_di}, expected 2.0")
  }
  if p.e_i != -75.0F {
    fail("e_i = \{p.e_i}, expected -75.0")
  }
  if p.e_e != 0.0F {
    fail("e_e = \{p.e_e}, expected 0.0")
  }
  if p.gsyn_e != 1.0F {
    fail("gsyn_e = \{p.gsyn_e}, expected 1.0")
  }
  if p.gsyn_i != 1.0F {
    fail("gsyn_i = \{p.gsyn_i}, expected 1.0")
  }
}

///|
test "DoubleExpSynapseVars::new: zero-init, all four arrays" {
  let v = DoubleExpSynapseVars::new(3)
  if v.n != 3 {
    fail("n = \{v.n}, expected 3")
  }
  for k in 0..<3 {
    if v.ge[k] != 0.0F || v.gi[k] != 0.0F {
      fail("ge/gi[\{k}] should be 0")
    }
    if v.he[k] != 0.0F || v.hi[k] != 0.0F {
      fail("he/hi[\{k}] should be 0")
    }
  }
}

///|
test "double_exp_synapse_step: 2-state ODE integrates spike input" {
  // Spike input glu[0]=1.0F. After step:
  //   he[0] += dt * (glu[0] - he[0]) / tau_re
  //         = 0 + 0.125 * (1.0 - 0) / 1.0 = 0.125
  //   ge[0] += dt * (he[0] - ge[0]) / tau_de
  //         = 0 + 0.125 * (0.125 - 0) / 6.0 = 0.002604
  let vars = DoubleExpSynapseVars::new(1)
  let p = DoubleExpSynapse::new()
  let glu : Array[Float] = [1.0F]
  let gaba : Array[Float] = [0.0F]
  let dt = 0.125F
  double_exp_synapse_step(vars, p, glu, gaba, dt)
  let he_expected = dt * (1.0F - 0.0F) / p.tau_re
  let ge_expected = dt * (he_expected - 0.0F) / p.tau_de
  if (vars.he[0] - he_expected).abs() > 1.0e-5F {
    fail("he[0] = \{vars.he[0]}, expected ~\{he_expected}")
  }
  if (vars.ge[0] - ge_expected).abs() > 1.0e-5F {
    fail("ge[0] = \{vars.ge[0]}, expected ~\{ge_expected}")
  }
}

///|
test "CurrentSynapse::new: defaults match Julia" {
  let p = CurrentSynapse::new()
  if p.tau_e != 6.0F {
    fail("tau_e = \{p.tau_e}, expected 6.0")
  }
  if p.tau_i != 2.0F {
    fail("tau_i = \{p.tau_i}, expected 2.0")
  }
}

///|
test "CurrentSynapseVars::new: zero-init + step decay" {
  let vars = CurrentSynapseVars::new(1)
  let p = CurrentSynapse::new()
  vars.ge[0] = 1.0F
  vars.gi[0] = 1.0F
  let glu : Array[Float] = [0.0F]
  let gaba : Array[Float] = [0.0F]
  let dt = 0.125F
  current_synapse_step(vars, p, glu, gaba, dt)
  let decay_e : Float = 1.0F - dt / p.tau_e
  let decay_i : Float = 1.0F - dt / p.tau_i
  if (vars.ge[0] - decay_e).abs() > 1.0e-5F {
    fail("ge[0] = \{vars.ge[0]}, expected ~\{decay_e}")
  }
  if (vars.gi[0] - decay_i).abs() > 1.0e-5F {
    fail("gi[0] = \{vars.gi[0]}, expected ~\{decay_i}")
  }
}

///|
test "current_synapse_current: syn_curr = -(ge - gi)" {
  let vars = CurrentSynapseVars::new(2)
  vars.ge[0] = 1.0F
  vars.gi[0] = 0.5F
  vars.ge[1] = 2.0F
  vars.gi[1] = 1.0F
  let syn_curr : Array[Float] = Array::make(2, 0.0F)
  current_synapse_current(vars, syn_curr)
  // syn_curr[0] = -(1.0 - 0.5) = -0.5
  // syn_curr[1] = -(2.0 - 1.0) = -1.0
  if (syn_curr[0] - -0.5F).abs() > 1.0e-6F {
    fail("syn_curr[0] = \{syn_curr[0]}, expected -0.5")
  }
  if (syn_curr[1] - -1.0F).abs() > 1.0e-6F {
    fail("syn_curr[1] = \{syn_curr[1]}, expected -1.0")
  }
}