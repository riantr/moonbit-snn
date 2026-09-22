// analysis_EI_balance.mbt — Kei balance measurement.
//
// Port of `refs/SNNUtils.jl/src/analysis/EI_balance.jl`:
//   - `kei_balance(kie_measure, target_rate, Nd)` — find the time index
//     where the (mean) post-synaptic membrane voltage first settles
//     within `2.3mV` of a target rate.
//
// The Julia API has TWO modes:
//   - `kei_measure::NamedTuple` with fields `(mins, νs, kie_test, voltage_data?)`
//   - `kei_measure::Vector{...}` of multiple NamedTuples
//
// We expose a simpler function-level port that takes a 2D voltage
// matrix `[T × N]` (stored as a flat Array[Float] of length T*N, row-major
// over time × neuron), a target voltage, and returns the index per
// neuron. The result is an `Array[Int]` of length N — the time index
// where `voltage_data[t, n]` is closest to `target` AND within 2.3 mV.
// If no such t exists, the entry is set to 0 (Julia sets it to 1).

///|
/// Index the entry nearest to `target` in a single 1D Float array.
/// Linear scan. Returns the 0-based index of the closest entry.
/// Ties: returns the first occurrence.
fn argmin_1d(arr : Array[Float], target : Float) -> Int {
  let n = arr.length()
  if n == 0 {
    return 0
  }
  let mut best_idx : Int = 0
  let mut best_dist = (arr[0] - target).abs()
  let mut i : Int = 1
  while i < n {
    let d = (arr[i] - target).abs()
    if d < best_dist {
      best_dist = d
      best_idx = i
    }
    i = i + 1
  }
  best_idx
}

///|
/// Find the time index per neuron where `voltage_data[t, n]` is closest
/// to `target_rate`. If the closest value is within `tolerance` (default
/// 2.3 mV), return that index; otherwise return 0.
///
/// `voltage_data` is a flat row-major `Array[Float]` of shape
/// `[n_steps × n_neurons]` — index via `voltage_data[t * n_neurons + n]`.
/// `target_rate` is in mV (e.g. -55.0F).
/// Result: `Array[Int]` of length `n_neurons`.
pub fn kei_balance_per_neuron(
  voltage_data : Array[Float],
  n_steps : Int,
  n_neurons : Int,
  target_rate : Float,
  tolerance : Float,
) -> Array[Int] {
  let result : Array[Int] = Array::make(n_neurons, 0)
  // For each neuron n, slice `voltage_data[:, n]` as a 1D array and find argmin.
  // Build per-neuron 1D slice on the fly (no allocation needed).
  for n in 0..<n_neurons {
    // Walk t ∈ [0, n_steps) and find the t with |voltage_data[t, n] - target| minimal.
    let mut best_t : Int = 0
    let mut best_dist = (voltage_data[0 * n_neurons + n] - target_rate).abs()
    for t in 1..<n_steps {
      let d = (voltage_data[t * n_neurons + n] - target_rate).abs()
      if d < best_dist {
        best_dist = d
        best_t = t
      }
    }
    // Tolerance check: Julia uses `abs(voltage_data[m, n] - target_rate) < 2.3mV`.
    if best_dist < tolerance {
      result[n] = best_t
    }
    // else: stays 0
  }
  result
}

///|
/// Find a single global time index where the per-neuron MEAN voltage is
/// closest to `target_rate`. Useful when looking at the population-average
/// settling time.
pub fn kei_balance_population(
  voltage_data : Array[Float],
  n_steps : Int,
  n_neurons : Int,
  target_rate : Float,
  tolerance : Float,
) -> Int {
  // Compute mean at each t.
  let mean_v : Array[Float] = Array::make(n_steps, 0.0F)
  for t in 0..<n_steps {
    let mut s : Float = 0.0F
    for n in 0..<n_neurons {
      s = s + voltage_data[t * n_neurons + n]
    }
    mean_v[t] = s / Float::from_int(n_neurons)
  }
  let best_t = argmin_1d(mean_v, target_rate)
  let d = (mean_v[best_t] - target_rate).abs()
  if d < tolerance {
    best_t
  } else {
    0
  }
}