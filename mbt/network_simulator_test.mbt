// network_simulator_test.mbt — Tests for the unified Network Simulator helpers.

///|
test "network_summary_empty_model" {
  let model = compose([], [])
  let s = network_summary(model)
  assert_eq(s.n_populations, 0)
  assert_eq(s.n_connections, 0)
  assert_eq(s.n_stimuli, 0)
  assert_eq(s.n_monitors, 0)
  assert_eq(s.n_stdp_entries, 0)
  assert_eq(s.n_stp_entries, 0)
  assert_eq(s.total_neurons, 0)
  // Time starts at 0.
  assert_eq(s.current_time, 0.0F)
}

///|
test "network_summary_with_2_if_pops" {
  let rng1 = Xoshiro::new(1UL)
  let rng2 = Xoshiro::new(2UL)
  let p1 = IF::new(5, IFParameter::new(), rng1)
  let p2 = IF::new(7, IFParameter::new(), rng2)
  let pops : Array[AnyPop] = [IF_(p1), IF_(p2)]
  let model = compose(pops, [])
  let s = network_summary(model)
  assert_eq(s.n_populations, 2)
  assert_eq(s.total_neurons, 12)
}

///|
test "merge_heterogeneous_concatenates_topology" {
  let rng1 = Xoshiro::new(1UL)
  let rng2 = Xoshiro::new(2UL)
  let rng3 = Xoshiro::new(3UL)
  let rng4 = Xoshiro::new(4UL)
  let p1 = IF::new(3, IFParameter::new(), rng1)
  let p2 = IF::new(4, IFParameter::new(), rng2)
  let p3 = IF::new(5, IFParameter::new(), rng3)
  let p4 = IF::new(6, IFParameter::new(), rng4)
  let pops_a : Array[AnyPop] = [IF_(p1), IF_(p2)]
  let pops_b : Array[AnyPop] = [IF_(p3), IF_(p4)]
  let model_a = compose(pops_a, [])
  let model_b = compose(pops_b, [])
  let merged = merge_heterogeneous(model_a, model_b)
  let s = network_summary(merged)
  assert_eq(s.n_populations, 4)
  assert_eq(s.total_neurons, 18)
}

///|
test "merge_heterogeneous_concatenates_monitors" {
  let rng = Xoshiro::new(1UL)
  let p = IF::new(3, IFParameter::new(), rng)
  let mon = Monitor::new_fire(p, 0)
  let model_a = compose([IF_(p)], [], monitors=[mon])
  let p2 = IF::new(4, IFParameter::new(), rng)
  let model_b = compose([IF_(p2)], [])
  let merged = merge_heterogeneous(model_a, model_b)
  let s = network_summary(merged)
  assert_eq(s.n_monitors, 1)
  assert_eq(s.n_populations, 2)
}

///|
test "reset_heterogeneous_full_clears_time" {
  let model = compose([], [])
  // Advance the time.
  heterogeneous_sim_for(model, 50.0F)
  let s_before = network_summary(model)
  assert_true(s_before.current_time > 0.0F)
  reset_heterogeneous_full(model)
  let s_after = network_summary(model)
  assert_eq(s_after.current_time, 0.0F)
}

///|
test "reset_heterogeneous_full_clears_monitors" {
  let rng = Xoshiro::new(1UL)
  let p = IF::new(3, IFParameter::new(), rng)
  // Attach a Poisson stimulus that injects spikes.
  let stim = PoissonStimulusIF::new(p, "ge", 20.0F, rng)
  let mon = Monitor::new_fire(p, 0)
  let model = compose([IF_(p)], [], stims=[PoissonIF_(stim)], monitors=[mon])
  heterogeneous_sim_for(model, 100.0F)
  let mon_spikes_before = mon.count_spikes()
  reset_heterogeneous_full(model)
  let mon_spikes_after = mon.count_spikes()
  assert_true(mon_spikes_before > 0)
  assert_eq(mon_spikes_after, 0)
}

///|
test "heterogeneous_sim_for_with_log_advances_time" {
  let model = compose([], [])
  let logs = heterogeneous_sim_for_with_log(model, 50.0F, 10.0F)
  // Time after should be 50.0.
  let s = network_summary(model)
  assert_true(s.current_time > 49.0F)
  // Logs may be empty if the model has no monitors; that's OK.
  let _ = logs
}

///|
test "heterogeneous_sim_for_with_log_logs_when_monitors_active" {
  let rng = Xoshiro::new(7UL)
  let p = IF::new(2, IFParameter::new(), rng)
  let stim = PoissonStimulusIF::new(p, "ge", 20.0F, rng)
  let mon = Monitor::new_fire(p, 0)
  let model = compose([IF_(p)], [], stims=[PoissonIF_(stim)], monitors=[mon])
  let logs = heterogeneous_sim_for_with_log(model, 30.0F, 5.0F)
  // Should have at least 1 log entry (final time >= 5.0).
  assert_true(logs.length() >= 1)
  // First log entry's t should be >= 5.0.
  let first_t = logs[0].t
  assert_true(first_t >= 4.9F)
}

///|
test "step_heterogeneous_with_record_invokes_callback" {
  let model = compose([], [])
  // Step 5 times with a closure that captures the time.
  let captured : Array[Float] = []
  for _k in 0..<5 {
    step_heterogeneous_with_record(model, 0.125F, fn(t : Float) { captured.push(t) })
  }
  assert_eq(captured.length(), 5)
  // Each time should be 0.125 apart (in dt units).
  let dt = 0.125F
  for k in 1..<captured.length() {
    let diff = captured[k] - captured[k - 1]
    let diff_diff = if diff > dt { diff - dt } else { dt - diff }
    assert_true(diff_diff < 1.0e-3F)
  }
}