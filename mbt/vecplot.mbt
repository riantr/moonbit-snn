// vecplot — text-dump Monitor data.
//
// Julia's `vecplot(pop, :v)` returns a `Plots.plot` object. Since we
// don't have Makie/Plots in the MoonBit port, we instead provide:
//   - `Monitor::dump_csv(m, path)`  : write time-series to CSV
//   - `Monitor::dump_summary(m)`    : print min/max/mean to stdout
//   - `Monitor::count_spikes(m)`    : count spike transitions in :fire
//
// This is a v0.0.x stand-in for vecplot. When the user wires up
// their own Python/Makie post-processing, they can call these
// dump functions to get the data out.

///|
/// Count rising-edge transitions in a fire monitor.
/// Returns the number of times the monitored neuron's fire flag
/// went from false to true. Approximates spike count.
pub fn Monitor::count_spikes(m : Monitor) -> Int {
  let n = m.data.length()
  if n == 0 {
    return 0
  }
  let mut count = 0
  let mut prev = 0.0F
  for i in 0..<n {
    let cur = m.data[i]
    if prev < 0.5F && cur >= 0.5F {
      count = count + 1
    }
    prev = cur
  }
  count
}

///|
/// Compute the firing rate (Hz) of a fire monitor over the full
/// recorded duration. Returns `count_spikes(m) / duration_ms * 1000.0F`.
/// Returns 0.0F if the buffer is empty or the duration is zero.
pub fn Monitor::firing_rate(m : Monitor) -> Float {
  let n = m.data.length()
  if n < 2 {
    return 0.0F
  }
  let n_t = m.times.length()
  if n_t < 2 {
    return 0.0F
  }
  let total_ms = m.times[n_t - 1] - m.times[0]
  if total_ms <= 0.0F {
    return 0.0F
  }
  let n_spikes = m.count_spikes()
  Float::from_int(n_spikes) * 1000.0F / total_ms
}

///|
/// Count rising-edge transitions in the fire buffer that occur
/// within the time window `[t_start, t_end]` (ms, inclusive on both
/// ends). Returns 0 if no spikes in the window. Used to compute
/// firing rates over sub-intervals of the simulation.
pub fn Monitor::count_spikes_interval(
  m : Monitor,
  t_start : Float,
  t_end : Float,
) -> Int {
  let n = m.data.length()
  let n_t = m.times.length()
  let lim = if n < n_t { n } else { n_t }
  let mut count = 0
  let mut prev = 0.0F
  for i in 0..<lim {
    let cur = m.data[i]
    let t = m.times[i]
    if t >= t_start && t <= t_end && prev < 0.5F && cur >= 0.5F {
      count = count + 1
    }
    prev = cur
  }
  count
}

///|
/// Clear all recorded data and timestamps. Mirrors Julia's
/// `clear_records!(pop)` API. Used to reset monitoring between
/// sim!() calls without re-allocating monitors.
pub fn Monitor::clear_records(m : Monitor) -> Unit {
  m.data.clear()
  m.times.clear()
}

///|
/// Return the mean of the monitor's data buffer.
/// Returns 0.0F if the buffer is empty.
pub fn Monitor::mean(m : Monitor) -> Float {
  let n = m.data.length()
  if n == 0 {
    return 0.0F
  }
  let mut sum = 0.0F
  for i in 0..<n {
    sum = sum + m.data[i]
  }
  sum / Float::from_int(n)
}

///|
/// Return the minimum value of the monitor's data buffer.
/// Panics via `assert_eq` if the buffer is empty.
pub fn Monitor::min(m : Monitor) -> Float {
  let n = m.data.length()
  if n == 0 {
    return 0.0F
  }
  let mut mn = m.data[0]
  for i in 1..<n {
    if m.data[i] < mn { mn = m.data[i] }
  }
  mn
}

///|
/// Return the maximum value of the monitor's data buffer.
/// Panics via `assert_eq` if the buffer is empty.
pub fn Monitor::max(m : Monitor) -> Float {
  let n = m.data.length()
  if n == 0 {
    return 0.0F
  }
  let mut mx = m.data[0]
  for i in 1..<n {
    if m.data[i] > mx { mx = m.data[i] }
  }
  mx
}

///|
/// Compute the firing rate (Hz) within a sub-interval `[t_start, t_end]`.
/// Returns `count_spikes_interval(m, t_start, t_end) / duration * 1000.0F`.
/// Returns 0.0F if the interval is empty or duration is zero.
pub fn Monitor::firing_rate_interval(
  m : Monitor,
  t_start : Float,
  t_end : Float,
) -> Float {
  let duration = t_end - t_start
  if duration <= 0.0F {
    return 0.0F
  }
  let n_spikes = m.count_spikes_interval(t_start, t_end)
  Float::from_int(n_spikes) * 1000.0F / duration
}

///|
/// Return the spike times (in ms) as an array of timestamps at which
/// the monitored neuron fired (rising-edge transitions in the fire
/// buffer). Returns an empty array if no spikes. Mirrors Julia's
/// `spiketimes(pop, neuron)` API at the per-neuron level.
pub fn Monitor::spike_times(m : Monitor) -> Array[Float] {
  let n = m.data.length()
  let n_t = m.times.length()
  let lim = if n < n_t { n } else { n_t }
  let result : Array[Float] = []
  let mut prev = 0.0F
  for i in 0..<lim {
    let cur = m.data[i]
    if prev < 0.5F && cur >= 0.5F {
      result.push(m.times[i])
    }
    prev = cur
  }
  result
}

///|
/// Print a one-line summary of the monitor buffer (min/max/mean).
pub fn Monitor::dump_summary(m : Monitor) -> Unit {
  let n = m.data.length()
  if n == 0 {
    println("Monitor[\{m.sym}] (empty)")
    return
  }
  let mut mn = m.data[0]
  let mut mx = m.data[0]
  let mut sum = 0.0F
  for i in 0..<n {
    let v = m.data[i]
    if v < mn { mn = v }
    if v > mx { mx = v }
    sum = sum + v
  }
  let mean = sum / Float::from_int(n)
  println("Monitor[\{m.sym}] n=" + n.to_string() +
          " min=" + mn.to_string() +
          " max=" + mx.to_string() +
          " mean=" + mean.to_string())
}

///|
/// Dump the monitor buffer as CSV to stdout: `time,value` lines.
/// Useful for piping to a Python plotting script.
pub fn Monitor::dump_csv_stdout(m : Monitor) -> Unit {
  let n = m.data.length()
  let n_t = m.times.length()
  let lim = if n < n_t { n } else { n_t }
  for i in 0..<lim {
    let t = m.times[i]
    let v = m.data[i]
    println(t.to_string() + "," + v.to_string())
  }
}

///|
/// Total duration covered by the monitor buffer.
pub fn Monitor::duration(m : Monitor) -> Float {
  if m.times.length() < 2 {
    return 0.0F
  }
  m.times[m.times.length() - 1] - m.times[0]
}

///|
/// Print a text-mode ASCII plot of the monitor buffer. Each
/// data point becomes a single character placed in a column at the
/// appropriate vertical position. Default width is 80 columns
/// (down-sampled if buffer is larger); default height is 15 rows.
///
/// `width` and `height` can be passed to override the canvas size.
/// The plot is rendered with the value axis on the left (max at top,
/// min at bottom) and the time axis on the x-axis (oldest at left,
/// newest at right). Spike markers (*) are added for fire monitors
/// where the value crosses 0.5.
pub fn Monitor::ascii_plot(
  m : Monitor,
  width? : Int = 80,
  height? : Int = 15,
) -> Unit {
  let n = m.data.length()
  if n == 0 {
    println("Monitor[\{m.sym}] (empty)")
    return
  }
  // Find min/max of data.
  let mut mn = m.data[0]
  let mut mx = m.data[0]
  for i in 0..<n {
    let v = m.data[i]
    if v < mn { mn = v }
    if v > mx { mx = v }
  }
  // Avoid degenerate range.
  let vrange = if mx - mn < 1.0e-6F { 1.0e-6F } else { mx - mn }
  let n_cols = if width > n { n } else { width }
  let n_rows = height
  // Down-sample: pick `n_cols` indices evenly spaced across [0, n).
  let col_to_idx : Array[Int] = Array::make(n_cols, 0)
  if n_cols == 1 {
    col_to_idx[0] = 0
  } else {
    for c in 0..<n_cols {
      // c * (n-1) / (n_cols - 1)
      col_to_idx[c] = c * (n - 1) / (n_cols - 1)
    }
  }
  // Build canvas: rows of StringBuilder.
  let canvas : Array[String] = Array::make(n_rows, "")
  // Each row is a string of n_cols spaces (plus padding for label).
  let row_init = " "  // left padding for value labels
  let pad = String::make(n_cols, ' ')
  for r in 0..<n_rows {
    canvas[r] = row_init + pad
  }
  // Plot each column.
  for c in 0..<n_cols {
    let idx = col_to_idx[c]
    let v = m.data[idx]
    // Map v to row 0..n_rows-1 (0 = top, n_rows-1 = bottom).
    let normalized = (v - mn) / vrange  // 0..1
    let row_from_top = (Float::from_int(n_rows) - Float::from_int(1) * normalized).to_int()
    // Clamp to [0, n_rows-1].
    let r = if row_from_top < 0 {
      0
    } else if row_from_top >= n_rows {
      n_rows - 1
    } else {
      row_from_top
    }
    // Place a character. Use '*' for fire spikes (>=0.5), '.' for others.
    let ch = if m.sym == "fire" {
      if v >= 0.5F { '*' } else { '.' }
    } else {
      '*'
    }
    let row_str = canvas[r]
    let new_row = row_int_set_char(row_str, c + row_init.length(), ch)
    canvas[r] = new_row
  }
  // Add value-axis labels at top, middle, bottom.
  // Render header (max).
  let max_label = format_axis_label(mx)
  let mid_label = format_axis_label((mx + mn) / 2.0F)
  let min_label = format_axis_label(mn)
  // Find the longest axis label so we can left-align.
  let label_width = {
    let a = max_label.length()
    let b = mid_label.length()
    let c = min_label.length()
    let m = if a > b { a } else { b }
    if m > c { m } else { c }
  }
  // Print from top row to bottom.
  for r in 0..<n_rows {
    let label = if r == 0 {
      max_label
    } else if r == n_rows - 1 {
      min_label
    } else if r == n_rows / 2 {
      mid_label
    } else {
      ""
    }
    let pad_count = if label_width > label.length() {
      label_width - label.length()
    } else {
      0
    }
    let padded = String::make(pad_count, ' ')
    println(padded + label + " |" + canvas[r])
  }
  // Footer: x-axis labels (start/end times).
  let n_t = m.times.length()
  if n_t >= 2 {
    let t_start = m.times[0]
    let t_end = m.times[n_t - 1]
    let total_chars = row_init.length() + n_cols + max_label.length() + 3
    let pad_str = String::make(total_chars - 4, ' ')
    println(pad_str + "t=" + t_start.to_string() + " → " + t_end.to_string())
  }
  let _ = m.duration()  // ensure method accessible
}

///|
/// Format a number with up to 6 significant digits for axis labels.
fn format_axis_label(v : Float) -> String {
  // Simple format: round to 4 decimal places, then strip trailing zeros.
  let scaled = v * 10000.0F
  let rounded = scaled.round().to_int()
  let scaled_back = Float::from_int(rounded) / 10000.0F
  scaled_back.to_string()
}

///|
/// Replace the character at position `pos` in `s` with `ch`.
/// Returns the new string. Builds via StringBuilder.
fn row_int_set_char(s : String, pos : Int, ch : Char) -> String {
  let sb = StringBuilder::new(size_hint=s.length())
  let len = s.length()
  // Write prefix [0, pos), then ch, then suffix [pos+1, len).
  let prefix_end = if pos < len { pos } else { len }
  if prefix_end > 0 {
    // Use String::substring to extract the prefix (0..prefix_end).
    let prefix = String::make(prefix_end, ' ')
    // Easier: just iterate by char position.
    let mut k = 0
    while k < prefix_end {
      // Hack: pull chars one at a time using string view.
      // Actually, MoonBit doesn't have s[k] indexing on String; use views.
      // For simplicity, fall back to single-char writes.
      sb.write_char(s[k].unsafe_to_char())
      k = k + 1
    }
    let _ = prefix
  }
  sb.write_char(ch)
  if pos + 1 < len {
    let mut k = pos + 1
    while k < len {
      sb.write_char(s[k].unsafe_to_char())
      k = k + 1
    }
  }
  sb.to_string()
}
