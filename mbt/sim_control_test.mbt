// Tests for simulation control functions: get_time / reset_time /
// heterogeneous_sim_for / monitor wiring. Mirrors
// SpikingNeuralNetworks.jl/test/sim/sim_control.jl.

///|
test "get_time: zero before sim" {
  let model : @snn_mbt.HeterogeneousModel = @snn_mbt.compose(
    [],
    [],
  )
  let t = @snn_mbt.get_time_heterogeneous(model)
  if t != 0.0F {
    fail("initial time = \{t}, expected 0.0")
  }
}

///|
test "get_time: advances after sim_for" {
  let rng = @snn_mbt.Xoshiro::default()
  let if_param = @snn_mbt.IFParameter::with_el(-49.0F)
  let pop = @snn_mbt.IF::new(10, if_param, rng)
  let any_pop : @snn_mbt.AnyPop = @snn_mbt.IF_(pop)
  let model : @snn_mbt.HeterogeneousModel = @snn_mbt.compose(
    [any_pop],
    [],
  )
  let duration_ms = 100.0F
  @snn_mbt.heterogeneous_sim_for(model, duration_ms)
  let t = @snn_mbt.get_time_heterogeneous(model)
  if (t - duration_ms).abs() > 1.0F {
    fail("time after sim_for(100) = \{t}, expected ~100.0")
  }
}

///|
test "reset_time_heterogeneous: resets to zero" {
  let rng = @snn_mbt.Xoshiro::default()
  let pop = @snn_mbt.IF::new(5, @snn_mbt.IFParameter::new(), rng)
  let any_pop : @snn_mbt.AnyPop = @snn_mbt.IF_(pop)
  let model : @snn_mbt.HeterogeneousModel = @snn_mbt.compose(
    [any_pop],
    [],
  )
  @snn_mbt.heterogeneous_sim_for(model, 200.0F)
  let t_before = @snn_mbt.get_time_heterogeneous(model)
  if (t_before - 200.0F).abs() > 1.0F {
    fail("before reset: time = \{t_before}, expected ~200")
  }
  @snn_mbt.reset_time_heterogeneous(model)
  let t_after = @snn_mbt.get_time_heterogeneous(model)
  if t_after != 0.0F {
    fail("after reset: time = \{t_after}, expected 0.0")
  }
}

///|
test "Monitor: spike count accumulates over sim" {
  // Mirrors Julia's "clear_records! — empties fire record" test:
  // after sim!(model, 300ms), sum(length, spiketimes(E)) > 0.
  let rng = @snn_mbt.Xoshiro::default()
  let if_param = @snn_mbt.IFParameter::with_el(-49.0F)
  let pop = @snn_mbt.IF::new(10, if_param, rng)
  let m = @snn_mbt.Monitor::new_fire(pop, 0)
  let any_pop : @snn_mbt.AnyPop = @snn_mbt.IF_(pop)
  let model : @snn_mbt.HeterogeneousModel = @snn_mbt.compose(
    [any_pop],
    [],
    monitors=[m],
  )
  @snn_mbt.heterogeneous_sim_for(model, 50.0F)
  // Monitor stores spike events as Float 1.0 entries in the data
  // array. Verify at least one spike was recorded.
  if m.data.length() == 0 {
    fail("monitor should record spikes within 50ms of active population")
  }
  let mut spike_count = 0
  for d in m.data {
    if d > 0.0F {
      spike_count = spike_count + 1
    }
  }
  if spike_count == 0 {
    fail("monitor recorded data but no spikes found (data.length() = \{m.data.length()})")
  }
}

///|
test "Monitor::new_v with sampling rate stores sr" {
  // Mirrors Julia's "monitor! — sr keyword stored" test.
  let rng = @snn_mbt.Xoshiro::default()
  let pop = @snn_mbt.IF::new(5, @snn_mbt.IFParameter::new(), rng)
  let m = @snn_mbt.Monitor::new_v_sr(pop, 0, 500.0F)
  if m.rec_step < 1 {
    fail("rec_step = \{m.rec_step}, expected >= 1")
  }
  // The exact rec_step depends on dt / sr_hz; just verify it's sane.
}

///|
test "HeterogeneousModel: compose with multiple pops + conns" {
  let rng = @snn_mbt.Xoshiro::default()
  let pre = @snn_mbt.IF::new(5, @snn_mbt.IFParameter::new(), rng)
  let post = @snn_mbt.IF::new(5, @snn_mbt.IFParameter::new(), rng)
  let syn = @snn_mbt.SpikingSynapse::random(
    pre, post, "ge", 1.0F, 0.0F, 0.5F, rng,
  )
  let any_pre : @snn_mbt.AnyPop = @snn_mbt.IF_(pre)
  let any_post : @snn_mbt.AnyPop = @snn_mbt.IF_(post)
  let model : @snn_mbt.HeterogeneousModel = @snn_mbt.compose(
    [any_pre, any_post],
    [syn],
  )
  if model.conns.length() != 1 {
    fail("model should have 1 conn, has \{model.conns.length()}")
  }
  if model.pops.length() != 2 {
    fail("model should have 2 pops, has \{model.pops.length()}")
  }
}