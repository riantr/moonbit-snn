// current_synapse_test.mbt — additional edge-case tests for v0.41.0
// CurrentSynapse (current-based, exponential decay — no he/hi
// auxiliary, no conductance gating).
//
// The 3 baseline tests in `synapse_params_test.mbt` cover defaults +
// single-step decay + the current formula. This file covers:
//   - sign of current (purely exc → negative, purely inh → positive,
//     balanced → zero)
//   - current does NOT zero ge/gi (unlike DeltaSynapse)
//   - multi-step decay convergence toward zero
//   - spike input accumulation (caller-owned glu/gaba buffers)
//   - long-run stability
//   - n-dim correctness with mixed routing

///|
test "current_synapse_pure_exc_current_negative" {
  // ge=1, gi=0 → syn_curr = -(1 - 0) = -1 (Na⁺/K⁺ excitatory inward).
  let vars = CurrentSynapseVars::new(1)
  vars.ge[0] = 1.0F
  vars.gi[0] = 0.0F
  let syn_curr : Array[Float] = Array::make(1, 99.0F)
  current_synapse_current(vars, syn_curr)
  if (syn_curr[0] - -1.0F).abs() > 1.0e-6F {
    fail("syn_curr[0] = \{syn_curr[0]}, expected -1.0 (pure exc)")
  }
}

///|
test "current_synapse_pure_inh_current_positive" {
  // ge=0, gi=1 → syn_curr = -(0 - 1) = +1 (Cl⁻ inhibitory inward).
  let vars = CurrentSynapseVars::new(1)
  vars.ge[0] = 0.0F
  vars.gi[0] = 1.0F
  let syn_curr : Array[Float] = Array::make(1, 99.0F)
  current_synapse_current(vars, syn_curr)
  if (syn_curr[0] - 1.0F).abs() > 1.0e-6F {
    fail("syn_curr[0] = \{syn_curr[0]}, expected +1.0 (pure inh)")
  }
}

///|
test "current_synapse_balanced_current_zero" {
  // ge=gi → syn_curr = -(ge - gi) = 0.
  let vars = CurrentSynapseVars::new(1)
  vars.ge[0] = 0.7F
  vars.gi[0] = 0.7F
  let syn_curr : Array[Float] = Array::make(1, 99.0F)
  current_synapse_current(vars, syn_curr)
  if syn_curr[0] != 0.0F {
    fail("syn_curr[0] = \{syn_curr[0]}, expected 0.0 (balanced)")
  }
}

///|
test "current_synapse_current_does_not_zero_ge_gi" {
  // Unlike DeltaSynapse (which zeros after read), CurrentSynapse
  // preserves ge/gi so the next step's decay continues from the
  // current value. This is critical for the integration loop.
  let vars = CurrentSynapseVars::new(1)
  vars.ge[0] = 1.5F
  vars.gi[0] = 0.7F
  let syn_curr : Array[Float] = Array::make(1, 0.0F)
  current_synapse_current(vars, syn_curr)
  if vars.ge[0] != 1.5F {
    fail("after current, ge[0] = \{vars.ge[0]}, expected 1.5 (preserved)")
  }
  if vars.gi[0] != 0.7F {
    fail("after current, gi[0] = \{vars.gi[0]}, expected 0.7 (preserved)")
  }
}

///|
test "current_synapse_step_accumulates_spike_input" {
  // glu=1 spike adds to ge, then decay. With dt=0.1, tau_e=6:
  //   ge_new = (ge_old + 1) * (1 - 0.1/6) = (ge_old + 1) * 0.98333
  let p = CurrentSynapse::new()
  let vars = CurrentSynapseVars::new(1)
  vars.ge[0] = 0.0F
  let glu : Array[Float] = [1.0F]
  let gaba : Array[Float] = [0.0F]
  let dt = 0.1F
  current_synapse_step(vars, p, glu, gaba, dt)
  let expected = (0.0F + 1.0F) * (1.0F - dt / p.tau_e)
  if (vars.ge[0] - expected).abs() > 1.0e-5F {
    fail("ge[0] = \{vars.ge[0]}, expected ~\{expected}")
  }
}

///|
test "current_synapse_step_decay_only_with_no_input" {
  // No spike: pure decay. ge_new = ge_old * (1 - dt/tau_e).
  let p = CurrentSynapse::new()
  let vars = CurrentSynapseVars::new(1)
  vars.ge[0] = 2.0F
  vars.gi[0] = 3.0F
  let glu : Array[Float] = [0.0F]
  let gaba : Array[Float] = [0.0F]
  let dt = 0.1F
  current_synapse_step(vars, p, glu, gaba, dt)
  let expected_ge = 2.0F * (1.0F - dt / p.tau_e)
  let expected_gi = 3.0F * (1.0F - dt / p.tau_i)
  if (vars.ge[0] - expected_ge).abs() > 1.0e-5F {
    fail("ge[0] = \{vars.ge[0]}, expected ~\{expected_ge}")
  }
  if (vars.gi[0] - expected_gi).abs() > 1.0e-5F {
    fail("gi[0] = \{vars.gi[0]}, expected ~\{expected_gi}")
  }
}

///|
test "current_synapse_step_does_not_clear_glu_gaba" {
  // Caller is responsible for clearing buffers between steps —
  // the step does NOT consume glu/gaba (parity with Delta + DoubleExp).
  let p = CurrentSynapse::new()
  let vars = CurrentSynapseVars::new(1)
  let glu : Array[Float] = [1.5F]
  let gaba : Array[Float] = [0.7F]
  current_synapse_step(vars, p, glu, gaba, 0.1F)
  if (glu[0] - 1.5F).abs() > 1.0e-6F {
    fail("glu[0] clobbered: got \{glu[0]}, expected 1.5")
  }
  if (gaba[0] - 0.7F).abs() > 1.0e-6F {
    fail("gaba[0] clobbered: got \{gaba[0]}, expected 0.7")
  }
}

///|
test "current_synapse_long_run_decay_converges_to_zero" {
  // With no input, ge/gi must decay exponentially toward zero.
  // After 1000 steps with dt=0.1, decay factor = (1 - 0.1/6)^1000
  // ≈ 0.98333^1000 ≈ 6.4e-8 — well within Float32 underflow.
  let p = CurrentSynapse::new()
  let vars = CurrentSynapseVars::new(1)
  vars.ge[0] = 1.0F
  vars.gi[0] = 1.0F
  let glu : Array[Float] = [0.0F]
  let gaba : Array[Float] = [0.0F]
  for _ in 0..<1000 {
    current_synapse_step(vars, p, glu, gaba, 0.1F)
  }
  // Both should be effectively zero (≤ 1e-4).
  if vars.ge[0] > 1.0e-4F {
    fail("ge[0] after 1000 steps = \{vars.ge[0]}, expected ~0")
  }
  if vars.gi[0] > 1.0e-4F {
    fail("gi[0] after 1000 steps = \{vars.gi[0]}, expected ~0")
  }
}

///|
test "current_synapse_repeated_input_saturates_to_dt_times_input_over_tau" {
  // With glu=1 every step:
  //   ge_new = (ge_old + 1) * (1 - dt/tau_e)
  // Steady state: ge_ss = ge_ss * (1 - dt/tau_e) + (1 - dt/tau_e)
  //            ⇒ ge_ss * dt/tau_e = (1 - dt/tau_e)
  //            ⇒ ge_ss = (1 - dt/tau_e) / (dt/tau_e) = tau_e/dt - 1
  // For tau_e=6, dt=0.1: ge_ss = 60 - 1 = 59.
  let p = CurrentSynapse::new()
  let vars = CurrentSynapseVars::new(1)
  let glu : Array[Float] = [1.0F]
  let gaba : Array[Float] = [0.0F]
  for _ in 0..<5000 {
    glu[0] = 1.0F
    current_synapse_step(vars, p, glu, gaba, 0.1F)
  }
  let ge_ss = p.tau_e / 0.1F - 1.0F
  // Allow 1% tolerance.
  let diff = if vars.ge[0] > ge_ss { vars.ge[0] - ge_ss } else { ge_ss - vars.ge[0] }
  if diff / ge_ss > 0.01F {
    fail("ge[0] = \{vars.ge[0]}, expected ~\{ge_ss}")
  }
}

///|
test "current_synapse_n_dim_mixed_routing" {
  // 4 neurons with distinct excitatory/inhibitory patterns.
  let n = 4
  let vars = CurrentSynapseVars::new(n)
  if vars.n != n {
    fail("n = \{vars.n}, expected \{n}")
  }
  if vars.ge.length() != n {
    fail("ge.length() = \{vars.ge.length()}, expected \{n}")
  }
  if vars.gi.length() != n {
    fail("gi.length() = \{vars.gi.length()}, expected \{n}")
  }
  // Apply distinct glu/gaba per neuron.
  let p = CurrentSynapse::new()
  let glu : Array[Float] = [0.5F, 0.0F, 1.0F, 0.0F]
  let gaba : Array[Float] = [0.0F, 0.7F, 0.0F, 0.3F]
  current_synapse_step(vars, p, glu, gaba, 0.1F)
  // After one step (starting from zero):
  //   neuron 0: ge=0.5 * (1 - 0.1/6), gi=0
  //   neuron 1: ge=0, gi=0.7 * (1 - 0.1/2)
  //   neuron 2: ge=1.0 * (1 - 0.1/6), gi=0
  //   neuron 3: ge=0, gi=0.3 * (1 - 0.1/2)
  let decay_e = 1.0F - 0.1F / p.tau_e
  let decay_i = 1.0F - 0.1F / p.tau_i
  let exp_ge_0 = 0.5F * decay_e
  let exp_gi_1 = 0.7F * decay_i
  let exp_ge_2 = 1.0F * decay_e
  let exp_gi_3 = 0.3F * decay_i
  if (vars.ge[0] - exp_ge_0).abs() > 1.0e-5F {
    fail("ge[0] = \{vars.ge[0]}, expected \{exp_ge_0}")
  }
  if (vars.gi[1] - exp_gi_1).abs() > 1.0e-5F {
    fail("gi[1] = \{vars.gi[1]}, expected \{exp_gi_1}")
  }
  if (vars.ge[2] - exp_ge_2).abs() > 1.0e-5F {
    fail("ge[2] = \{vars.ge[2]}, expected \{exp_ge_2}")
  }
  if (vars.gi[3] - exp_gi_3).abs() > 1.0e-5F {
    fail("gi[3] = \{vars.gi[3]}, expected \{exp_gi_3}")
  }
  // Verify the other slots are zero.
  if vars.ge[1] != 0.0F {
    fail("ge[1] = \{vars.ge[1]}, expected 0")
  }
  if vars.ge[3] != 0.0F {
    fail("ge[3] = \{vars.ge[3]}, expected 0")
  }
  if vars.gi[0] != 0.0F {
    fail("gi[0] = \{vars.gi[0]}, expected 0")
  }
  if vars.gi[2] != 0.0F {
    fail("gi[2] = \{vars.gi[2]}, expected 0")
  }
}

///|
test "current_synapse_zero_state_produces_zero_current" {
  // All-zero buffers + all-zero spike input → zero current.
  let vars = CurrentSynapseVars::new(3)
  let syn_curr : Array[Float] = Array::make(3, 99.0F)
  current_synapse_current(vars, syn_curr)
  for i in 0..<3 {
    if syn_curr[i] != 0.0F {
      fail("syn_curr[\{i}] = \{syn_curr[i]}, expected 0")
    }
  }
}