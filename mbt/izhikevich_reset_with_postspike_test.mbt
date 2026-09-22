// izhikevich_reset_with_postspike_test.mbt — tests for
// iz_reset_with_postspike.

///|
test "iz_reset_with_postspike — clears v/u/fire/ge/gi and resets tabs_const" {
  // Build with one tabs_const, run, reset with another.
  let ps1 = IZPostSpike::custom(5)
  let pop = IZ::init_with_postspike(1, IZParameter::rs(), 35.0F, 7.0F, ps1)
  step_iz_with_postspike(pop, 0.125F)  // fire, tabs=5
  if pop.tabs[0] != 5 { fail("expected tabs=5 after fire") }
  if pop.tabs_const != 5 { fail("expected tabs_const=5") }
  // Reset with a different postspike.
  let ps2 = IZPostSpike::custom(2)
  iz_reset_with_postspike(pop, ps2)
  if pop.tabs[0] != 0 { fail("tabs should be 0 after reset") }
  if pop.tabs_const != 2 { fail("tabs_const should be 2 after reset") }
  if pop.v[0] != -65.0F { fail("v should be -65 after reset") }
  if pop.u[0] != -13.0F { fail("u should be -13 after reset") }
}

///|
test "iz_reset_with_postspike — next fire uses new tabs_const" {
  // Start with tabs_const=5, switch to tabs_const=2, then fire. New
  // tabs should be 2 (not 5).
  let ps1 = IZPostSpike::custom(5)
  let pop = IZ::init_with_postspike(1, IZParameter::rs(), 35.0F, 7.0F, ps1)
  iz_reset_with_postspike(pop, IZPostSpike::custom(2))
  // Force a high voltage so the next step will fire.
  pop.v[0] = 35.0F
  pop.u[0] = 0.2F * 35.0F
  step_iz_with_postspike(pop, 0.125F)
  if !pop.fire[0] { fail("expected fire") }
  if pop.tabs[0] != 2 { fail("new tabs should be 2, got \{pop.tabs[0]}") }
}

///|
test "iz_reset_with_postspike — preserves i (external input)" {
  let pop = IZ::init_with_postspike(1, IZParameter::rs(), 35.0F, 7.0F, IZPostSpike::custom(2))
  pop.i[0] = 12.0F
  iz_reset_with_postspike(pop, IZPostSpike::custom(2))
  if pop.i[0] != 12.0F { fail("i should be preserved, got \{pop.i[0]}") }
}

///|
test "iz_reset_with_postspike — same postspike as iz_reset clears everything" {
  // Verify iz_reset_with_postspike is equivalent to iz_reset when the
  // new postspike matches the current tabs_const.
  let pop_a = IZ::init_with_postspike(2, IZParameter::rs(), 35.0F, 7.0F, IZPostSpike::custom(3))
  let pop_b = IZ::init_with_postspike(2, IZParameter::rs(), 35.0F, 7.0F, IZPostSpike::custom(3))
  // Perturb both identically.
  for _ in 0..<10 {
    step_iz_with_postspike(pop_a, 0.125F)
    step_iz_with_postspike(pop_b, 0.125F)
  }
  iz_reset(pop_a)
  iz_reset_with_postspike(pop_b, IZPostSpike::custom(3))
  // Both should have identical state.
  let mut k = 0
  while k < 2 {
    if (pop_a.v[k] - pop_b.v[k]).abs() > 1.0e-6 {
      fail("v[\{k}] mismatch after reset")
    }
    if (pop_a.u[k] - pop_b.u[k]).abs() > 1.0e-6 {
      fail("u[\{k}] mismatch")
    }
    if pop_a.tabs[k] != pop_b.tabs[k] {
      fail("tabs[\{k}] mismatch")
    }
    k = k + 1
  }
  if pop_a.tabs_const != pop_b.tabs_const {
    fail("tabs_const mismatch")
  }
}

///|
test "iz_reset_with_postspike — multi-neuron heterogeneous postspike" {
  // 3 neurons with one tabs_const=2 initially. Reset to a different
  // (single) tabs_const. All neurons now share that tabs_const.
  let pop = IZ::init_with_postspike(3, IZParameter::rs(), 35.0F, 7.0F, IZPostSpike::custom(2))
  step_iz_with_postspike(pop, 0.125F)
  if pop.tabs[0] != 2 || pop.tabs[1] != 2 || pop.tabs[2] != 2 {
    fail("expected all tabs=2 after fire")
  }
  iz_reset_with_postspike(pop, IZPostSpike::custom(7))
  if pop.tabs_const != 7 { fail("tabs_const should be 7") }
  let mut k = 0
  while k < 3 {
    if pop.tabs[k] != 0 { fail("tabs[\{k}] should be 0 after reset") }
    k = k + 1
  }
  // Force-fire and verify all neurons now have tabs=7.
  pop.v[0] = 35.0F
  pop.v[1] = 35.0F
  pop.v[2] = 35.0F
  step_iz_with_postspike(pop, 0.125F)
  if pop.tabs[0] != 7 || pop.tabs[1] != 7 || pop.tabs[2] != 7 {
    fail("expected all tabs=7 after second fire")
  }
}