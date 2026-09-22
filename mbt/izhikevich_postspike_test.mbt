// izhikevich_postspike_test.mbt — tests for IZPostSpike + IZ::init_with_postspike.

///|
test "IZPostSpike::new — default 1-step refractory" {
  let ps = IZPostSpike::new()
  if ps.tabs_const != 1 { fail("default tabs_const expected 1, got \{ps.tabs_const}") }
}

///|
test "IZPostSpike::custom — arbitrary refractory period" {
  let ps = IZPostSpike::custom(8)
  if ps.tabs_const != 8 { fail("tabs_const expected 8") }
  let ps2 = IZPostSpike::custom(0)
  if ps2.tabs_const != 0 { fail("tabs_const expected 0 (no refractory)") }
}

///|
test "IZ::init_with_postspike — initial state honours v_init / u_init" {
  let postspike = IZPostSpike::custom(4)
  let pop = IZ::init_with_postspike(3, IZParameter::rs(), -65.0F, -13.0F, postspike)
  if pop.n != 3 { fail("n expected 3") }
  let mut k = 0
  while k < 3 {
    if pop.v[k] != -65.0F { fail("v[\{k}] expected -65") }
    if pop.u[k] != -13.0F { fail("u[\{k}] expected -13") }
    if pop.fire[k] { fail("fire[\{k}] should start false") }
    k = k + 1
  }
}

///|
test "IZ::init_with_postspike — step_iz evolves from initial state" {
  let postspike = IZPostSpike::custom(2)
  let pop = IZ::init_with_postspike(1, IZParameter::rs(), -50.0F, -10.0F, postspike)
  pop.i[0] = 10.0F
  step_iz(pop, 0.125F)
  // v should evolve; if it crossed 30 mV, fire.
  let v_before = -50.0F
  if pop.v[0] == v_before && !pop.fire[0] {
    fail("v did not change after step")
  }
}

///|
test "IZPostSpike + IZ::init_with_postspike — fire/i/ge/gi zero-init" {
  let pop = IZ::init_with_postspike(2, IZParameter::rs(), -65.0F, -13.0F, IZPostSpike::custom(2))
  if pop.fire[0] || pop.fire[1] { fail("fire should start false") }
  if pop.i[0] != 0.0F || pop.i[1] != 0.0F { fail("i should be 0") }
  if pop.ge[0] != 0.0F || pop.ge[1] != 0.0F { fail("ge should be 0") }
  if pop.gi[0] != 0.0F || pop.gi[1] != 0.0F { fail("gi should be 0") }
}