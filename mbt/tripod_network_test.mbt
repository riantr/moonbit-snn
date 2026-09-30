// tripod_network_test.mbt — integration tests for the TripodNetwork example.
//
// Mirrors the scenario in examples/tripod_network/main.mbt: a small
// E-I Tripod network with I1→E(:soma, iSTDPRate) + I2→E(:d1, iSTDPPotential)
// compartment synapses.

///|
fn build_tripod_network_test() -> (Tripod, IF, IF, CompartmentSynapseTripod, CompartmentSynapseTripod, IstdpRateEntry, IstdpPotentialEntry) {
  let rng = Xoshiro::new(123UL)
  let ne = 20
  let ni1 = 5
  let ni2 = 5
  // E = Tripod.
  let e_param = AdExParameter::new()
  let e_pop = Tripod::new(ne, e_param, rng)
  // I1, I2 = IF.
  let i1_param = IFParameter::custom(7.0F, -50.0F, -60.0F, -55.0F, 1.0F)
  let i1_pop = IF::new(ni1, i1_param, rng)
  let i2_param = IFParameter::custom(20.0F, -50.0F, -60.0F, -55.0F, 1.0F)
  let i2_pop = IF::new(ni2, i2_param, rng)
  // Tonic drive to keep I populations active.
  for k in 0..<ni1 { i1_pop.i[k] = 250.0F }
  for k in 0..<ni2 { i2_pop.i[k] = 250.0F }
  // Compartment synapses.
  let i1_to_e = CompartmentSynapseTripod::random(
    i1_pop, e_pop, "ge", "soma", 5.0F, 0.0F, 0.2F, rng,
  )
  let i2_to_e = CompartmentSynapseTripod::random(
    i2_pop, e_pop, "ge", "d1", 5.0F, 0.0F, 0.2F, rng,
  )
  let i1_entry = IstdpRateEntry::new(0, ni1, ne)
  let i2_entry = IstdpPotentialEntry::new(0, ni2, ne)
  (e_pop, i1_pop, i2_pop, i1_to_e, i2_to_e, i1_entry, i2_entry)
}

///|
test "tripod_network_smoke_runs_without_crash" {
  let dt = 0.125F
  let (e_pop, i1_pop, i2_pop, i1_to_e, i2_to_e, i1_entry, i2_entry) = build_tripod_network_test()
  let ne = e_pop.n
  let ni1 = i1_pop.n
  let ni2 = i2_pop.n
  let total_steps = 400 // 50 ms
  for step in 0..<total_steps {
    let _ = step
    forward_compartment_tripod(i1_to_e, Float::from_int(step) * dt)
    forward_compartment_tripod(i2_to_e, Float::from_int(step) * dt)
    step_synapses(i1_pop, dt)
    synaptic_current(i1_pop)
    step_neuron(i1_pop, dt)
    step_synapses(i2_pop, dt)
    synaptic_current(i2_pop)
    step_neuron(i2_pop, dt)
    step_tripod(e_pop, dt)
    istdp_rate_step(
      i1_to_e.matrix.vals, i1_pop.fire, e_pop.fire,
      i1_to_e.matrix.colptr, i1_to_e.matrix.rowptr,
      i1_entry.vars, i1_entry.param, Float::from_int(step) * dt, dt,
    )
    istdp_potential_step(
      i2_to_e.matrix.vals, i2_pop.fire, e_pop.fire,
      i2_to_e.matrix.colptr, i2_to_e.matrix.rowptr,
      e_pop.v_d1,
      i2_entry.vars, i2_entry.param, Float::from_int(step) * dt, dt,
    )
    let _ = ne
    let _ = ni1
    let _ = ni2
  }
  // All populations must have finite voltages after the run.
  let v_e_0 = e_pop.v_s[0]
  let v_i1_0 = i1_pop.v[0]
  let v_i2_0 = i2_pop.v[0]
  let _ = v_e_0
  let _ = v_i1_0
  let _ = v_i2_0
  if v_e_0 != v_e_0 { fail("E v_s[0] not finite") }
  if v_i1_0 != v_i1_0 { fail("I1 v[0] not finite") }
  if v_i2_0 != v_i2_0 { fail("I2 v[0] not finite") }
}

///|
test "tripod_network_i_populations_active_under_tonic_drive" {
  let dt = 0.125F
  let (_e_pop, i1_pop, i2_pop, _i1_to_e, _i2_to_e, _i1_entry, _i2_entry) = build_tripod_network_test()
  let total_steps = 1000 // 125 ms
  let mut i1_spikes = 0
  let mut i2_spikes = 0
  for _ in 0..<total_steps {
    step_synapses(i1_pop, dt)
    synaptic_current(i1_pop)
    step_neuron(i1_pop, dt)
    step_synapses(i2_pop, dt)
    synaptic_current(i2_pop)
    step_neuron(i2_pop, dt)
    for k in 0..<i1_pop.n { if i1_pop.fire[k] { i1_spikes = i1_spikes + 1 } }
    for k in 0..<i2_pop.n { if i2_pop.fire[k] { i2_spikes = i2_spikes + 1 } }
  }
  // With tonic 250 pA drive, both I populations should produce some spikes.
  if i1_spikes == 0 { fail("I1 never fired under tonic drive") }
  if i2_spikes == 0 { fail("I2 never fired under tonic drive") }
}

///|
test "tripod_network_istdp_rate_step_modifies_weights" {
  // Verify that the iSTDP-rate step actually modifies the synapse
  // weights over a few hundred steps with active pre/post.
  let dt = 0.125F
  let (e_pop, i1_pop, _i2_pop, i1_to_e, _i2_to_e, i1_entry, _i2_entry) = build_tripod_network_test()
  let weights_before = i1_to_e.matrix.vals.copy()
  let n_weights_before = weights_before.length()
  let mut total_abs_delta = 0.0F
  for step in 0..<400 {
    forward_compartment_tripod(i1_to_e, Float::from_int(step) * dt)
    step_synapses(i1_pop, dt)
    synaptic_current(i1_pop)
    step_neuron(i1_pop, dt)
    step_tripod(e_pop, dt)
    istdp_rate_step(
      i1_to_e.matrix.vals, i1_pop.fire, e_pop.fire,
      i1_to_e.matrix.colptr, i1_to_e.matrix.rowptr,
      i1_entry.vars, i1_entry.param, Float::from_int(step) * dt, dt,
    )
  }
  for k in 0..<n_weights_before {
    let diff = i1_to_e.matrix.vals[k] - weights_before[k]
    let abs_diff = if diff > 0.0F { diff } else { -diff }
    total_abs_delta = total_abs_delta + abs_diff
  }
  let _ = total_abs_delta
  // Verify at least one weight changed.
  let mut changed = 0
  for k in 0..<n_weights_before {
    if i1_to_e.matrix.vals[k] != weights_before[k] {
      changed = changed + 1
    }
  }
  if changed == 0 {
    fail("iSTDP rate step did not modify any weights")
  }
}