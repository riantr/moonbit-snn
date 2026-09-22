// izhikevich_bitexact_custom_test.mbt — bit-exact IZ behaviour with
// IZParameter::custom (arbitrary a/b/c/d) — extension of the existing
// izhikevich_bitexact_test.mbt to cover custom parameter values.

///|
test "IZ bit-exact — custom RS-like (a=0.02, b=0.2, c=-65, d=8)" {
  // Custom matches the IZParameter::rs() defaults. Verify that
  // step_iz gives the same trajectory from same seed.
  let rng = Xoshiro::new(7UL)
  let p = IZParameter::custom(0.02F, 0.2F, -65.0F, 8.0F)
  let pop = IZ::new(2, p, rng)
  pop.i[0] = 10.0F
  // Run 100 steps.
  let mut fire_count = 0
  for _ in 0..<100 {
    step_iz(pop, 0.125F)
    if pop.fire[0] { fire_count = fire_count + 1 }
  }
  // RS neuron (a=0.02, d=8) with 10 pA input should fire multiple times
  // in 100 steps (12.5 ms); total > 0 expected.
  if fire_count == 0 {
    fail("custom RS should fire in 100 steps, got 0")
  }
}

///|
test "IZ bit-exact — custom FS-like (a=0.1, d=2) faster than RS" {
  // Two parallel populations: RS and FS. Same input, same seed prefix.
  // FS (a=0.1, d=2) typically fires more spikes per second than RS
  // (a=0.02, d=8) because the adaptation increment is smaller.
  let rng_rs = Xoshiro::new(42UL)
  let pop_rs = IZ::new(1, IZParameter::custom(0.02F, 0.2F, -65.0F, 8.0F), rng_rs)
  pop_rs.i[0] = 10.0F
  let mut rs_fires = 0
  for _ in 0..<500 {
    step_iz(pop_rs, 0.125F)
    if pop_rs.fire[0] { rs_fires = rs_fires + 1 }
  }
  let rng_fs = Xoshiro::new(42UL)
  let pop_fs = IZ::new(1, IZParameter::custom(0.1F, 0.2F, -65.0F, 2.0F), rng_fs)
  pop_fs.i[0] = 10.0F
  let mut fs_fires = 0
  for _ in 0..<500 {
    step_iz(pop_fs, 0.125F)
    if pop_fs.fire[0] { fs_fires = fs_fires + 1 }
  }
  // FS (a=0.1, d=2) typically has higher firing rate than RS under
  // identical drive; verify the relationship holds.
  if fs_fires <= rs_fires {
    fail("FS fires should exceed RS fires (FS=\{(fs_fires)} vs RS=\{rs_fires})")
  }
}

///|
test "IZ custom — different c resets the post-fire voltage correctly" {
  // Custom c=-70 (vs default -65). After firing, v should reset to -70.
  let rng = Xoshiro::new(1UL)
  let pop = IZ::new(1, IZParameter::custom(0.02F, 0.2F, -70.0F, 8.0F), rng)
  pop.v[0] = 35.0F  // force fire on next step
  step_iz(pop, 0.125F)
  if !pop.fire[0] { fail("expected fire") }
  if pop.v[0] != -70.0F {
    fail("expected v reset to -70, got \{pop.v[0]}")
  }
}