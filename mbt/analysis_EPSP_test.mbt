// analysis_EPSP_test.mbt — tests for analysis_EPSP.mbt.

///|
test "exc_peak — single-compartment voltage rises then falls" {
  // Single compartment (n_compartments=1). 10 timesteps. Voltage rises
  // from -65 to -55 (peak at t=4) then back to -65.
  let v : Array[Float] = [
    -65.0F, -63.0F, -61.0F, -58.0F, -55.0F,
    -57.0F, -60.0F, -62.0F, -64.0F, -65.0F,
  ]
  let peak = exc_peak(v, 1, 0, -65.0F, 0)
  if (peak - 10.0F).abs() > 1.0e-6 {
    fail("exc_peak expected 10.0, got \{peak}")
  }
}

///|
test "exc_peak — spiketime offset skips leading samples" {
  // Voltage rises from -65 (t=0) to -55 (t=5). spiketime=3 should still
  // include t=5 (the peak) since "spiketime:end" is inclusive.
  let v : Array[Float] = [
    -65.0F, -64.0F, -63.0F, -62.0F, -60.0F,
    -55.0F, -58.0F, -60.0F, -62.0F, -65.0F,
  ]
  let peak = exc_peak(v, 1, 3, -65.0F, 0)
  // Peak is -55 at t=5. peak - rest = -55 - (-65) = 10.0.
  if (peak - 10.0F).abs() > 1.0e-6 {
    fail("expected 10.0, got \{peak}")
  }
}

///|
test "exc_peak — rest offset subtracted from peak" {
  // Same trace as above but rest=-70 → peak - rest = -55 - (-70) = 15.0.
  let v : Array[Float] = [-65.0F, -60.0F, -55.0F, -60.0F, -65.0F]
  let peak = exc_peak(v, 1, 0, -70.0F, 0)
  if (peak - 15.0F).abs() > 1.0e-6 {
    fail("expected 15.0, got \{peak}")
  }
}

///|
test "inh_trough — inhibitory dip below rest" {
  // Voltage dips from -65 to -75 at t=3 then back to -65.
  let v : Array[Float] = [
    -65.0F, -68.0F, -72.0F, -75.0F, -72.0F,
    -68.0F, -65.0F, -65.0F, -65.0F, -65.0F,
  ]
  let trough = inh_trough(v, 1, 0, -65.0F, 0)
  // trough = -75 - (-65) = -10.
  if (trough - (-10.0F)).abs() > 1.0e-6 {
    fail("expected -10.0, got \{trough}")
  }
}

///|
test "exc_peak — multi-compartment selects the right column" {
  // 10 timesteps × 2 compartments (soma, dend).
  // Soma (col 0): flat at -65.
  // Dend (col 1): rises to -55 at t=4.
  let v : Array[Float] = [
    -65.0F, -65.0F,  // t=0
    -65.0F, -63.0F,
    -65.0F, -61.0F,
    -65.0F, -58.0F,
    -65.0F, -55.0F,  // t=4 (peak in col 1)
    -65.0F, -57.0F,
    -65.0F, -60.0F,
    -65.0F, -65.0F,
    -65.0F, -65.0F,
    -65.0F, -65.0F,
  ]
  // Pick soma (compartment=0): no rise → peak - rest = 0.
  let peak_soma = exc_peak(v, 2, 0, -65.0F, 0)
  if peak_soma != 0.0F { fail("soma peak should be 0, got \{peak_soma}") }
  // Pick dend (compartment=1): rise → peak - rest = 10.
  let peak_dend = exc_peak(v, 2, 0, -65.0F, 1)
  if (peak_dend - 10.0F).abs() > 1.0e-6 {
    fail("dend peak should be 10, got \{peak_dend}")
  }
}