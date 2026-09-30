// delta_synapse_test.mbt — tests for v0.40.6 DeltaSynapse (instantaneous).
//
// DeltaSynapse is the simplest synaptic model: no time constants.
// On each step:
//   ge[i] += glu[i]
//   gi[i] += gaba[i]
// Then the synaptic current read zeroes ge/gi (one-shot consume).
//
// These tests cover the edge cases that the existing
// `synapse_params_test.mbt` doesn't hit:
//   - accumulation over multiple steps
//   - sign / magnitude of synaptic current for inh-only / exc-only
//   - caller-owns-buffer discipline (step does NOT clear glu/gaba)
//   - n-dim correctness

///|
test "delta_synapse_step_accumulates_over_multiple_calls" {
  let vars = DeltaSynapseVars::new(1)
  // Spike 1: glu=1.0
  let glu : Array[Float] = [1.0F]
  let gaba : Array[Float] = [0.0F]
  delta_synapse_step(vars, glu, gaba)
  // Caller clears glu before next call.
  glu[0] = 0.0F
  // Spike 2: glu=2.0
  glu[0] = 2.0F
  delta_synapse_step(vars, glu, gaba)
  // ge = 1.0 + 2.0 = 3.0 (cumulative — no decay term).
  if (vars.ge[0] - 3.0F).abs() > 1.0e-6F {
    fail("ge[0] = \{vars.ge[0]}, expected 3.0 (cumulative)")
  }
}

///|
test "delta_synapse_step_does_not_clear_glu_gaba" {
  // The step must NOT consume the input buffers — caller is
  // responsible for clearing them. (Otherwise it would silently
  // double-count on the next step.)
  let vars = DeltaSynapseVars::new(1)
  let glu : Array[Float] = [1.5F]
  let gaba : Array[Float] = [0.7F]
  delta_synapse_step(vars, glu, gaba)
  if (glu[0] - 1.5F).abs() > 1.0e-6F {
    fail("glu[0] was clobbered by step: got \{glu[0]}, expected 1.5")
  }
  if (gaba[0] - 0.7F).abs() > 1.0e-6F {
    fail("gaba[0] was clobbered by step: got \{gaba[0]}, expected 0.7")
  }
}

///|
test "delta_synapse_current_zero_with_no_input" {
  let vars = DeltaSynapseVars::new(3)
  let syn_curr : Array[Float] = Array::make(3, 99.0F)
  delta_synapse_current(vars, syn_curr)
  for i in 0..<3 {
    if syn_curr[i] != 0.0F {
      fail("syn_curr[\{i}] = \{syn_curr[i]}, expected 0")
    }
  }
}

///|
test "delta_synapse_current_positive_for_inh_only" {
  // ge=0, gi=1 → syn_curr = -(0 - 1) = +1 (Cl⁻ current flows inward).
  let vars = DeltaSynapseVars::new(1)
  vars.gi[0] = 1.0F
  let syn_curr : Array[Float] = Array::make(1, 0.0F)
  delta_synapse_current(vars, syn_curr)
  if (syn_curr[0] - 1.0F).abs() > 1.0e-6F {
    fail("syn_curr[0] = \{syn_curr[0]}, expected 1.0")
  }
}

///|
test "delta_synapse_current_negative_for_exc_only" {
  // ge=1, gi=0 → syn_curr = -(1 - 0) = -1 (Na⁺/K⁺ excitatory current).
  let vars = DeltaSynapseVars::new(1)
  vars.ge[0] = 1.0F
  let syn_curr : Array[Float] = Array::make(1, 0.0F)
  delta_synapse_current(vars, syn_curr)
  if (syn_curr[0] - -1.0F).abs() > 1.0e-6F {
    fail("syn_curr[0] = \{syn_curr[0]}, expected -1.0")
  }
}

///|
test "delta_synapse_current_zeros_ge_gi_after_read" {
  let vars = DeltaSynapseVars::new(2)
  vars.ge[0] = 0.7F
  vars.ge[1] = 0.3F
  vars.gi[0] = 0.2F
  vars.gi[1] = 0.4F
  let syn_curr : Array[Float] = Array::make(2, 0.0F)
  delta_synapse_current(vars, syn_curr)
  for i in 0..<2 {
    if vars.ge[i] != 0.0F {
      fail("after current, ge[\{i}] = \{vars.ge[i]}, expected 0")
    }
    if vars.gi[i] != 0.0F {
      fail("after current, gi[\{i}] = \{vars.gi[i]}, expected 0")
    }
  }
}

///|
test "delta_synapse_step_then_current_is_zero" {
  // Without an intervening caller-clear, a 2nd step would double-count.
  // After step → current → reset, ge/gi must be zero (single-shot).
  let vars = DeltaSynapseVars::new(2)
  let glu : Array[Float] = [0.5F, 1.0F]
  let gaba : Array[Float] = [0.2F, 0.4F]
  delta_synapse_step(vars, glu, gaba)
  let syn_curr : Array[Float] = Array::make(2, 0.0F)
  delta_synapse_current(vars, syn_curr)
  // syn_curr[0] = -(0.5 - 0.2) = -0.3
  if (syn_curr[0] - -0.3F).abs() > 1.0e-6F {
    fail("syn_curr[0] = \{syn_curr[0]}, expected -0.3")
  }
  // syn_curr[1] = -(1.0 - 0.4) = -0.6
  if (syn_curr[1] - -0.6F).abs() > 1.0e-6F {
    fail("syn_curr[1] = \{syn_curr[1]}, expected -0.6")
  }
  // After current, ge/gi are zeroed.
  for i in 0..<2 {
    if vars.ge[i] != 0.0F {
      fail("ge[\{i}] = \{vars.ge[i]}, expected 0 after current")
    }
    if vars.gi[i] != 0.0F {
      fail("gi[\{i}] = \{vars.gi[i]}, expected 0 after current")
    }
  }
}

///|
test "delta_synapse_n_dim_correctness" {
  let n = 10
  let vars = DeltaSynapseVars::new(n)
  if vars.n != n {
    fail("n = \{vars.n}, expected \{n}")
  }
  // All buffers must be exactly length n.
  if vars.ge.length() != n {
    fail("ge.length() = \{vars.ge.length()}, expected \{n}")
  }
  if vars.gi.length() != n {
    fail("gi.length() = \{vars.gi.length()}, expected \{n}")
  }
  // Set distinct values per index, verify they don't bleed.
  let glu : Array[Float] = Array::make(n, 0.0F)
  let gaba : Array[Float] = Array::make(n, 0.0F)
  for i in 0..<n {
    glu[i] = Float::from_int(i) * 0.1F
    gaba[i] = Float::from_int(i) * -0.05F
  }
  delta_synapse_step(vars, glu, gaba)
  for i in 0..<n {
    let expected_ge = Float::from_int(i) * 0.1F
    let expected_gi = Float::from_int(i) * -0.05F
    if (vars.ge[i] - expected_ge).abs() > 1.0e-5F {
      fail("ge[\{i}] = \{vars.ge[i]}, expected \{expected_ge}")
    }
    if (vars.gi[i] - expected_gi).abs() > 1.0e-5F {
      fail("gi[\{i}] = \{vars.gi[i]}, expected \{expected_gi}")
    }
  }
}

///|
test "delta_synapse_no_op_with_zero_inputs" {
  // All zeros in → all zeros out.
  let vars = DeltaSynapseVars::new(3)
  let glu : Array[Float] = Array::make(3, 0.0F)
  let gaba : Array[Float] = Array::make(3, 0.0F)
  delta_synapse_step(vars, glu, gaba)
  let syn_curr : Array[Float] = Array::make(3, 99.0F)
  delta_synapse_current(vars, syn_curr)
  for i in 0..<3 {
    if syn_curr[i] != 0.0F {
      fail("syn_curr[\{i}] = \{syn_curr[i]}, expected 0")
    }
  }
}