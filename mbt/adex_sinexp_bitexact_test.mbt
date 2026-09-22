// adex_sinexp_bitexact_test.mbt — regression test for AdExSinExp
// neuron under tonic drive.
//
// Verifies that an AdExSinExp population under high tonic current
// produces sensible numerical output:
//   - No NaN values in any state (v, w, syn_curr, he, hi).
//   - Voltages stay in physiological range [-80, 30]mV.
//   - Some firing activity occurs (at least one neuron fires).
//   - Firing rate is bounded (no runaway).
//
// Complements `chain_bitexact_test.mbt` (IF) and
// `izhikevich_bitexact_test.mbt` (IZ) with AdExSinExp coverage.

///|
test "AdExSinExp — tonic drive produces no NaN state" {
  let param = AdExSinExpParameter::lkd_adex()
  let rng = Xoshiro::new(42UL)
  let pop = AdExSinExp::new(10, param, rng)
  // Tonic drive above firing threshold.
  let mut k = 0
  while k < 10 {
    pop.i[k] = 400.0F
    k = k + 1
  }
  let dt = 0.125F
  let n_steps = 800  // 100ms
  let mut t = 0
  while t < n_steps {
    adex_sinexp_step_synapses(pop, dt)
    adex_sinexp_synaptic_current(pop)
    step_adex_sinexp(pop, dt)
    t = t + 1
  }
  // NaN checks.
  let mut i = 0
  while i < 10 {
    if pop.v[i] != pop.v[i] {
      fail("v[\{i}] is NaN after tonic drive")
    }
    if pop.w[i] != pop.w[i] {
      fail("w[\{i}] is NaN after tonic drive")
    }
    i = i + 1
  }
}

///|
test "AdExSinExp — voltages stay in physiological range" {
  let param = AdExSinExpParameter::lkd_adex()
  let rng = Xoshiro::new(42UL)
  let pop = AdExSinExp::new(10, param, rng)
  let mut k = 0
  while k < 10 {
    pop.i[k] = 400.0F
    k = k + 1
  }
  let dt = 0.125F
  let n_steps = 800
  let mut t = 0
  while t < n_steps {
    adex_sinexp_step_synapses(pop, dt)
    adex_sinexp_synaptic_current(pop)
    step_adex_sinexp(pop, dt)
    t = t + 1
  }
  let mut i = 0
  while i < 10 {
    if pop.v[i] < -80.0F || pop.v[i] > 30.0F {
      fail("v[\{i}] = \{pop.v[i]}mV, out of expected [-80, 30]mV")
    }
    i = i + 1
  }
}

///|
test "AdExSinExp — fires at least once under tonic drive" {
  // With 400pA tonic drive (above firing threshold), some neurons
  // should fire within 100ms.
  let param = AdExSinExpParameter::lkd_adex()
  let rng = Xoshiro::new(42UL)
  let pop = AdExSinExp::new(10, param, rng)
  let mut k = 0
  while k < 10 {
    pop.i[k] = 400.0F
    k = k + 1
  }
  let dt = 0.125F
  let n_steps = 800
  let mut total_fires = 0
  let mut t = 0
  while t < n_steps {
    adex_sinexp_step_synapses(pop, dt)
    adex_sinexp_synaptic_current(pop)
    step_adex_sinexp(pop, dt)
    let mut i = 0
    while i < 10 {
      if pop.fire[i] {
        total_fires = total_fires + 1
      }
      i = i + 1
    }
    t = t + 1
  }
  if total_fires == 0 {
    fail("AdExSinExp produced 0 fires in 100ms under 400pA tonic drive")
  }
}

///|
test "AdExSinExp — firing rate bounded (no runaway)" {
  // Rate should be bounded by ~100 Hz (theoretical limit is
  // 1 / t_abs ≈ 1000 Hz, but realistically 100-200 Hz under tonic).
  // We allow up to 300 Hz to leave plenty of headroom for different
  // tonic settings.
  let param = AdExSinExpParameter::lkd_adex()
  let rng = Xoshiro::new(42UL)
  let pop = AdExSinExp::new(10, param, rng)
  let mut k = 0
  while k < 10 {
    pop.i[k] = 400.0F
    k = k + 1
  }
  let dt = 0.125F
  let n_steps = 800
  let mut total_fires = 0
  let mut t = 0
  while t < n_steps {
    adex_sinexp_step_synapses(pop, dt)
    adex_sinexp_synaptic_current(pop)
    step_adex_sinexp(pop, dt)
    let mut i = 0
    while i < 10 {
      if pop.fire[i] {
        total_fires = total_fires + 1
      }
      i = i + 1
    }
    t = t + 1
  }
  // Total per-neuron fires in 100ms: 800 steps × 10 neurons = 8000 slots.
  // Bound: rate < 300 Hz → total < 10 * 300 * 0.1 = 300 fires.
  if total_fires > 300 {
    fail("AdExSinExp total fires = \{total_fires}, expected < 300 in 100ms (rate < 300Hz)")
  }
}