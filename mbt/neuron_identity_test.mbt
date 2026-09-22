// Tests for Identity neuron.

///|
test "Identity::new constructs population with zero state" {
  let p = Identity::new(3, { dummy: 0.0F })
  assert_eq(p.n, 3)
  assert_eq(p.g.length(), 3)
  assert_eq(p.h.length(), 3)
  assert_eq(p.fire.length(), 3)
  assert_eq(p.spikecount.length(), 3)
  assert_eq(p.g[0], 0.0F)
  assert_eq(p.fire[0], false)
}

///|
test "Identity::step_neuron_id fires when g > 0" {
  let p = Identity::new(2, { dummy: 0.0F })
  p.g[0] = 5.0F  // spike this step
  p.g[1] = 0.0F  // no spike
  step_neuron_id(p, 0.125F)
  assert_eq(p.fire[0], true)
  assert_eq(p.fire[1], false)
  assert_eq(p.h[0], 5.0F)
  assert_eq(p.spikecount[0], 5.0F)
  assert_eq(p.g[0], 0.0F)  // reset for next step
  assert_eq(p.g[1], 0.0F)
}

///|
test "Identity::step_neuron_id accumulates h across steps" {
  let p = Identity::new(1, { dummy: 0.0F })
  p.g[0] = 1.0F
  step_neuron_id(p, 0.125F)
  assert_eq(p.h[0], 1.0F)
  // Next step, no input.
  p.g[0] = 0.0F
  step_neuron_id(p, 0.125F)
  assert_eq(p.fire[0], false)
  assert_eq(p.h[0], 1.0F)  // h unchanged
}