// analysis_EPSP_pair_test.mbt — tests for epsp_pair and epsp_pair_window.

///|
test "epsp_pair — both exc peak and inh trough in one pass" {
  // Voltage rises to -55 (exc) at t=4 then dips to -75 (inh) at t=8.
  let v : Array[Float] = [
    -65.0F, -63.0F, -61.0F, -58.0F, -55.0F,
    -58.0F, -65.0F, -72.0F, -75.0F, -70.0F,
    -65.0F,
  ]
  let (exc, inh) = epsp_pair(v, 1, 0, -65.0F, 0)
  if (exc - 10.0F).abs() > 1.0e-6 { fail("exc expected 10.0, got \{exc}") }
  if (inh - (-10.0F)).abs() > 1.0e-6 { fail("inh expected -10.0, got \{inh}") }
}

///|
test "epsp_pair — monotonic voltage returns (0, 0)" {
  // Monotonic rise (no fall below starting value).
  let v : Array[Float] = [-65.0F, -64.0F, -63.0F, -62.0F, -61.0F, -60.0F]
  let (exc, inh) = epsp_pair(v, 1, 0, -65.0F, 0)
  // exc = -60 - (-65) = 5; inh = -65 - (-65) = 0 (no trough below rest).
  if (exc - 5.0F).abs() > 1.0e-6 { fail("exc expected 5.0") }
  if inh != 0.0F { fail("inh expected 0, got \{inh}") }
}

///|
test "epsp_pair — single-pass matches individual exc_peak + inh_trough" {
  let v : Array[Float] = [-65.0F, -60.0F, -55.0F, -70.0F, -65.0F]
  let (exc_p, inh_p) = epsp_pair(v, 1, 0, -65.0F, 0)
  let exc_only = exc_peak(v, 1, 0, -65.0F, 0)
  let inh_only = inh_trough(v, 1, 0, -65.0F, 0)
  if (exc_p - exc_only).abs() > 1.0e-6 { fail("exc mismatch") }
  if (inh_p - inh_only).abs() > 1.0e-6 { fail("inh mismatch") }
}

///|
test "epsp_pair_window — bounded window" {
  let v : Array[Float] = [
    -65.0F, -63.0F, -61.0F, -58.0F, -55.0F,
    -58.0F, -65.0F, -72.0F, -75.0F, -70.0F,
    -65.0F,
  ]
  // Window [0, 5] excludes the trough at t=8.
  let (exc, inh) = epsp_pair_window(v, 1, 0, 5, -65.0F, 0)
  if (exc - 10.0F).abs() > 1.0e-6 { fail("exc expected 10") }
  // inh within [0,5] = -65 - (-65) = 0 (no dip below rest).
  if inh != 0.0F { fail("inh in window [0,5] should be 0, got \{inh}") }
  // Window [6, 9] excludes the peak at t=4.
  let (exc2, inh2) = epsp_pair_window(v, 1, 6, 9, -65.0F, 0)
  // exc in [6,9]: max is -65 (the only "rise" was earlier) → 0.
  if exc2 != 0.0F { fail("exc in window [6,9] should be 0, got \{exc2}") }
  if (inh2 - (-10.0F)).abs() > 1.0e-6 {
    fail("inh in window [6,9] should be -10, got \{inh2}")
  }
}

///|
test "epsp_pair_window — empty/inverted window returns (0, 0)" {
  let v : Array[Float] = [-65.0F, -63.0F, -60.0F, -55.0F]
  let (exc, inh) = epsp_pair_window(v, 1, 5, 3, -65.0F, 0)
  if exc != 0.0F || inh != 0.0F {
    fail("empty window should return (0, 0)")
  }
  let (exc2, inh2) = epsp_pair_window(v, 1, -1, 3, -65.0F, 0)
  if exc2 != 0.0F || inh2 != 0.0F {
    fail("negative start should return (0, 0)")
  }
}