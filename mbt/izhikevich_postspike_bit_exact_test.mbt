// izhikevich_postspike_bit_exact_test.mbt — bit-exact refractory timing
// verification: trace when fire happens + when membrane resumes + when
// tabs reaches zero.

///|
test "postspike timing — tabs counts down 2,1,0 after single fire" {
  let ps = IZPostSpike::custom(2)
  let pop = IZ::init_with_postspike(1, IZParameter::rs(), 35.0F, 7.0F, ps)
  // Step 1: fire, tabs → 2.
  step_iz_with_postspike(pop, 0.125F)
  if !pop.fire[0] { fail("step 1 expected fire") }
  if pop.tabs[0] != 2 { fail("tabs after fire expected 2, got \{pop.tabs[0]}") }
  // Step 2: refractory (tabs=2 → 1).
  step_iz_with_postspike(pop, 0.125F)
  if pop.tabs[0] != 1 { fail("tabs after step 2 expected 1, got \{pop.tabs[0]}") }
  // Step 3: refractory (tabs=1 → 0).
  step_iz_with_postspike(pop, 0.125F)
  if pop.tabs[0] != 0 { fail("tabs after step 3 expected 0, got \{pop.tabs[0]}") }
  // Step 4: refractory over; membrane updates resume.
  pop.v[0] = -65.0F  // restart from rest
  pop.u[0] = -13.0F
  step_iz_with_postspike(pop, 0.125F)
  // tabs should remain 0 (no fire on step 4 since v=-65).
  if pop.tabs[0] != 0 { fail("tabs should stay 0 (no re-fire)") }
}

///|
test "postspike timing — tabs_const=5: 5 steps of refractory" {
  let ps = IZPostSpike::custom(5)
  let pop = IZ::init_with_postspike(1, IZParameter::rs(), 35.0F, 7.0F, ps)
  step_iz_with_postspike(pop, 0.125F)
  if pop.tabs[0] != 5 { fail("tabs expected 5") }
  // Each subsequent step decrements tabs.
  let expected : Array[Int] = [4, 3, 2, 1, 0]
  let mut step = 0
  while step < 5 {
    step_iz_with_postspike(pop, 0.125F)
    if pop.tabs[0] != expected[step] {
      fail("step \{step + 1} tabs expected \{expected[step]}, got \{pop.tabs[0]}")
    }
    step = step + 1
  }
}

///|
test "postspike timing — second fire after refractory re-extends" {
  // First fire → tabs=2 → wait 2 steps → second fire → tabs=2 again.
  let ps = IZPostSpike::custom(2)
  let pop = IZ::init_with_postspike(1, IZParameter::rs(), 35.0F, 7.0F, ps)
  step_iz_with_postspike(pop, 0.125F)  // fire #1, tabs=2
  if !pop.fire[0] { fail("fire #1") }
  step_iz_with_postspike(pop, 0.125F)  // tabs=1
  step_iz_with_postspike(pop, 0.125F)  // tabs=0, refractory over
  // Now force fire again.
  pop.v[0] = 35.0F
  pop.u[0] = 7.0F
  step_iz_with_postspike(pop, 0.125F)
  if !pop.fire[0] { fail("fire #2") }
  if pop.tabs[0] != 2 { fail("tabs after second fire expected 2, got \{pop.tabs[0]}") }
  // V should be reset to c=-65.
  if pop.v[0] != -65.0F { fail("v should reset to -65") }
}