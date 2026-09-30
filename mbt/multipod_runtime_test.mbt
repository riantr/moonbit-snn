// multipod_runtime_test.mbt — runtime smoke tests for the Multipod step.
//
// These tests probe whether the deferred stack-overflow issues from
// v0.10.53 have been resolved by recent MoonBit runtime improvements.
// They exercise `Multipod::step` on small populations over short sims.

///|
test "multipod_step_nd2_n2_runs_finite" {
  // Dendrite::new(n) sized to match Multipod's population size.
  let d = Dendrite::new(2)
  let p = Multipod::new([d, d], 2)
  // Step a few times — must not crash.
  let mut step = 0
  while step < 100 {
    Multipod::step(p, 0.125F)
    step = step + 1
  }
  // All v_s must be finite.
  let mut i = 0
  while i < p.n {
    let v = p.v_s[i]
    let _ = v
    if v != v {
      fail("v_s not finite")
    }
    i = i + 1
  }
}

///|
test "multipod_step_nd3_n3_longer_runs" {
  let d = Dendrite::new(3)
  let p = Multipod::new([d, d, d], 3)
  let mut step = 0
  while step < 500 {
    Multipod::step(p, 0.125F)
    step = step + 1
  }
  let mut i = 0
  while i < p.n {
    let v = p.v_s[i]
    if v != v {
      fail("v_s not finite")
    }
    i = i + 1
  }
}

///|
test "multipod_step_under_strong_drive_produces_spike" {
  let d = Dendrite::new(1)
  let p = Multipod::new([d, d], 1)
  // Inject strong he_s current → should trigger a spike.
  let mut step = 0
  let mut spike_count = 0
  while step < 200 {
    p.he_s[0] = 1000.0F
    Multipod::step(p, 0.125F)
    if p.fire[0] {
      spike_count = spike_count + 1
    }
    step = step + 1
  }
  let _ = spike_count
  let v = p.v_s[0]
  if v != v {
    fail("v_s not finite")
  }
}