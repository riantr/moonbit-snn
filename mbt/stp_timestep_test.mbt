// Tests for MarkramSTPParameterTimestep (continuous-time Euler Markram
// STP variant) 鈥?mirrors SpikingNeuralNetworks.jl/test/syn/
// with_plasticity.jl "MarkramSTPParameterTimestep" testset.

///|
test "MarkramSTPParameterTimestep::new: defaults match Julia" {
  let p = MarkramSTPParameterTimestep::new()
  if p.u != 0.5F {
    fail("u = \{p.u}, expected 0.5")
  }
  if p.tau_f != 750.0F {
    fail("tau_f = \{p.tau_f}, expected 750.0")
  }
  if p.tau_d != 250.0F {
    fail("tau_d = \{p.tau_d}, expected 250.0")
  }
}

///|
test "MarkramSTPEntryTimestep::new: fields populated, vars initialised" {
  let e = MarkramSTPEntryTimestep::new(0, 4, 3)
  if e.conn_index != 0 {
    fail("conn_index = \{e.conn_index}, expected 0")
  }
  if e.vars.n_pre != 4 || e.vars.n_post != 3 {
    fail("n_pre/n_post wrong")
  }
  if e.vars.u.length() != 4 {
    fail("u length = \{e.vars.u.length()}, expected 4")
  }
  for k in 0..<4 {
    if e.vars.u[k] != 0.5F {
      fail("u[\{k}] = \{e.vars.u[k]} should be 0.5 initially")
    }
    if e.vars.x[k] != 1.0F {
      fail("x[\{k}] = \{e.vars.x[k]} should be 1.0 initially")
    }
    if e.vars.rho_pre[k] != 0.5F {
      fail("rho_pre[\{k}] = \{e.vars.rho_pre[k]} should be 0.5 initially")
    }
  }
}

///|
test "markram_stp_step_timestep: no-spike continuous decay toward baseline" {
  // With no firing, u should drift toward U (0.5) and x toward 1.
  let rng = Xoshiro::default()
  let if_param = IFParameter::new()
  let pre = IF::new(2, if_param, rng)
  let post = IF::new(2, if_param, rng)
  let syn = SpikingSynapse::new(pre, post, "ge")
  spiking_connect(syn, 1, 1, 1.0F)
  spiking_connect(syn, 1, 2, 1.0F)
  SpikingSynapse::init_rho(syn)
  pre.fire[0] = false
  pre.fire[1] = false
  let entry = MarkramSTPEntryTimestep::new(0, 2, 2)
  entry.vars.u[0] = 0.0F
  entry.vars.u[1] = 1.0F
  entry.vars.x[0] = 0.0F
  entry.vars.x[1] = 0.0F
  let dt = 0.125F
  markram_stp_step_timestep(syn, entry.vars, entry.param, 0.0F, dt)
  let u0_expected = 0.0F + dt * (0.5F - 0.0F) / entry.param.tau_f
  if (entry.vars.u[0] - u0_expected).abs() > 1.0e-5F {
    fail("u[0] = \{entry.vars.u[0]}, expected ~\{u0_expected}")
  }
  let u1_expected = 1.0F + dt * (0.5F - 1.0F) / entry.param.tau_f
  if (entry.vars.u[1] - u1_expected).abs() > 1.0e-5F {
    fail("u[1] = \{entry.vars.u[1]}, expected ~\{u1_expected}")
  }
  let x0_expected = 0.0F + dt * (1.0F - 0.0F) / entry.param.tau_d
  if (entry.vars.x[0] - x0_expected).abs() > 1.0e-5F {
    fail("x[0] = \{entry.vars.x[0]}, expected ~\{x0_expected}")
  }
}

///|
test "markram_stp_step_timestep: spike bump before continuous update" {
  // Pre-spike: u[j] += U*(1 - u[j]); x[j] += -u[j]*x[j] FIRST.
  // Then continuous Euler.
  let rng = Xoshiro::default()
  let if_param = IFParameter::new()
  let pre = IF::new(1, if_param, rng)
  let post = IF::new(1, if_param, rng)
  let syn = SpikingSynapse::new(pre, post, "ge")
  spiking_connect(syn, 1, 1, 1.0F)
  SpikingSynapse::init_rho(syn)
  pre.fire[0] = true
  let entry = MarkramSTPEntryTimestep::new(0, 1, 1)
  entry.vars.u[0] = 0.5F
  entry.vars.x[0] = 1.0F
  let dt = 0.125F
  markram_stp_step_timestep(syn, entry.vars, entry.param, 0.0F, dt)
  let u_after_spike = 0.5F + 0.5F * (1.0F - 0.5F)
  let x_after_spike = 1.0F + (-u_after_spike * 1.0F)
  let u_final = u_after_spike + dt * (0.5F - u_after_spike) / 750.0F
  let x_final = x_after_spike + dt * (1.0F - x_after_spike) / 250.0F
  if (entry.vars.u[0] - u_final).abs() > 1.0e-5F {
    fail("u[0] = \{entry.vars.u[0]}, expected ~\{u_final}")
  }
  if (entry.vars.x[0] - x_final).abs() > 1.0e-5F {
    fail("x[0] = \{entry.vars.x[0]}, expected ~\{x_final}")
  }
  let rho_expected = u_final * x_final
  if (entry.vars.rho_pre[0] - rho_expected).abs() > 1.0e-6F {
    fail("rho_pre[0] = \{entry.vars.rho_pre[0]}, expected ~\{rho_expected}")
  }
  if (syn.rho[0] - rho_expected).abs() > 1.0e-6F {
    fail("syn.rho[0] = \{syn.rho[0]}, expected ~\{rho_expected}")
  }
}

///|
test "markram_stp_step_timestep: rho broadcast to all outgoing connections" {
  let rng = Xoshiro::default()
  let if_param = IFParameter::new()
  let pre = IF::new(1, if_param, rng)
  let post = IF::new(3, if_param, rng)
  let syn = SpikingSynapse::new(pre, post, "ge")
  spiking_connect(syn, 1, 1, 1.0F)
  spiking_connect(syn, 1, 2, 1.0F)
  spiking_connect(syn, 1, 3, 1.0F)
  SpikingSynapse::init_rho(syn)
  pre.fire[0] = false
  let entry = MarkramSTPEntryTimestep::new(0, 1, 3)
  entry.vars.u[0] = 0.3F
  entry.vars.x[0] = 0.7F
  markram_stp_step_timestep(syn, entry.vars, entry.param, 0.0F, 0.125F)
  let rho = entry.vars.rho_pre[0]
  if (syn.rho[0] - rho).abs() > 1.0e-6F {
    fail("syn.rho[0] = \{syn.rho[0]}, expected \{rho}")
  }
  if (syn.rho[1] - rho).abs() > 1.0e-6F {
    fail("syn.rho[1] = \{syn.rho[1]}, expected \{rho}")
  }
  if (syn.rho[2] - rho).abs() > 1.0e-6F {
    fail("syn.rho[2] = \{syn.rho[2]}, expected \{rho}")
  }
}

///|
test "markram_stp_step_timestep: rho stays in (0, 1) bounds over time" {
  let rng = Xoshiro::default()
  let if_param = IFParameter::new()
  let pre = IF::new(4, if_param, rng)
  let post = IF::new(4, if_param, rng)
  let syn = SpikingSynapse::new(pre, post, "ge")
  for j in 1..<=4 {
    for i in 1..<=4 {
      spiking_connect(syn, j, i, 1.0F)
    }
  }
  SpikingSynapse::init_rho(syn)
  let entry = MarkramSTPEntryTimestep::new(0, 4, 4)
  let rng2 = Xoshiro::from_state(
    0xCAFEBABEUL, 0xDEADBEEFUL, 0xFEEDFACEUL, 0x0123456789ABCDEFUL,
  )
  for step in 0..<500 {
    let _ = step
    for k in 0..<4 {
      pre.fire[k] = next_f32(rng2) < 0.3F
    }
    markram_stp_step_timestep(syn, entry.vars, entry.param, Float::from_int(step) * 0.125F, 0.125F)
  }
  for s in 0..<syn.matrix.vals.length() {
    if syn.rho[s] < 0.0F {
      fail("rho[\{s}] = \{syn.rho[s]} < 0")
    }
    if syn.rho[s] > 1.0F + 1.0e-5F {
      fail("rho[\{s}] = \{syn.rho[s]} > 1")
    }
    if (syn.rho[s]).is_nan() || (syn.rho[s]).is_inf() {
      fail("rho[\{s}] = \{syn.rho[s]} not finite")
    }
  }
}

///|
test "markram_stp_step_timestep: weights stay finite over many steps" {
  // Mirrors Julia's `with_plasticity.jl` MarkramSTPParameterTimestep
  // testset: after many random steps, all vals stay finite.
  let rng = Xoshiro::default()
  let if_param = IFParameter::new()
  let pre = IF::new(4, if_param, rng)
  let post = IF::new(4, if_param, rng)
  let syn = SpikingSynapse::new(pre, post, "ge")
  for j in 1..<=4 {
    for i in 1..<=4 {
      spiking_connect(syn, j, i, 1.0F)
    }
  }
  SpikingSynapse::init_rho(syn)
  let entry = MarkramSTPEntryTimestep::new(0, 4, 4)
  let rng2 = Xoshiro::from_state(
    0xDEADBEEFUL, 0xCAFEBABEUL, 0xFEEDFACEUL, 0x0123456789ABCDEFUL,
  )
  for step in 0..<200 {
    let _ = step
    for k in 0..<4 {
      pre.fire[k] = next_f32(rng2) < 0.3F
    }
    markram_stp_step_timestep(syn, entry.vars, entry.param, Float::from_int(step) * 0.125F, 0.125F)
  }
  for s in 0..<syn.matrix.vals.length() {
    if (syn.matrix.vals[s]).is_nan() || (syn.matrix.vals[s]).is_inf() {
      fail("vals[\{s}] not finite")
    }
  }
}

///|
test "markram_stp_step_timestep: inactive flag disables rule" {
  let rng = Xoshiro::default()
  let if_param = IFParameter::new()
  let pre = IF::new(1, if_param, rng)
  let post = IF::new(1, if_param, rng)
  let syn = SpikingSynapse::new(pre, post, "ge")
  spiking_connect(syn, 1, 1, 1.0F)
  SpikingSynapse::init_rho(syn)
  // Initialise rho to a known non-zero value to detect updates.
  syn.rho[0] = 0.42F
  pre.fire[0] = true
  let entry = MarkramSTPEntryTimestep::new(0, 1, 1)
  entry.vars.active[0] = false
  entry.vars.u[0] = 0.0F
  entry.vars.x[0] = 0.0F
  markram_stp_step_timestep(syn, entry.vars, entry.param, 0.0F, 0.125F)
  // u and x should remain at 0.
  if entry.vars.u[0] != 0.0F {
    fail("inactive: u[0] = \{entry.vars.u[0]} should be 0.0")
  }
  if entry.vars.x[0] != 0.0F {
    fail("inactive: x[0] = \{entry.vars.x[0]} should be 0.0")
  }
  // syn.rho[0] should still be 0.42.
  if (syn.rho[0] - 0.42F).abs() > 1.0e-6F {
    fail("inactive: rho[0] = \{syn.rho[0]} should be 0.42")
  }
}
