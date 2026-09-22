// izhikevich_step_synapses_test.mbt — tests for standalone synapse decay.

///|
test "step_iz_synapses — exponential decay of ge" {
  // Initial ge[0] = 1.0; tau_e = 5.0; dt = 0.125.
  // After step: ge = 1.0 + 0.125 * (-1.0 / 5.0) = 1.0 - 0.025 = 0.975.
  let rng = Xoshiro::new(42UL)
  let pop = IZ::new(2, IZParameter::rs(), rng)
  pop.ge[0] = 1.0F
  pop.gi[0] = 0.5F
  pop.ge[1] = 2.0F
  step_iz_synapses(pop, 0.125F)
  if (pop.ge[0] - 0.975F).abs() > 1.0e-4 {
    fail("ge[0] expected ~0.975, got \{pop.ge[0]}")
  }
  if (pop.gi[0] - 0.49375F).abs() > 1.0e-4 {
    fail("gi[0] expected ~0.49375, got \{pop.gi[0]}")
  }
  if (pop.ge[1] - 1.95F).abs() > 1.0e-4 {
    fail("ge[1] expected ~1.95, got \{pop.ge[1]}")
  }
}

///|
test "step_iz_synapses — does not touch v or u" {
  let rng = Xoshiro::new(42UL)
  let pop = IZ::new(3, IZParameter::rs(), rng)
  let v_before = pop.v[0]
  let u_before = pop.u[0]
  pop.ge[0] = 1.0F
  step_iz_synapses(pop, 0.125F)
  if pop.v[0] != v_before { fail("v[0] should not change") }
  if pop.u[0] != u_before { fail("u[0] should not change") }
}

///|
test "step_iz_synapses — 100-step stability (ge decays to 0)" {
  // Starting from ge=1.0 with tau_e=5, after many steps ge should
  // approach 0 (exponential decay).
  let rng = Xoshiro::new(42UL)
  let pop = IZ::new(1, IZParameter::rs(), rng)
  pop.ge[0] = 1.0F
  for _ in 0..<1000 {
    step_iz_synapses(pop, 0.1F)
  }
  // After 1000 steps × 0.1 = 100 ms, with τe=5ms: ge ≈ exp(-100/5) ≈ 4.5e-9
  if pop.ge[0] > 1.0e-6F {
    fail("ge[0] should be near 0 after long decay, got \{pop.ge[0]}")
  }
}

///|
test "step_iz then step_iz_synapses — full equivalence" {
  // Verify that calling step_iz (which internally does both) produces
  // the same ge decay as calling step_iz_synapses separately.
  let rng1 = Xoshiro::new(42UL)
  let rng2 = Xoshiro::new(42UL)
  let p1 = IZ::new(3, IZParameter::rs(), rng1)
  let p2 = IZ::new(3, IZParameter::rs(), rng2)
  p1.ge[0] = 1.0F
  p2.ge[0] = 1.0F
  p1.gi[1] = 0.5F
  p2.gi[1] = 0.5F
  step_iz(p1, 0.125F)
  step_iz_synapses(p2, 0.125F)
  // Compare ge/gi after both calls.
  if (p1.ge[0] - p2.ge[0]).abs() > 1.0e-4 {
    fail("ge[0] mismatch: step_iz=\{p1.ge[0]} vs step_iz_synapses=\{p2.ge[0]}")
  }
  if (p1.gi[1] - p2.gi[1]).abs() > 1.0e-4 {
    fail("gi[1] mismatch: step_iz=\{p1.gi[1]} vs step_iz_synapses=\{p2.gi[1]}")
  }
}