// izhikevich_synapses_postspike_test.mbt — tests for
// step_iz_synapses_postspike (standalone synapse decay with
// refractory handling).

///|
test "step_iz_synapses_postspike — decayed ge with no refractory" {
  // Default IZ::new has tabs_const=0 → no refractory; same as
  // step_iz_synapses.
  let pop = IZ::new(1, IZParameter::rs(), Xoshiro::new(1UL))
  pop.ge[0] = 1.0F
  pop.gi[0] = 0.5F
  step_iz_synapses_postspike(pop, 0.125F)
  if (pop.ge[0] - 0.975F).abs() > 1.0e-4 {
    fail("ge expected ≈0.975, got \{pop.ge[0]}")
  }
  if (pop.gi[0] - 0.49375F).abs() > 1.0e-4 {
    fail("gi expected ≈0.494, got \{pop.gi[0]}")
  }
}

///|
test "step_iz_synapses_postspike — tabs countdown during refractory" {
  // Force-fire with tabs_const=2, set ge=1.0 to verify decay, then run
  // synapse decay twice. tabs decrements from 2 → 1 → 0; ge decays.
  let ps = IZPostSpike::custom(2)
  let pop = IZ::init_with_postspike(1, IZParameter::rs(), 35.0F, 7.0F, ps)
  pop.ge[0] = 1.0F
  step_iz_with_postspike(pop, 0.125F)  // fire, tabs=2
  if pop.tabs[0] != 2 { fail("tabs after fire expected 2") }
  let ge_after_fire = pop.ge[0]
  if ge_after_fire >= 1.0F { fail("ge should have decayed slightly during fire step") }
  // Step 2: tabs=2 → 1, ge decays.
  step_iz_synapses_postspike(pop, 0.125F)
  if pop.tabs[0] != 1 { fail("tabs after step 2 expected 1, got \{pop.tabs[0]}") }
  if pop.ge[0] >= ge_after_fire { fail("ge should decay further") }
  // Step 3: tabs=1 → 0, ge decays again.
  let ge_after_step2 = pop.ge[0]
  step_iz_synapses_postspike(pop, 0.125F)
  if pop.tabs[0] != 0 { fail("tabs after step 3 expected 0, got \{pop.tabs[0]}") }
  if pop.ge[0] >= ge_after_step2 { fail("ge should decay further") }
}

///|
test "step_iz_synapses_postspike — does NOT modify v or u" {
  let pop = IZ::init_with_postspike(1, IZParameter::rs(), 35.0F, 7.0F, IZPostSpike::custom(2))
  let v_before = pop.v[0]
  let u_before = pop.u[0]
  step_iz_synapses_postspike(pop, 0.125F)
  if pop.v[0] != v_before { fail("v should not change") }
  if pop.u[0] != u_before { fail("u should not change") }
}

///|
test "step_iz_synapses_postspike — tabs_const=0 ≡ step_iz_synapses" {
  let rng = Xoshiro::new(42UL)
  let pop_a = IZ::new(1, IZParameter::rs(), rng)
  pop_a.ge[0] = 1.0F
  step_iz_synapses(pop_a, 0.125F)
  let pop_b = IZ::init_with_postspike(1, IZParameter::rs(), -65.0F, -13.0F, IZPostSpike::custom(0))
  pop_b.ge[0] = 1.0F
  step_iz_synapses_postspike(pop_b, 0.125F)
  // Should produce identical ge/gi (no refractory).
  if (pop_a.ge[0] - pop_b.ge[0]).abs() > 1.0e-4 {
    fail("ge mismatch: \{pop_a.ge[0]} vs \{pop_b.ge[0]}")
  }
}

///|
test "step_iz_synapses_postspike — multi-neuron heterogeneous tabs" {
  // 3 neurons. Force-fire all → tabs each = 2. Then run synapse decay.
  let pop = IZ::init_with_postspike(3, IZParameter::rs(), 35.0F, 7.0F, IZPostSpike::custom(2))
  step_iz_with_postspike(pop, 0.125F)  // all fire
  if pop.tabs[0] != 2 || pop.tabs[1] != 2 || pop.tabs[2] != 2 {
    fail("expected all tabs=2 after step 1")
  }
  step_iz_synapses_postspike(pop, 0.125F)
  if pop.tabs[0] != 1 || pop.tabs[1] != 1 || pop.tabs[2] != 1 {
    fail("expected all tabs=1 after step 2")
  }
}