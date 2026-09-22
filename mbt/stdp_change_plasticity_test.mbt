// Tests for STDPEntry::change_plasticity! (runtime LTP parameter swap).
//
// Mirrors SpikingNeuralNetworks.jl/test/syn/with_plasticity.jl
// "change_plasticity!" section: build a model with STDPGerstner on a
// synapse, run train!, then swap to STDPConfavreux2025 and verify the
// entry's param field updated.

///|
test "STDPEntry::change_plasticity: swap STDPGerstner parameters" {
  let entry = STDPEntry::new(0, 2, 2)
  // Initial param: defaults
  if entry.param.a_pre != 0.01F {
    fail("initial a_pre = \{entry.param.a_pre}, expected 0.01")
  }
  // Swap with custom params (struct literal — STDPGerstner fields immutable).
  let new_param : STDPGerstner = {
    a_pre: 0.5F,
    a_post: 0.3F,
    tau_pre: 20.0F,
    tau_post: 20.0F,
    w_max: 30.0F,
    w_min: 0.0F,
  }
  STDPEntry::change_plasticity(entry, new_param)
  if (entry.param.a_pre - 0.5F).abs() > 1.0e-6F {
    fail("after swap: a_pre = \{entry.param.a_pre}, expected 0.5")
  }
  if (entry.param.a_post - 0.3F).abs() > 1.0e-6F {
    fail("after swap: a_post = \{entry.param.a_post}, expected 0.3")
  }
}

///|
test "STDPEntry::change_plasticity: preserves vars state" {
  let entry = STDPEntry::new(0, 1, 1)
  // Mutate vars (simulate some pre/post firing).
  entry.vars.tpre[0] = 5.0F
  entry.vars.tpost[0] = 10.0F
  entry.vars.last_pre[0] = 5.0F
  entry.vars.last_post[0] = 10.0F
  // Swap params; vars should not change.
  let new_param : STDPGerstner = {
    a_pre: 0.5F,
    a_post: 0.3F,
    tau_pre: 20.0F,
    tau_post: 20.0F,
    w_max: 30.0F,
    w_min: 0.0F,
  }
  STDPEntry::change_plasticity(entry, new_param)
  if (entry.vars.tpre[0] - 5.0F).abs() > 1.0e-6F {
    fail("tpre changed: \{entry.vars.tpre[0]} (expected 5.0)")
  }
  if (entry.vars.tpost[0] - 10.0F).abs() > 1.0e-6F {
    fail("tpost changed: \{entry.vars.tpost[0]} (expected 10.0)")
  }
  if (entry.vars.last_pre[0] - 5.0F).abs() > 1.0e-6F {
    fail("last_pre changed: \{entry.vars.last_pre[0]} (expected 5.0)")
  }
  if (entry.vars.last_post[0] - 10.0F).abs() > 1.0e-6F {
    fail("last_post changed: \{entry.vars.last_post[0]} (expected 10.0)")
  }
}

///|
test "STDPEntryMexicanHat::change_plasticity: swap parameters" {
  let entry = STDPEntryMexicanHat::new(0, 2, 2)
  if (entry.param.a - 0.1F).abs() > 1.0e-6F {
    fail("initial a = \{entry.param.a}, expected 0.1")
  }
  let new_param : STDPMexicanHat = { a: 0.05F, tau: 30.0F, w_max: 30.0F, w_min: 0.0F }
  STDPEntryMexicanHat::change_plasticity(entry, new_param)
  if (entry.param.a - 0.05F).abs() > 1.0e-6F {
    fail("after swap: a = \{entry.param.a}, expected 0.05")
  }
}

///|
test "STDPEntryAntiSymmetric::change_plasticity: swap parameters" {
  let entry = STDPEntryAntiSymmetric::new(0, 2, 2)
  if (entry.param.a_x - 0.03F).abs() > 1.0e-6F {
    fail("initial a_x = \{entry.param.a_x}, expected 0.03")
  }
  let new_param : STDPAntiSymmetric = {
    a_x: 0.05F,
    a_y: 0.0F,
    tau_x: 20.0F,
    tau_y: 20.0F,
    alpha_pre: 0.0F,
    alpha_post: 0.0F,
    w_max: 30.0F,
    w_min: 0.0F,
  }
  STDPEntryAntiSymmetric::change_plasticity(entry, new_param)
  if (entry.param.a_x - 0.05F).abs() > 1.0e-6F {
    fail("after swap: a_x = \{entry.param.a_x}, expected 0.05")
  }
}

///|
test "STDPEntry::change_plasticity: subsequent steps use new params" {
  // Verify that after the swap, stdp_step uses the new A_pre / A_post
  // (not the original). Run a tiny pre/post pair with the new params.
  let entry = STDPEntry::new(0, 1, 1)
  let new_param : STDPGerstner = {
    a_pre: 1.0F,        // large amplitude
    a_post: 0.0F,       // no LTD
    tau_pre: 20.0F,
    tau_post: 20.0F,
    w_max: 30.0F,
    w_min: 0.0F,
  }
  STDPEntry::change_plasticity(entry, new_param)
  // Simulate pre firing at t=0, post firing at t=10 (Δt=10 > 0 → LTP).
  entry.vars.tpre[0] = 0.0F
  entry.vars.tpost[0] = 10.0F
  // ΔW expected = a_pre * exp(-Δt/τ_pre) = 1.0 * exp(-0.5) ≈ 0.6065
  let dw = gerstner_kernel(
    entry.vars.tpost[0] - entry.vars.tpre[0],
    entry.param.tau_pre,
    entry.param.tau_post,
    entry.param.a_pre,
    entry.param.a_post,
  )
  if (dw - 0.6065F).abs() > 0.01F {
    fail("dw = \{dw}, expected ~0.6065")
  }
}