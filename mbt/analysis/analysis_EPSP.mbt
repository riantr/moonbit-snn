// analysis_EPSP.mbt — voltage peak finder for post-synaptic responses.
//
// Port of `get_EPSP` from `refs/SNNUtils.jl/src/analysis/protocols.jl`:
//   `get_EPSP(v; spiketime, rest, inh, compartment)` — returns the peak
//   (exc) or trough (inh) deviation from `rest` in the voltage trace
//   starting from `spiketime`. Used to quantify the strength of a
//   post-synaptic response.
//
// MoonBit port: takes a flat row-major `Array[Float]` voltage trace of
// shape `[n_steps × n_compartments]` and a `spiketime` 0-based index.

///|
/// `exc_peak(v, n_compartments, spiketime, rest, compartment)` — returns
/// `max(v[compartment, spiketime:end]) - rest`. Mirrors Julia's
/// `get_EPSP(...; inh=false)` for the excitatory case.
pub fn exc_peak(
  v : Array[Float],
  n_compartments : Int,
  spiketime : Int,
  rest : Float,
  compartment : Int,
) -> Float {
  // Walk `v[compartment + t * n_compartments]` for t ≥ spiketime.
  let mut best : Float = v[compartment + spiketime * n_compartments]
  let n_steps = v.length() / n_compartments
  let mut t = spiketime + 1
  while t < n_steps {
    let val = v[compartment + t * n_compartments]
    if val > best {
      best = val
    }
    t = t + 1
  }
  best - rest
}

///|
/// `inh_trough(v, n_compartments, spiketime, rest, compartment)` —
/// returns `min(v[compartment, spiketime:end]) - rest`. Mirrors Julia's
/// `get_EPSP(...; inh=true)`.
pub fn inh_trough(
  v : Array[Float],
  n_compartments : Int,
  spiketime : Int,
  rest : Float,
  compartment : Int,
) -> Float {
  let mut best : Float = v[compartment + spiketime * n_compartments]
  let n_steps = v.length() / n_compartments
  let mut t = spiketime + 1
  while t < n_steps {
    let val = v[compartment + t * n_compartments]
    if val < best {
      best = val
    }
    t = t + 1
  }
  best - rest
}

///|
/// `exc_peak_with_time(v, n_compartments, spiketime, rest, compartment)` —
/// returns `(peak, time_of_peak)` where peak is the voltage deviation
/// (`max - rest`) and time_of_peak is the 0-based timestep at which the
/// maximum occurred. Mirrors Julia's `get_EPSP` extended output.
pub fn exc_peak_with_time(
  v : Array[Float],
  n_compartments : Int,
  spiketime : Int,
  rest : Float,
  compartment : Int,
) -> (Float, Int) {
  let mut best_val : Float = v[compartment + spiketime * n_compartments]
  let mut best_t : Int = spiketime
  let n_steps = v.length() / n_compartments
  let mut t = spiketime + 1
  while t < n_steps {
    let val = v[compartment + t * n_compartments]
    if val > best_val {
      best_val = val
      best_t = t
    }
    t = t + 1
  }
  (best_val - rest, best_t)
}

///|
/// `inh_trough_with_time(v, n_compartments, spiketime, rest, compartment)` —
/// returns `(trough, time_of_trough)` for inhibitory EPSP. Symmetric
/// counterpart of `exc_peak_with_time`.
pub fn inh_trough_with_time(
  v : Array[Float],
  n_compartments : Int,
  spiketime : Int,
  rest : Float,
  compartment : Int,
) -> (Float, Int) {
  let mut best_val : Float = v[compartment + spiketime * n_compartments]
  let mut best_t : Int = spiketime
  let n_steps = v.length() / n_compartments
  let mut t = spiketime + 1
  while t < n_steps {
    let val = v[compartment + t * n_compartments]
    if val < best_val {
      best_val = val
      best_t = t
    }
    t = t + 1
  }
  (best_val - rest, best_t)
}

///|
/// `exc_peak_window(v, n_compartments, t_start, t_end, rest, compartment)` —
/// peak deviation within a closed interval `[t_start, t_end]`. Mirrors
/// Julia's `get_EPSP(..., interval=...)` overload.
pub fn exc_peak_window(
  v : Array[Float],
  n_compartments : Int,
  t_start : Int,
  t_end : Int,
  rest : Float,
  compartment : Int,
) -> Float {
  if t_start > t_end || t_start < 0 {
    return 0.0F
  }
  let mut best : Float = v[compartment + t_start * n_compartments]
  let mut t = t_start + 1
  while t <= t_end {
    let val = v[compartment + t * n_compartments]
    if val > best {
      best = val
    }
    t = t + 1
  }
  best - rest
}

///|
/// `inh_trough_window(v, n_compartments, t_start, t_end, rest, compartment)` —
/// trough within `[t_start, t_end]`. Symmetric counterpart.
pub fn inh_trough_window(
  v : Array[Float],
  n_compartments : Int,
  t_start : Int,
  t_end : Int,
  rest : Float,
  compartment : Int,
) -> Float {
  if t_start > t_end || t_start < 0 {
    return 0.0F
  }
  let mut best : Float = v[compartment + t_start * n_compartments]
  let mut t = t_start + 1
  while t <= t_end {
    let val = v[compartment + t * n_compartments]
    if val < best {
      best = val
    }
    t = t + 1
  }
  best - rest
}

///|
/// `epsp_pair(v, n_compartments, spiketime, rest, compartment)` — returns
/// `(exc_peak, inh_trough)` in one pass. Walks the trace once and tracks
/// max + min simultaneously. Useful when both excitatory and inhibitory
/// responses are expected (e.g. EI-balanced stimuli).
pub fn epsp_pair(
  v : Array[Float],
  n_compartments : Int,
  spiketime : Int,
  rest : Float,
  compartment : Int,
) -> (Float, Float) {
  let mut best_max : Float = v[compartment + spiketime * n_compartments]
  let mut best_min : Float = best_max
  let n_steps = v.length() / n_compartments
  let mut t = spiketime + 1
  while t < n_steps {
    let val = v[compartment + t * n_compartments]
    if val > best_max { best_max = val }
    if val < best_min { best_min = val }
    t = t + 1
  }
  (best_max - rest, best_min - rest)
}

///|
/// `epsp_pair_window(v, n_compartments, t_start, t_end, rest, compartment)` —
/// `(exc_peak, inh_trough)` within `[t_start, t_end]`.
pub fn epsp_pair_window(
  v : Array[Float],
  n_compartments : Int,
  t_start : Int,
  t_end : Int,
  rest : Float,
  compartment : Int,
) -> (Float, Float) {
  if t_start > t_end || t_start < 0 {
    return (0.0F, 0.0F)
  }
  let mut best_max : Float = v[compartment + t_start * n_compartments]
  let mut best_min : Float = best_max
  let mut t = t_start + 1
  while t <= t_end {
    let val = v[compartment + t * n_compartments]
    if val > best_max { best_max = val }
    if val < best_min { best_min = val }
    t = t + 1
  }
  (best_max - rest, best_min - rest)
}

///|
/// Z-score normalisation: returns the standard score `(v - mean) / std`
/// for the voltage trace `v[compartment, t_start:t_end]`. Useful for
/// comparing EPSP magnitudes across compartments with different
/// baseline voltages.
pub fn epsp_normalise(
  v : Array[Float],
  n_compartments : Int,
  compartment : Int,
  t_start : Int,
  t_end : Int,
) -> Array[Float] {
  let n_steps = v.length() / n_compartments
  if t_start > t_end || t_start < 0 || t_end >= n_steps {
    return []
  }
  let n = t_end - t_start + 1
  // Compute mean.
  let mut sum : Float = 0.0F
  let mut t = t_start
  while t <= t_end {
    sum = sum + v[compartment + t * n_compartments]
    t = t + 1
  }
  let mean = sum / Float::from_int(n)
  // Compute std (population, N).
  let mut var_sum : Float = 0.0F
  let mut t2 = t_start
  while t2 <= t_end {
    let d = v[compartment + t2 * n_compartments] - mean
    var_sum = var_sum + d * d
    t2 = t2 + 1
  }
  let std = (var_sum / Float::from_int(n)).sqrt()
  if std == 0.0F {
    // Degenerate: return zeros.
    let out : Array[Float] = Array::make(n, 0.0F)
    return out
  }
  // Build z-score array.
  let out : Array[Float] = Array::make(n, 0.0F)
  let mut k = 0
  let mut t3 = t_start
  while t3 <= t_end {
    out[k] = (v[compartment + t3 * n_compartments] - mean) / std
    k = k + 1
    t3 = t3 + 1
  }
  out
}

///|
/// Normalise ALL compartments at once over the time window
/// `[t_start, t_end]`. Returns `Array[Array[Float]]` of length
/// `n_compartments`, where each inner array is `[length window]` z-scores.
/// Useful for computing relative EPSP magnitudes across the soma and
/// all dendrites simultaneously.
pub fn epsp_normalise_by_compartment(
  v : Array[Float],
  n_compartments : Int,
  t_start : Int,
  t_end : Int,
) -> Array[Array[Float]] {
  let n_steps = v.length() / n_compartments
  if t_start > t_end || t_start < 0 || t_end >= n_steps {
    return []
  }
  let n = t_end - t_start + 1
  let result : Array[Array[Float]] = []
  for comp in 0..<n_compartments {
    // Compute mean for this compartment over the window.
    let mut sum : Float = 0.0F
    let mut t = t_start
    while t <= t_end {
      sum = sum + v[comp + t * n_compartments]
      t = t + 1
    }
    let mean = sum / Float::from_int(n)
    // Compute std (population).
    let mut var_sum : Float = 0.0F
    let mut t2 = t_start
    while t2 <= t_end {
      let d = v[comp + t2 * n_compartments] - mean
      var_sum = var_sum + d * d
      t2 = t2 + 1
    }
    let std = (var_sum / Float::from_int(n)).sqrt()
    let z : Array[Float] = Array::make(n, 0.0F)
    if std == 0.0F {
      result.push(z)
      continue
    }
    let mut k = 0
    let mut t3 = t_start
    while t3 <= t_end {
      z[k] = (v[comp + t3 * n_compartments] - mean) / std
      k = k + 1
      t3 = t3 + 1
    }
    result.push(z)
  }
  result
}

///|
/// `epsp_normalise_windowed_by_compartment(v, n_compartments, t_start,
/// t_end, win_start, win_end)` — like `epsp_normalise_by_compartment`
/// but uses only the `[win_start, win_end]` sub-window for the
/// per-compartment mean/std computation, while still returning
/// z-scores over the full `[t_start, t_end]` range. Useful when the
/// baseline window (e.g. before a spike) differs from the
/// observation window (e.g. after the spike).
pub fn epsp_normalise_windowed_by_compartment(
  v : Array[Float],
  n_compartments : Int,
  t_start : Int,
  t_end : Int,
  win_start : Int,
  win_end : Int,
) -> Array[Array[Float]] {
  let n_steps = v.length() / n_compartments
  if t_start > t_end || t_start < 0 || t_end >= n_steps {
    return []
  }
  if win_start > win_end || win_start < 0 || win_end >= n_steps {
    return []
  }
  let n = t_end - t_start + 1
  let n_win = win_end - win_start + 1
  let result : Array[Array[Float]] = []
  for comp in 0..<n_compartments {
    // Compute mean over the baseline window.
    let mut sum : Float = 0.0F
    let mut t = win_start
    while t <= win_end {
      sum = sum + v[comp + t * n_compartments]
      t = t + 1
    }
    let mean = sum / Float::from_int(n_win)
    // Compute std (population) over the baseline window.
    let mut var_sum : Float = 0.0F
    let mut t2 = win_start
    while t2 <= win_end {
      let d = v[comp + t2 * n_compartments] - mean
      var_sum = var_sum + d * d
      t2 = t2 + 1
    }
    let std = (var_sum / Float::from_int(n_win)).sqrt()
    let z : Array[Float] = Array::make(n, 0.0F)
    if std == 0.0F {
      result.push(z)
      continue
    }
    // Build z-score array over the observation window.
    let mut k = 0
    let mut t3 = t_start
    while t3 <= t_end {
      z[k] = (v[comp + t3 * n_compartments] - mean) / std
      k = k + 1
      t3 = t3 + 1
    }
    result.push(z)
  }
  result
}
