// Tests for the IF neuron.

///|
test "IF::new initialises v in [Vr, Vt)" {
  let param = IFParameter::new()
  let rng = Xoshiro::from_state(1UL, 2UL, 3UL, 4UL)
  let p = IF::new(5, param, rng)
  for k in 0..<5 {
    let v = p.v[k]
    if v < param.vr || v >= param.vt {
      fail("v[\{k}] = \{v} out of [Vr, Vt) = [\{param.vr}, \{param.vt})")
    }
  }
}

///|
test "IF::new zero state" {
  let param = IFParameter::new()
  let rng = Xoshiro::default()
  let p = IF::new(3, param, rng)
  for k in 0..<3 {
    assert_eq(p.fire[k], false)
    assert_eq(p.tabs[k], 0)
    assert_eq(p.i[k], 0.0F)
    assert_eq(p.syn_curr[k], 0.0F)
    assert_eq(p.ge[k], 0.0F)
    assert_eq(p.gi[k], 0.0F)
  }
}

///|
test "IF: tab_const / dt = 16 (0.125 ms step, 2ms refractory)" {
  let p_post = PostSpike::new()
  // 2ms / 0.125ms = 16
  let tabs_steps : Int = (p_post.tabs_const / 0.125F).to_int()
  assert_eq(tabs_steps, 16)
}

///|
test "IF: step_neuron produces no fire when v is below Vt" {
  // Reset v to a value well below threshold.
  let param = IFParameter::new()
  let rng = Xoshiro::default()
  let p = IF::new(1, param, rng)
  p.v[0] = -100.0F
  p.i[0] = 0.0F
  p.syn_curr[0] = 0.0F
  step_neuron(p, 0.125F)
  assert_eq(p.fire[0], false)
}
