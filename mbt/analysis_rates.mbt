// analysis_rates.mbt — population-level firing rate helpers (v0.41.3).
//
// Complements `vecplot.mbt::Monitor::firing_rate` (which operates on
// the rising-edge buffer) by providing direct spike-train utilities
// that accept arrays of spike times — useful for offline analysis
// and for cross-validation against Julia.
//
// Conventions:
//   - spike times are in milliseconds (Float)
//   - rates are returned in Hz (spikes per second)
//   - time windows are inclusive on both ends
//
// API:
//   neuron_firing_rate(spike_times, t_start, t_end) → Float (Hz)
//   population_firing_rate(per_neuron_spikes, t_start, t_end) → Float (Hz)
//     (mean over active neurons)
//   population_total_rate(per_neuron_spikes, t_start, t_end) → Float (Hz)
//     (sum over all neurons)
//   firing_rate_dynamics(per_neuron_spikes, window, dt) → Array[Float]
//     (sliding-window mean rate at each timestep)
//   fano_factor(per_neuron_counts) → Float
//     (variance / mean over per-neuron spike counts)

///|
/// Count spikes in `spike_times` that fall within `[t_start, t_end]`.
/// Both bounds are inclusive.
fn count_in_window(spike_times : Array[Float], t_start : Float, t_end : Float) -> Int {
  let mut count = 0
  for i in 0..<spike_times.length() {
    let t = spike_times[i]
    if t >= t_start && t <= t_end {
      count = count + 1
    }
  }
  count
}

///|
/// Firing rate (Hz) of a single neuron over `[t_start, t_end]`.
/// Returns 0.0F if the window duration is zero.
pub fn neuron_firing_rate(
  spike_times : Array[Float],
  t_start : Float,
  t_end : Float,
) -> Float {
  let duration_ms = t_end - t_start
  if duration_ms <= 0.0F {
    return 0.0F
  }
  let count = count_in_window(spike_times, t_start, t_end)
  Float::from_int(count) * 1000.0F / duration_ms
}

///|
/// Mean firing rate (Hz) across a population (one element per neuron).
/// Silent neurons contribute 0 Hz. Window is `[t_start, t_end]`.
pub fn population_firing_rate(
  per_neuron_spikes : Array[Array[Float]],
  t_start : Float,
  t_end : Float,
) -> Float {
  let n_pop = per_neuron_spikes.length()
  if n_pop == 0 {
    return 0.0F
  }
  let mut total = 0.0F
  for i in 0..<n_pop {
    total = total + neuron_firing_rate(per_neuron_spikes[i], t_start, t_end)
  }
  total / Float::from_int(n_pop)
}

///|
/// Total firing rate (Hz) across the population — sum of per-neuron
/// rates. Useful for `population_total_rate / n_neurons == mean`.
pub fn population_total_rate(
  per_neuron_spikes : Array[Array[Float]],
  t_start : Float,
  t_end : Float,
) -> Float {
  let n_pop = per_neuron_spikes.length()
  if n_pop == 0 {
    return 0.0F
  }
  let mut total = 0.0F
  for i in 0..<n_pop {
    let duration_ms = t_end - t_start
    if duration_ms > 0.0F {
      let count = count_in_window(per_neuron_spikes[i], t_start, t_end)
      total = total + Float::from_int(count) * 1000.0F / duration_ms
    }
  }
  total
}

///|
/// Sliding-window mean firing rate (Hz). Returns an array of length
/// `n_steps` where each entry is the mean rate over the window
/// `[i*dt, i*dt + window]`. `window` is in ms.
pub fn firing_rate_dynamics(
  per_neuron_spikes : Array[Array[Float]],
  window : Float,
  dt : Float,
) -> Array[Float] {
  if dt <= 0.0F {
    return []
  }
  // Determine total duration from the last spike across all neurons.
  let mut t_max = 0.0F
  for i in 0..<per_neuron_spikes.length() {
    let spikes = per_neuron_spikes[i]
    let n = spikes.length()
    if n > 0 {
      let last = spikes[n - 1]
      if last > t_max {
        t_max = last
      }
    }
  }
  let n_steps = Float::to_int(t_max / dt + 0.999F)
  let out : Array[Float] = Array::make(n_steps, 0.0F)
  let n_pop = per_neuron_spikes.length()
  for s in 0..<n_steps {
    let t_start_s = Float::from_int(s) * dt
    let t_end_s = t_start_s + window
    let mut sum = 0.0F
    for i in 0..<n_pop {
      let count = count_in_window(per_neuron_spikes[i], t_start_s, t_end_s)
      sum = sum + Float::from_int(count) * 1000.0F / window
    }
    out[s] = sum / Float::from_int(n_pop)
  }
  out
}

///|
/// Fano factor over an array of per-neuron spike counts: var / mean.
/// Returns 0.0F for a population with ≤1 neurons or all-zero counts.
pub fn fano_factor(per_neuron_counts : Array[Int]) -> Float {
  let n = per_neuron_counts.length()
  if n <= 1 {
    return 0.0F
  }
  let n_f = Float::from_int(n)
  let mut sum : Float = 0.0F
  for i in 0..<n {
    sum = sum + Float::from_int(per_neuron_counts[i])
  }
  let mean = sum / n_f
  if mean <= 0.0F {
    return 0.0F
  }
  let mut var_sum : Float = 0.0F
  for i in 0..<n {
    let d = Float::from_int(per_neuron_counts[i]) - mean
    var_sum = var_sum + d * d
  }
  // Sample variance (n - 1 denominator).
  let variance = var_sum / Float::from_int(n - 1)
  variance / mean
}

///|
/// Per-neuron firing rates (Hz) over `[t_start, t_end]`. Returns an
/// array of length n_neurons.
pub fn per_neuron_firing_rates(
  per_neuron_spikes : Array[Array[Float]],
  t_start : Float,
  t_end : Float,
) -> Array[Float] {
  let n_pop = per_neuron_spikes.length()
  let out : Array[Float] = Array::make(n_pop, 0.0F)
  for i in 0..<n_pop {
    out[i] = neuron_firing_rate(per_neuron_spikes[i], t_start, t_end)
  }
  out
}

///|
/// Coefficient of variation (CV) of per-neuron firing rates across
/// the population. CV = std(rates) / mean(rates). Returns 0.0F if
/// mean is zero or population is empty.
pub fn firing_rate_cv(
  per_neuron_spikes : Array[Array[Float]],
  t_start : Float,
  t_end : Float,
) -> Float {
  let n_pop = per_neuron_spikes.length()
  if n_pop == 0 {
    return 0.0F
  }
  let rates = per_neuron_firing_rates(per_neuron_spikes, t_start, t_end)
  let n_f = Float::from_int(n_pop)
  let mut sum : Float = 0.0F
  for i in 0..<n_pop {
    sum = sum + rates[i]
  }
  let mean = sum / n_f
  if mean <= 0.0F {
    return 0.0F
  }
  let mut var_sum : Float = 0.0F
  for i in 0..<n_pop {
    let d = rates[i] - mean
    var_sum = var_sum + d * d
  }
  let variance = var_sum / Float::from_int(n_pop - 1)
  // std = sqrt(variance). Use Double path for sqrt to preserve precision.
  let std = variance.to_double().sqrt().to_float()
  std / mean
}