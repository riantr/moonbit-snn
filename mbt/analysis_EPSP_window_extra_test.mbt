// analysis_EPSP_window_extra_test.mbt — additional tests for the
// windowed EPSP helpers (multi-compartment + boundary cases).

///|
test "exc_peak_window — multi-compartment picks correct column" {
  // 10 steps × 2 compartments. Soma (col 0): flat. Dend (col 1): peak at t=4.
  let v : Array[Float] = [
    -65.0F, -65.0F,
    -65.0F, -63.0F,
    -65.0F, -61.0F,
    -65.0F, -58.0F,
    -65.0F, -55.0F,
    -65.0F, -57.0F,
    -65.0F, -60.0F,
    -65.0F, -62.0F,
    -65.0F, -64.0F,
    -65.0F, -65.0F,
  ]
  // Window [2, 5] on the dend column: peak at t=4 is in the window.
  let peak = exc_peak_window(v, 2, 2, 5, -65.0F, 1)
  if (peak - 10.0F).abs() > 1.0e-6 { fail("dend peak expected 10") }
  // Soma column stays flat → peak - rest = 0.
  let peak_soma = exc_peak_window(v, 2, 0, 9, -65.0F, 0)
  if peak_soma != 0.0F { fail("soma peak should be 0") }
}

///|
test "inh_trough_window — multi-compartment picks correct column" {
  // 10 steps × 2 compartments. Soma (col 0): flat. Dend (col 1): dip at t=3.
  let v : Array[Float] = [
    -65.0F, -65.0F,
    -65.0F, -68.0F,
    -65.0F, -72.0F,
    -65.0F, -75.0F,
    -65.0F, -72.0F,
    -65.0F, -68.0F,
    -65.0F, -65.0F,
    -65.0F, -65.0F,
    -65.0F, -65.0F,
    -65.0F, -65.0F,
  ]
  // Window [0, 6] picks up dip at t=3 → trough = -75 - (-65) = -10.
  let trough = inh_trough_window(v, 2, 0, 6, -65.0F, 1)
  if (trough - (-10.0F)).abs() > 1.0e-6 {
    fail("dend trough expected -10, got \{trough}")
  }
  // Window [4, 9] excludes dip → min in window is -72 → -7.
  let trough2 = inh_trough_window(v, 2, 4, 9, -65.0F, 1)
  if (trough2 - (-7.0F)).abs() > 1.0e-6 {
    fail("window [4,9] trough expected -7, got \{trough2}")
  }
}

///|
test "exc_peak_with_time — boundary case (peak at spiketime)" {
  // If the first sample (t=spiketime) is already the max, time_of_peak
  // is spiketime.
  let v : Array[Float] = [-50.0F, -55.0F, -60.0F, -65.0F]
  let (_, time) = exc_peak_with_time(v, 1, 0, -65.0F, 0)
  if time != 0 { fail("time expected 0 (peak at spiketime), got \{time}") }
}

///|
test "exc_peak_with_time — multi-compartment" {
  // 6 steps × 3 compartments (soma, dend1, dend2). Peak in dend2 only.
  let v : Array[Float] = [
    -65.0F, -65.0F, -65.0F,
    -65.0F, -65.0F, -62.0F,
    -65.0F, -65.0F, -60.0F,
    -65.0F, -65.0F, -57.0F,  // peak in col 2 at t=3
    -65.0F, -65.0F, -60.0F,
    -65.0F, -65.0F, -65.0F,
  ]
  let (peak, time) = exc_peak_with_time(v, 3, 0, -65.0F, 2)
  if (peak - 8.0F).abs() > 1.0e-6 { fail("peak expected 8, got \{peak}") }
  if time != 3 { fail("time expected 3, got \{time}") }
  // Verify other columns are 0.
  let (peak0, _) = exc_peak_with_time(v, 3, 0, -65.0F, 0)
  if peak0 != 0.0F { fail("soma peak should be 0, got \{peak0}") }
}