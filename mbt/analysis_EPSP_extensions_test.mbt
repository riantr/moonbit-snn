// analysis_EPSP_extensions_test.mbt — tests for exc_peak_with_time,
// inh_trough_with_time, exc_peak_window, inh_trough_window.

///|
test "exc_peak_with_time — peak value and time index" {
  // Single compartment. Peak at t=4 (-55).
  let v : Array[Float] = [
    -65.0F, -63.0F, -61.0F, -58.0F, -55.0F,
    -57.0F, -60.0F, -62.0F, -64.0F, -65.0F,
  ]
  let (peak, time) = exc_peak_with_time(v, 1, 0, -65.0F, 0)
  if (peak - 10.0F).abs() > 1.0e-6 { fail("peak expected 10.0, got \{peak}") }
  if time != 4 { fail("time_of_peak expected 4, got \{time}") }
}

///|
test "exc_peak_with_time — plateau (first occurrence wins)" {
  // Two equal peaks at t=3 and t=6.
  let v : Array[Float] = [-65.0F, -62.0F, -58.0F, -55.0F, -60.0F, -58.0F, -55.0F, -62.0F]
  let (peak, time) = exc_peak_with_time(v, 1, 0, -65.0F, 0)
  if (peak - 10.0F).abs() > 1.0e-6 { fail("peak expected 10.0") }
  if time != 3 { fail("expected first peak at t=3, got \{time}") }
}

///|
test "inh_trough_with_time — trough and time" {
  // Trough at t=3 (-75).
  let v : Array[Float] = [-65.0F, -68.0F, -72.0F, -75.0F, -72.0F, -68.0F, -65.0F]
  let (trough, time) = inh_trough_with_time(v, 1, 0, -65.0F, 0)
  if (trough - (-10.0F)).abs() > 1.0e-6 {
    fail("trough expected -10.0, got \{trough}")
  }
  if time != 3 { fail("time expected 3, got \{time}") }
}

///|
test "exc_peak_window — peak within [t_start, t_end]" {
  // 10-timestep trace; window [2, 6] should pick the local peak at t=5.
  let v : Array[Float] = [
    -65.0F, -63.0F, -62.0F, -60.0F, -57.0F,
    -54.0F, -56.0F, -58.0F, -60.0F, -62.0F,
  ]
  // Without window: peak at t=5 (-54) → peak - rest = 11.
  let full = exc_peak(v, 1, 0, -65.0F, 0)
  if (full - 11.0F).abs() > 1.0e-6 { fail("full peak expected 11") }
  // With window [2, 6]: still the same peak (t=5 is in window).
  let w = exc_peak_window(v, 1, 2, 6, -65.0F, 0)
  if (w - 11.0F).abs() > 1.0e-6 {
    fail("windowed peak expected 11, got \{w}")
  }
  // With window [6, 9] (excluding peak at t=5): max in window is at t=6 (-56) → 9.
  let w2 = exc_peak_window(v, 1, 6, 9, -65.0F, 0)
  if (w2 - 9.0F).abs() > 1.0e-6 {
    fail("window [6,9] peak expected 9, got \{w2}")
  }
}

///|
test "exc_peak_window — empty/inverted window returns 0" {
  let v : Array[Float] = [-65.0F, -63.0F, -60.0F, -55.0F]
  if exc_peak_window(v, 1, 5, 3, -65.0F, 0) != 0.0F {
    fail("inverted window should return 0")
  }
  if exc_peak_window(v, 1, -1, 3, -65.0F, 0) != 0.0F {
    fail("negative start should return 0")
  }
}

///|
test "inh_trough_window — trough within [t_start, t_end]" {
  // Trough at t=3 (-75). Window [0, 5] picks it up.
  let v : Array[Float] = [-65.0F, -68.0F, -72.0F, -75.0F, -72.0F, -68.0F, -65.0F]
  let w = inh_trough_window(v, 1, 0, 5, -65.0F, 0)
  if (w - (-10.0F)).abs() > 1.0e-6 {
    fail("window [0,5] trough expected -10, got \{w}")
  }
  // Window [4, 6] excludes trough: min in window is -72 → -7.
  let w2 = inh_trough_window(v, 1, 4, 6, -65.0F, 0)
  if (w2 - (-7.0F)).abs() > 1.0e-6 {
    fail("window [4,6] trough expected -7, got \{w2}")
  }
}