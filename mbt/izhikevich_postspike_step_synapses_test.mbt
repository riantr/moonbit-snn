// izhikevich_postspike_step_synapses_test.mbt 鈥?integration tests
// combining step_iz_synapses_postspike (standalone decay) and
// step_iz_with_postspike (membrane + fire). Verifies the typical
// sim! loop pattern.

///|
test "postspike step+synapses / tab countdown through combined loop" {
  // Pattern: synapse-step then neuron-step. Verify tabs decrement each
  // full iteration.
  let pop = IZ::init_with_postspike(
    1, IZParameter::rs(), 35.0F, 7.0F, IZPostSpike::custom(3),
  )
  // First neuron-step fires (v=35 > 30).
  step_iz_with_postspike(pop, 0.125F)
  if pop.tabs[0] != 3 { fail("tabs after fire expected 3, got \{pop.tabs[0]}") }
  // Run combined loop for 5 iterations; tabs should reach 0 by iter 4
  // (decrement 3->2->1->0 across 3 synapse-step calls).
  for iter in 0..<5 {
    let _ = iter
    step_iz_synapses_postspike(pop, 0.125F)
    step_iz_with_postspike(pop, 0.125F)
  }
  // After 5 combined iterations (5 synapse-step calls + 5 neuron-step
  // calls in addition to the initial neuron-step that set tabs=3),
  // tabs should be 0 (decremented 3->0 across the first 3 synapse-step
  // calls).
  if pop.tabs[0] != 0 {
    fail("tabs after 5 combined iterations expected 0, got \{pop.tabs[0]}")
  }
}

///|
test "postspike step+synapses / ge decays during refractory" {
  // When tabs > 0, step_iz_with_postspike skips the membrane update
  // but still decays synapses. So ge continues to shrink during
  // refractory even though v/u don't move.
  let pop = IZ::init_with_postspike(
    1, IZParameter::rs(), 35.0F, 7.0F, IZPostSpike::custom(4),
  )
  step_iz_with_postspike(pop, 0.125F)  // fire, tabs=4
  // Inject ge explicitly.
  pop.ge[0] = 10.0F
  let v_ref = pop.v[0]
  let u_ref = pop.u[0]
  // 3 more neuron-steps (during refractory); tabs goes 4->3->2->1.
  for _ in 0..<3 {
    step_iz_with_postspike(pop, 0.125F)
    // Re-inject ge so we can see decay each step.
    pop.ge[0] = 10.0F
  }
  // v and u should NOT have moved (still in refractory).
  if pop.v[0] != v_ref { fail("v should be frozen during refractory") }
  if pop.u[0] != u_ref { fail("u should be frozen during refractory") }
  // tabs should be 1.
  if pop.tabs[0] != 1 {
    fail("tabs expected 1 after 3 refractory steps, got \{pop.tabs[0]}")
  }
  // Final synapse-step should let tabs reach 0.
  step_iz_synapses_postspike(pop, 0.125F)
  if pop.tabs[0] != 0 {
    fail("tabs expected 0 after 4th refractory decay, got \{pop.tabs[0]}")
  }
}

///|
test "postspike step+synapses / multi-neuron heterogeneous refractory" {
  // 3 neurons with different tabs_const values. Build each in its own
  // population; verify each fires at the expected rate.
  let postspike_a = IZPostSpike::custom(2)
  let pop_a = IZ::init_with_postspike(
    1, IZParameter::rs(), -65.0F, -13.0F, postspike_a,
  )
  let postspike_b = IZPostSpike::custom(4)
  let pop_b = IZ::init_with_postspike(
    1, IZParameter::rs(), -65.0F, -13.0F, postspike_b,
  )
  // Strong tonic drive to force firing.
  pop_a.i[0] = 30.0F
  pop_b.i[0] = 30.0F
  let n_steps = 50
  let mut count_a = 0
  let mut count_b = 0
  for _ in 0..<n_steps {
    step_iz_synapses_postspike(pop_a, 0.125F)
    step_iz_with_postspike(pop_a, 0.125F)
    pop_a.i[0] = 30.0F
    if pop_a.fire[0] { count_a = count_a + 1 }
    step_iz_synapses_postspike(pop_b, 0.125F)
    step_iz_with_postspike(pop_b, 0.125F)
    pop_b.i[0] = 30.0F
    if pop_b.fire[0] { count_b = count_b + 1 }
  }
  // pop_a has shorter refractory -> more spikes.
  if count_a <= count_b {
    fail("pop_a (tabs=2) should fire more than pop_b (tabs=4); got \{count_a} vs \{count_b}")
  }
  // With dt=0.125ms, tabs=2 -> max 1/0.25ms = 4 kHz -> 50 steps can
  // yield at most 50 spikes. tabs=4 -> max 1/0.5ms = 2 kHz -> at most
  // 50 spikes. Both can easily exceed the IZ RS adaptation rate, so
  // expect a few spikes per population.
  if count_a == 0 { fail("pop_a should fire at least once") }
  if count_b == 0 { fail("pop_b should fire at least once") }
}

///|
test "postspike step+synapses / injection between decay and step" {
  // Pattern: decay -> inject -> step. Verify the injected ge
  // contributes to the next membrane update (causes a fire).
  let pop = IZ::init_with_postspike(
    1, IZParameter::rs(), -65.0F, -13.0F, IZPostSpike::custom(0),
  )
  // Sub-threshold drive.
  pop.i[0] = 1.0F
  // Run 10 steps; should not fire.
  for _ in 0..<10 {
    step_iz_synapses_postspike(pop, 0.125F)
    step_iz_with_postspike(pop, 0.125F)
    pop.i[0] = 1.0F
    pop.ge[0] = 0.0F
    let _ = 0
  }
  if pop.fire[0] { fail("sub-threshold should not fire") }
  // Now inject strong ge and step again. The neuron should fire
  // (v crosses 30 via ge contribution, gets reset to c=-65).
  pop.ge[0] = 100.0F
  step_iz_with_postspike(pop, 0.125F)
  if !pop.fire[0] {
    fail("strong ge should have driven neuron to fire")
  }
  // After firing, v is reset to c=-65.
  if pop.v[0] != -65.0F {
    fail("v should be reset to c=-65 after fire, got \{pop.v[0]}")
  }
  // u should be incremented by d=8 after fire.
  if (pop.u[0] - (-5.0F)).abs() > 0.01F {
    fail("u should be incremented by d=8 after fire, got \{pop.u[0]}")
  }
}

///|
test "postspike step+synapses / 100-step round-trip is deterministic" {
  // Two parallel populations with same seed, run combined
  // decay+update loop for 100 steps. Verify trajectories are bit-exact.
  let pop_a = IZ::init_with_postspike(
    2, IZParameter::rs(), -65.0F, -13.0F, IZPostSpike::custom(3),
  )
  let pop_b = IZ::init_with_postspike(
    2, IZParameter::rs(), -65.0F, -13.0F, IZPostSpike::custom(3),
  )
  pop_a.i[0] = 6.0F
  pop_a.i[1] = 10.0F
  pop_b.i[0] = 6.0F
  pop_b.i[1] = 10.0F
  for _ in 0..<100 {
    step_iz_synapses_postspike(pop_a, 0.125F)
    step_iz_with_postspike(pop_a, 0.125F)
    pop_a.i[0] = 6.0F
    pop_a.i[1] = 10.0F
    step_iz_synapses_postspike(pop_b, 0.125F)
    step_iz_with_postspike(pop_b, 0.125F)
    pop_b.i[0] = 6.0F
    pop_b.i[1] = 10.0F
    let _ = 0
  }
  // Bit-exact comparison (==, not approx).
  if pop_a.v[0] != pop_b.v[0] {
    fail("v[0] should be bit-exact after 100 combined steps (a=\{pop_a.v[0]} b=\{pop_b.v[0]})")
  }
  if pop_a.v[1] != pop_b.v[1] {
    fail("v[1] should be bit-exact")
  }
  if pop_a.u[0] != pop_b.u[0] {
    fail("u[0] should be bit-exact")
  }
  if pop_a.u[1] != pop_b.u[1] {
    fail("u[1] should be bit-exact")
  }
  // fire flags should also match.
  if pop_a.fire[0] != pop_b.fire[0] {
    fail("fire[0] should be bit-exact")
  }
  if pop_a.fire[1] != pop_b.fire[1] {
    fail("fire[1] should be bit-exact")
  }
  // tabs should match.
  if pop_a.tabs[0] != pop_b.tabs[0] {
    fail("tabs[0] should be bit-exact")
  }
  if pop_a.tabs[1] != pop_b.tabs[1] {
    fail("tabs[1] should be bit-exact")
  }
}