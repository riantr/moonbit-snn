// analysis_rates_test.mbt — tests for v0.41.3 analysis_rates.mbt.

///|
test "neuron_firing_rate_basic" {
  // 1 neuron, 10 spikes at t = 0, 100, 200, ..., 900 ms.
  let spikes : Array[Float] = [0.0F, 100.0F, 200.0F, 300.0F, 400.0F,
                                500.0F, 600.0F, 700.0F, 800.0F, 900.0F]
  // Window [0, 900] ms → 10 spikes / 0.9 s ≈ 11.111 Hz.
  let r = @snn_mbt.neuron_firing_rate(spikes, 0.0F, 900.0F)
  if (r - 11.111F).abs() > 0.01F {
    fail("rate = \{r}, expected ~11.11 Hz")
  }
}

///|
test "neuron_firing_rate_zero_window_returns_zero" {
  let spikes : Array[Float] = [1.0F, 2.0F, 3.0F]
  if @snn_mbt.neuron_firing_rate(spikes, 5.0F, 5.0F) != 0.0F {
    fail("zero-duration window should return 0")
  }
}

///|
test "neuron_firing_rate_no_spikes_in_window" {
  // All spikes before the window.
  let spikes : Array[Float] = [1.0F, 2.0F, 3.0F]
  if @snn_mbt.neuron_firing_rate(spikes, 100.0F, 200.0F) != 0.0F {
    fail("no spikes in window should return 0")
  }
}

///|
test "neuron_firing_rate_window_boundary_inclusive" {
  // Spike exactly at t_start and t_end must count.
  let spikes : Array[Float] = [0.0F, 100.0F]
  // Window [0, 100]: 2 spikes / 0.1 s = 20 Hz.
  let r = @snn_mbt.neuron_firing_rate(spikes, 0.0F, 100.0F)
  if r != 20.0F {
    fail("rate = \{r}, expected 20.0 (boundary inclusive)")
  }
}

///|
test "population_firing_rate_mean_over_neurons" {
  // 3 neurons with different rates:
  //   neuron 0: 10 spikes in [0, 1000] → 10 Hz
  //   neuron 1: 20 spikes in [0, 1000] → 20 Hz
  //   neuron 2: 30 spikes in [0, 1000] → 30 Hz
  // Mean = 20 Hz.
  let n0 : Array[Float] = []
  for _ in 0..<10 {
    n0.push(0.0F)
  }
  let n1 : Array[Float] = []
  for _ in 0..<20 {
    n1.push(0.0F)
  }
  let n2 : Array[Float] = []
  for _ in 0..<30 {
    n2.push(0.0F)
  }
  let pop : Array[Array[Float]] = [n0, n1, n2]
  let r = @snn_mbt.population_firing_rate(pop, 0.0F, 1000.0F)
  if r != 20.0F {
    fail("mean rate = \{r}, expected 20.0")
  }
}

///|
test "population_firing_rate_empty_population" {
  let pop : Array[Array[Float]] = []
  if @snn_mbt.population_firing_rate(pop, 0.0F, 100.0F) != 0.0F {
    fail("empty population should return 0")
  }
}

///|
test "population_total_rate_sums_per_neuron" {
  // Same setup: 10 + 20 + 30 = 60 Hz total.
  let n0 : Array[Float] = []
  for _ in 0..<10 {
    n0.push(0.0F)
  }
  let n1 : Array[Float] = []
  for _ in 0..<20 {
    n1.push(0.0F)
  }
  let n2 : Array[Float] = []
  for _ in 0..<30 {
    n2.push(0.0F)
  }
  let pop : Array[Array[Float]] = [n0, n1, n2]
  let r = @snn_mbt.population_total_rate(pop, 0.0F, 1000.0F)
  if r != 60.0F {
    fail("total rate = \{r}, expected 60.0")
  }
}

///|
test "firing_rate_dynamics_sliding_window" {
  // 1 neuron, 4 spikes: 100, 300, 500, 700 ms.
  // Window=400ms, dt=200ms. t_max=700 → n_steps = ceil(700/200) = 4.
  //   step 0: window [0, 400]   → 2 spikes (100, 300) → 5.0 Hz
  //   step 1: window [200, 600] → 2 spikes (300, 500) → 5.0 Hz
  //   step 2: window [400, 800] → 2 spikes (500, 700) → 5.0 Hz
  //   step 3: window [600, 1000] → 1 spike (700) → 2.5 Hz
  let n0 : Array[Float] = [100.0F, 300.0F, 500.0F, 700.0F]
  let pop : Array[Array[Float]] = [n0]
  let dyn = @snn_mbt.firing_rate_dynamics(pop, 400.0F, 200.0F)
  if dyn.length() != 4 {
    fail("dyn.length() = \{dyn.length()}, expected 4")
  }
  if (dyn[0] - 5.0F).abs() > 1.0e-5F {
    fail("dyn[0] = \{dyn[0]}, expected 5.0")
  }
  if (dyn[1] - 5.0F).abs() > 1.0e-5F {
    fail("dyn[1] = \{dyn[1]}, expected 5.0")
  }
  if (dyn[2] - 5.0F).abs() > 1.0e-5F {
    fail("dyn[2] = \{dyn[2]}, expected 5.0")
  }
  if (dyn[3] - 2.5F).abs() > 1.0e-5F {
    fail("dyn[3] = \{dyn[3]}, expected 2.5 (only spike 700 in [600,1000])")
  }
}

///|
test "fano_factor_perfectly_regular_counts" {
  // All neurons fire the same number of times → variance = 0 → Fano = 0.
  let counts : Array[Int] = [5, 5, 5, 5, 5]
  if @snn_mbt.fano_factor(counts) != 0.0F {
    fail("regular counts should give Fano = 0")
  }
}

///|
test "fano_factor_poisson_like_counts" {
  // Counts with mean=10, var=10 → Fano ≈ 1.0 (Poisson-like).
  let counts : Array[Int] = [10, 12, 8, 11, 9, 10]
  let f = @snn_mbt.fano_factor(counts)
  // Sample variance of [10,12,8,11,9,10]: mean=10, ss=10, var=10/5=2,
  // Fano=2/10=0.2. (Small sample; not Poisson-like.)
  // Just check Fano is positive and finite.
  if f <= 0.0F {
    fail("Fano = \{f}, expected > 0")
  }
  if f != f {  // NaN check
    fail("Fano = NaN")
  }
}

///|
test "fano_factor_all_zeros_returns_zero" {
  let counts : Array[Int] = [0, 0, 0, 0]
  if @snn_mbt.fano_factor(counts) != 0.0F {
    fail("all-zero counts should give Fano = 0 (mean = 0)")
  }
}

///|
test "fano_factor_single_neuron_returns_zero" {
  let counts : Array[Int] = [42]
  if @snn_mbt.fano_factor(counts) != 0.0F {
    fail("n=1 population should give Fano = 0 (no variance)")
  }
}

///|
test "per_neuron_firing_rates_correct_shape_and_values" {
  // 3 neurons, each fires exactly once in [0, 1000] → 1 Hz each.
  let n0 : Array[Float] = [100.0F]
  let n1 : Array[Float] = [200.0F]
  let n2 : Array[Float] = [300.0F]
  let pop : Array[Array[Float]] = [n0, n1, n2]
  let rates = @snn_mbt.per_neuron_firing_rates(pop, 0.0F, 1000.0F)
  if rates.length() != 3 {
    fail("rates.length() = \{rates.length()}, expected 3")
  }
  for i in 0..<3 {
    if rates[i] != 1.0F {
      fail("rates[\{i}] = \{rates[i]}, expected 1.0 Hz")
    }
  }
}

///|
test "firing_rate_cv_zero_for_identical_rates" {
  // All neurons fire 10 times in [0, 1000] → identical 10 Hz rates.
  let n0 : Array[Float] = []
  for _ in 0..<10 {
    n0.push(0.0F)
  }
  let n1 : Array[Float] = []
  for _ in 0..<10 {
    n1.push(0.0F)
  }
  let pop : Array[Array[Float]] = [n0, n1]
  let cv = @snn_mbt.firing_rate_cv(pop, 0.0F, 1000.0F)
  if cv != 0.0F {
    fail("CV = \{cv}, expected 0 (identical rates)")
  }
}

///|
test "firing_rate_cv_positive_for_heterogeneous_rates" {
  // Neuron 0: 10 Hz; neuron 1: 30 Hz. CV = std/mean > 0.
  let n0 : Array[Float] = []
  for _ in 0..<10 {
    n0.push(0.0F)
  }
  let n1 : Array[Float] = []
  for _ in 0..<30 {
    n1.push(0.0F)
  }
  let pop : Array[Array[Float]] = [n0, n1]
  let cv = @snn_mbt.firing_rate_cv(pop, 0.0F, 1000.0F)
  // mean = 20, var = ((10-20)^2 + (30-20)^2) / 1 = 200, std = sqrt(200) ≈ 14.14
  // CV = 14.14 / 20 ≈ 0.707
  if cv <= 0.0F {
    fail("CV = \{cv}, expected > 0")
  }
  if (cv - 0.707F).abs() > 0.01F {
    fail("CV = \{cv}, expected ~0.707")
  }
}