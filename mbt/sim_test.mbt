// Tests for the Monitor sr-based sub-sampling.

///|
test "Monitor without sr records every step" {
  let param = IFParameter::new()
  let rng = Xoshiro::default()
  let p = IF::new(1, param, rng)
  let m = Monitor::new_v(p, 0)
  for _ in 0..<100 {
    record_one(m, 0.0F)
  }
  assert_eq(m.data.length(), 100)
  assert_eq(m.times.length(), 100)
}

///|
test "Monitor with sr=1kHz records every 8th step" {
  let param = IFParameter::new()
  let rng = Xoshiro::default()
  let p = IF::new(1, param, rng)
  // 1 kHz in our internal units = 1.0F. With dt=0.125F, rec_step = 8.
  let m = Monitor::new_v_sr(p, 0, 1.0F)
  for _ in 0..<800 {
    record_one(m, 0.0F)
  }
  // Should record 800 / 8 = 100 entries.
  assert_eq(m.data.length(), 100)
  assert_eq(m.times.length(), 100)
}

///|
test "Monitor with sr=8kHz records every step" {
  let param = IFParameter::new()
  let rng = Xoshiro::default()
  let p = IF::new(1, param, rng)
  // 8 kHz in our internal units = 8.0F. rec_step = max(1, 1) = 1.
  let m = Monitor::new_v_sr(p, 0, 8.0F)
  for _ in 0..<100 {
    record_one(m, 0.0F)
  }
  // 100 / 1 = 100 entries.
  assert_eq(m.data.length(), 100)
}

///|
test "Monitor with sr=0.5kHz records every 16th step" {
  let param = IFParameter::new()
  let rng = Xoshiro::default()
  let p = IF::new(1, param, rng)
  // 500 Hz in our internal units = 0.5F. rec_step = 1/(0.5*0.125) = 16.
  let m = Monitor::new_v_sr(p, 0, 0.5F)
  for _ in 0..<100 {
    record_one(m, 0.0F)
  }
  // 100 / 16 ≈ 6 records (one every 16 steps).
  assert_eq(m.data.length(), 6)
}
