// izhikevich_reset_test.mbt — tests for iz_reset (state reset).

///|
test "iz_reset — restores v to -65 and u to b*-65" {
  let pop = IZ::new(2, IZParameter::rs(), Xoshiro::new(1UL))
  // Run a few steps to perturb state.
  pop.i[0] = 30.0F
  pop.i[1] = 30.0F
  for _ in 0..<20 {
    step_iz(pop, 0.125F)
  }
  // v and u should have moved away from initial state.
  let v_before_reset_0 = pop.v[0]
  let u_before_reset_0 = pop.u[0]
  if v_before_reset_0 == -65.0F { fail("v[0] should have moved") }
  if u_before_reset_0 == -13.0F { fail("u[0] should have moved") }
  // Reset.
  iz_reset(pop)
  if pop.v[0] != -65.0F { fail("v[0] should be -65, got \{pop.v[0]}") }
  if pop.v[1] != -65.0F { fail("v[1] should be -65, got \{pop.v[1]}") }
  // u = b * -65 = 0.2 * -65 = -13.
  if pop.u[0] != -13.0F { fail("u[0] should be -13, got \{pop.u[0]}") }
  if pop.u[1] != -13.0F { fail("u[1] should be -13, got \{pop.u[1]}") }
}

///|
test "iz_reset — clears fire, ge, gi, tabs" {
  let pop = IZ::init_with_postspike(2, IZParameter::rs(), 35.0F, 7.0F, IZPostSpike::custom(2))
  step_iz_with_postspike(pop, 0.125F)  // fire, tabs=2, ge may have small values
  if !pop.fire[0] { fail("step 1 expected fire") }
  // Manually set ge, gi to non-zero.
  pop.ge[0] = 5.0F
  pop.gi[1] = 3.0F
  iz_reset(pop)
  if pop.fire[0] || pop.fire[1] { fail("fire should be false after reset") }
  if pop.ge[0] != 0.0F { fail("ge[0] should be 0, got \{pop.ge[0]}") }
  if pop.gi[1] != 0.0F { fail("gi[1] should be 0, got \{pop.gi[1]}") }
  if pop.tabs[0] != 0 || pop.tabs[1] != 0 {
    fail("tabs should be 0 after reset")
  }
}

///|
test "iz_reset — does NOT modify i (external input)" {
  let pop = IZ::new(2, IZParameter::rs(), Xoshiro::new(1UL))
  pop.i[0] = 10.0F
  pop.i[1] = 20.0F
  iz_reset(pop)
  if pop.i[0] != 10.0F { fail("i[0] should be preserved, got \{pop.i[0]}") }
  if pop.i[1] != 20.0F { fail("i[1] should be preserved, got \{pop.i[1]}") }
}

///|
test "iz_reset — step_iz after reset produces deterministic trajectory" {
  // Two parallel populations: one runs 20 steps + reset + 20 more steps;
  // the other runs 20 steps + reset + 20 more steps with same seed.
  // Both should produce identical final state (since reset zeros all
  // perturbation).
  let pop_a = IZ::new(2, IZParameter::rs(), Xoshiro::new(7UL))
  let pop_b = IZ::new(2, IZParameter::rs(), Xoshiro::new(8UL))
  pop_a.i[0] = 5.0F
  pop_b.i[0] = 5.0F
  pop_a.i[1] = 5.0F
  pop_b.i[1] = 5.0F
  for _ in 0..<20 {
    step_iz(pop_a, 0.125F)
    step_iz(pop_b, 0.125F)
  }
  iz_reset(pop_a)
  iz_reset(pop_b)
  // After reset, both populations are at rest with same i.
  for _ in 0..<20 {
    step_iz(pop_a, 0.125F)
    step_iz(pop_b, 0.125F)
    // Restore i after each step.
    pop_a.i[0] = 5.0F
    pop_a.i[1] = 5.0F
    pop_b.i[0] = 5.0F
    pop_b.i[1] = 5.0F
  }
  if (pop_a.v[0] - pop_b.v[0]).abs() > 1.0e-6 {
    fail("after reset + 20 steps, v[0] should match (a=\{pop_a.v[0]} vs b=\{pop_b.v[0]})")
  }
  if (pop_a.u[0] - pop_b.u[0]).abs() > 1.0e-6 {
    fail("u[0] mismatch (a=\{pop_a.u[0]} vs b=\{pop_b.u[0]})")
  }
}

///|
test "iz_reset — works for FS parameters (b=0.2 still)" {
  // For FS (a=0.1, b=0.2, c=-65, d=2), u should reset to b * -65 = -13
  // (same as RS since b is identical).
  let pop = IZ::new(1, IZParameter::fs(), Xoshiro::new(1UL))
  // Run a few steps.
  pop.i[0] = 50.0F
  for _ in 0..<30 {
    step_iz(pop, 0.125F)
  }
  iz_reset(pop)
  if pop.v[0] != -65.0F { fail("FS v should be -65, got \{pop.v[0]}") }
  if pop.u[0] != -13.0F { fail("FS u should be -13, got \{pop.u[0]}") }
}