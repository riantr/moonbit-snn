// Tests for vecplot helpers (count_spikes, dump_summary, dump_csv_stdout).

///|
test "count_spikes counts rising edges" {
  let param = IFParameter::new()
  let rng = Xoshiro::default()
  let p = IF::new(1, param, rng)
  let m = Monitor::new_fire(p, 0)
  // Simulate a 3-spike sequence: 0,1,0,0,1,0,0,1,0
  let pattern : Array[Float] = [0.0F, 1.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 1.0F, 0.0F]
  for v in pattern {
    m.data.push(v)
  }
  assert_eq(m.count_spikes(), 3)
}

///|
test "count_spikes on empty monitor is 0" {
  let param = IFParameter::new()
  let rng = Xoshiro::default()
  let p = IF::new(1, param, rng)
  let m = Monitor::new_fire(p, 0)
  assert_eq(m.count_spikes(), 0)
}

///|
test "dump_summary prints min/max/mean" {
  let param = IFParameter::new()
  let rng = Xoshiro::default()
  let p = IF::new(1, param, rng)
  let m = Monitor::new_v(p, 0)
  m.data.push(-65.0F)
  m.data.push(-60.0F)
  m.data.push(-70.0F)
  // min=-70, max=-60, mean=-65
  m.dump_summary()
}

///|
test "duration returns t[-1] - t[0]" {
  let param = IFParameter::new()
  let rng = Xoshiro::default()
  let p = IF::new(1, param, rng)
  let m = Monitor::new_v(p, 0)
  m.times.push(0.0F)
  m.times.push(100.0F)
  m.times.push(200.0F)
  assert_eq(m.duration(), 200.0F)
}

///|
test "duration of empty monitor is 0" {
  let param = IFParameter::new()
  let rng = Xoshiro::default()
  let p = IF::new(1, param, rng)
  let m = Monitor::new_v(p, 0)
  assert_eq(m.duration(), 0.0F)
}

///|
test "ascii_plot renders without crashing on a v monitor" {
  let param = IFParameter::new()
  let rng = Xoshiro::default()
  let p = IF::new(1, param, rng)
  let m = Monitor::new_v(p, 0)
  // Push a sawtooth-like trace.
  let mut k = 0
  while k < 80 {
    let t = Float::from_int(k) * 0.05F
    let v = -65.0F + (k % 20).to_float()
    m.data.push(v)
    m.times.push(t)
    k = k + 1
  }
  m.ascii_plot()  // should not crash; output verified visually
  // Also test with custom width/height.
  m.ascii_plot(width=40, height=10)
}

///|
test "ascii_plot renders spike markers for fire monitor" {
  let param = IFParameter::new()
  let rng = Xoshiro::default()
  let p = IF::new(1, param, rng)
  let m = Monitor::new_fire(p, 0)
  for k in 0..<30 {
    let f = if k == 5 || k == 15 || k == 25 { 1.0F } else { 0.0F }
    m.data.push(f)
    m.times.push(Float::from_int(k) * 0.1F)
  }
  m.ascii_plot(width=30, height=5)
}

///|
test "ascii_plot on empty monitor is a no-op" {
  let param = IFParameter::new()
  let rng = Xoshiro::default()
  let p = IF::new(1, param, rng)
  let m = Monitor::new_v(p, 0)
  m.ascii_plot()  // should print "(empty)" without crashing
}

///|
test "firing_rate computes Hz from spikes and duration" {
  let param = IFParameter::new()
  let rng = Xoshiro::default()
  let p = IF::new(1, param, rng)
  let m = Monitor::new_fire(p, 0)
  // 5 spikes over 1000ms = 5 Hz.
  m.data.push(0.0F); m.times.push(0.0F)
  m.data.push(1.0F); m.times.push(50.0F)
  m.data.push(0.0F); m.times.push(100.0F)
  m.data.push(1.0F); m.times.push(200.0F)
  m.data.push(0.0F); m.times.push(300.0F)
  m.data.push(1.0F); m.times.push(500.0F)
  m.data.push(0.0F); m.times.push(700.0F)
  m.data.push(1.0F); m.times.push(800.0F)
  m.data.push(0.0F); m.times.push(900.0F)
  m.data.push(1.0F); m.times.push(950.0F)
  m.data.push(0.0F); m.times.push(1000.0F)
  let fr = m.firing_rate()
  if fr < 4.99F || fr > 5.01F {
    fail("firing_rate: " + fr.to_string() + " expected ~5 Hz")
  }
}

///|
test "firing_rate on empty monitor is 0" {
  let param = IFParameter::new()
  let rng = Xoshiro::default()
  let p = IF::new(1, param, rng)
  let m = Monitor::new_fire(p, 0)
  assert_eq(m.firing_rate(), 0.0F)
}

///|
test "firing_rate on zero-duration monitor is 0" {
  let param = IFParameter::new()
  let rng = Xoshiro::default()
  let p = IF::new(1, param, rng)
  let m = Monitor::new_fire(p, 0)
  m.data.push(0.0F); m.times.push(100.0F)
  m.data.push(1.0F); m.times.push(100.0F)
  assert_eq(m.firing_rate(), 0.0F)
}

///|
test "spike_times returns timestamps at rising edges" {
  let param = IFParameter::new()
  let rng = Xoshiro::default()
  let p = IF::new(1, param, rng)
  let m = Monitor::new_fire(p, 0)
  // 3 spikes at t=50, 200, 700.
  m.data.push(0.0F); m.times.push(0.0F)
  m.data.push(1.0F); m.times.push(50.0F)
  m.data.push(0.0F); m.times.push(100.0F)
  m.data.push(1.0F); m.times.push(200.0F)
  m.data.push(0.0F); m.times.push(300.0F)
  m.data.push(0.0F); m.times.push(500.0F)
  m.data.push(1.0F); m.times.push(700.0F)
  m.data.push(0.0F); m.times.push(900.0F)
  let st = m.spike_times()
  assert_eq(st.length(), 3)
  assert_eq(st[0], 50.0F)
  assert_eq(st[1], 200.0F)
  assert_eq(st[2], 700.0F)
}

///|
test "spike_times on empty monitor returns empty array" {
  let param = IFParameter::new()
  let rng = Xoshiro::default()
  let p = IF::new(1, param, rng)
  let m = Monitor::new_fire(p, 0)
  assert_eq(m.spike_times().length(), 0)
}

///|
test "count_spikes_interval filters by time window" {
  let param = IFParameter::new()
  let rng = Xoshiro::default()
  let p = IF::new(1, param, rng)
  let m = Monitor::new_fire(p, 0)
  // 5 spikes at t=50, 200, 700, 800, 950.
  m.data.push(0.0F); m.times.push(0.0F)
  m.data.push(1.0F); m.times.push(50.0F)
  m.data.push(0.0F); m.times.push(100.0F)
  m.data.push(1.0F); m.times.push(200.0F)
  m.data.push(0.0F); m.times.push(300.0F)
  m.data.push(1.0F); m.times.push(700.0F)
  m.data.push(0.0F); m.times.push(750.0F)
  m.data.push(1.0F); m.times.push(800.0F)
  m.data.push(0.0F); m.times.push(850.0F)
  m.data.push(1.0F); m.times.push(950.0F)
  m.data.push(0.0F); m.times.push(1000.0F)
  // Window [0, 500]: should contain spikes at 50, 200 (2 spikes).
  assert_eq(m.count_spikes_interval(0.0F, 500.0F), 2)
  // Window [500, 1000]: should contain spikes at 700, 800, 950 (3 spikes).
  assert_eq(m.count_spikes_interval(500.0F, 1000.0F), 3)
  // Window [600, 750]: should contain only the 700 spike.
  assert_eq(m.count_spikes_interval(600.0F, 750.0F), 1)
  // Window with no spikes.
  assert_eq(m.count_spikes_interval(3000.0F, 4000.0F), 0)
}

///|
test "mean / min / max compute basic statistics" {
  let param = IFParameter::new()
  let rng = Xoshiro::default()
  let p = IF::new(1, param, rng)
  let m = Monitor::new_v(p, 0)
  m.data.push(-65.0F)
  m.data.push(-60.0F)
  m.data.push(-70.0F)
  // mean = (-65 -60 -70) / 3 = -65
  // min = -70, max = -60
  if m.mean() < -65.01F || m.mean() > -64.99F {
    fail("mean: " + m.mean().to_string() + " expected ~-65")
  }
  assert_eq(m.min(), -70.0F)
  assert_eq(m.max(), -60.0F)
}

///|
test "mean on empty monitor is 0" {
  let param = IFParameter::new()
  let rng = Xoshiro::default()
  let p = IF::new(1, param, rng)
  let m = Monitor::new_v(p, 0)
  assert_eq(m.mean(), 0.0F)
}

///|
test "firing_rate_interval computes Hz in sub-window" {
  let param = IFParameter::new()
  let rng = Xoshiro::default()
  let p = IF::new(1, param, rng)
  let m = Monitor::new_fire(p, 0)
  // 2 spikes in [500, 700] (at t=550 and t=650), 1 spike outside.
  m.data.push(0.0F); m.times.push(0.0F)
  m.data.push(1.0F); m.times.push(50.0F)
  m.data.push(0.0F); m.times.push(100.0F)
  m.data.push(1.0F); m.times.push(550.0F)
  m.data.push(0.0F); m.times.push(600.0F)
  m.data.push(1.0F); m.times.push(650.0F)
  m.data.push(0.0F); m.times.push(700.0F)
  // Window [500, 1000] duration=500ms, 2 spikes → 4 Hz.
  let fr = m.firing_rate_interval(500.0F, 1000.0F)
  if fr < 3.99F || fr > 4.01F {
    fail("expected ~4 Hz in [500,1000], got " + fr.to_string())
  }
  // Empty interval (start == end) → 0.
  assert_eq(m.firing_rate_interval(500.0F, 500.0F), 0.0F)
}

///|
test "clear_records resets data and times" {
  let param = IFParameter::new()
  let rng = Xoshiro::default()
  let p = IF::new(1, param, rng)
  let m = Monitor::new_fire(p, 0)
  m.data.push(0.0F); m.times.push(0.0F)
  m.data.push(1.0F); m.times.push(50.0F)
  m.data.push(0.0F); m.times.push(100.0F)
  assert_eq(m.data.length(), 3)
  assert_eq(m.times.length(), 3)
  m.clear_records()
  assert_eq(m.data.length(), 0)
  assert_eq(m.times.length(), 0)
  // After clearing, count_spikes should return 0.
  assert_eq(m.count_spikes(), 0)
}
