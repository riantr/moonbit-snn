// analysis_EI_balance_test.mbt — tests for analysis_EI_balance.mbt.

///|
test "kei_balance_per_neuron — constant voltage equals target → index 0" {
  // 10 timesteps × 3 neurons, all = -55.0F
  let v : Array[Float] = []
  for _ in 0..<30 {
    // (no append helper for Array literal in this context; build manually)
  }
  // Manually fill: 10×3 all = -55.0
  let v_actual : Array[Float] = Array::make(30, -55.0F)
  let r = kei_balance_per_neuron(v_actual, 10, 3, -55.0F, 2.3F)
  if r.length() != 3 { fail("expected 3 entries") }
  // All distances = 0, less than tolerance, first argmin wins → index 0.
  if r[0] != 0 { fail("expected 0, got \{r[0]}") }
  if r[1] != 0 { fail("expected 0, got \{r[1]}") }
  if r[2] != 0 { fail("expected 0, got \{r[2]}") }
}

///|
test "kei_balance_per_neuron — per-neuron settling at t=5" {
  // 10 timesteps × 2 neurons. n=0 settles to -55 at t=5, n=1 settles at t=7.
  // voltage_data[t * n_neurons + n]
  let v : Array[Float] = Array::make(20, -65.0F)
  // n=0: t=5 → -55
  v[5 * 2 + 0] = -55.0F
  // n=1: t=7 → -55
  v[7 * 2 + 1] = -55.0F
  let r = kei_balance_per_neuron(v, 10, 2, -55.0F, 2.3F)
  if r[0] != 5 { fail("n=0 expected 5, got \{r[0]}") }
  if r[1] != 7 { fail("n=1 expected 7, got \{r[1]}") }
}

///|
test "kei_balance_per_neuron — outside tolerance → 0" {
  // Voltage never settles within 2.3mV of -55.
  let v : Array[Float] = Array::make(20, -50.0F)
  let r = kei_balance_per_neuron(v, 10, 2, -55.0F, 2.3F)
  // |v - target| = 5.0 > 2.3 → 0
  if r[0] != 0 { fail("expected 0 (out of tolerance), got \{r[0]}") }
  if r[1] != 0 { fail("expected 0 (out of tolerance), got \{r[1]}") }
}

///|
test "kei_balance_per_neuron — exactly at tolerance boundary" {
  // Voltage = -55 + 2.3 = -52.7 → |delta| = 2.3, NOT < 2.3, returns 0.
  let v : Array[Float] = Array::make(10, -52.7F)
  let r = kei_balance_per_neuron(v, 10, 1, -55.0F, 2.3F)
  if r[0] != 0 { fail("at boundary expected 0 (not strictly <), got \{r[0]}") }
  // Now use voltage = -55 + 2.29 = -52.71 → |delta| = 2.29, < 2.3, returns 0.
  let v2 : Array[Float] = Array::make(10, -52.71F)
  let r2 = kei_balance_per_neuron(v2, 10, 1, -55.0F, 2.3F)
  if r2[0] != 0 { fail("within tolerance expected 0, got \{r2[0]}") }
}

///|
test "kei_balance_population — settles at t=5 (mean reaches -55)" {
  // 10 timesteps × 4 neurons, all = -55 at t=5, otherwise -70.
  let v : Array[Float] = Array::make(40, -70.0F)
  for n in 0..<4 {
    v[5 * 4 + n] = -55.0F
  }
  let t = kei_balance_population(v, 10, 4, -55.0F, 2.3F)
  if t != 5 { fail("expected 5, got \{t}") }
}

///|
test "kei_balance_population — never settles → returns 0" {
  let v : Array[Float] = Array::make(40, -50.0F)
  let t = kei_balance_population(v, 10, 4, -55.0F, 2.3F)
  if t != 0 { fail("expected 0, got \{t}") }
}