// analysis_intervals.mbt — time interval membership helpers.
//
// Port of `refs/SNNUtils.jl/src/stimuli/sequence/sequence.jl`:
//   - `time_in_interval(x, intervals)` — true if `x ∈ [start, end]` of
//     any interval in `intervals`.
//   - `start_interval(x, intervals)` — start of the interval containing
//     `x`, or -1.0F if no such interval exists.
//
// `intervals` is `Array[Array[Float]]` where each inner array has length 2
// (start, end).

///|
/// Returns true iff `x` is in any interval (start ≤ x ≤ end).
/// `intervals : Array[Array[Float]]` where each inner array has 2 entries
/// (start, end). Empty interval list → false.
pub fn time_in_interval(
  x : Float,
  intervals : Array[Array[Float]],
) -> Bool {
  for i in 0..<intervals.length() {
    let iv = intervals[i]
    if iv.length() >= 2 {
      if x >= iv[0] && x <= iv[1] {
        return true
      }
    }
  }
  false
}

///|
/// Returns the start of the interval containing `x`, or -1.0F if none.
pub fn start_interval(
  x : Float,
  intervals : Array[Array[Float]],
) -> Float {
  for i in 0..<intervals.length() {
    let iv = intervals[i]
    if iv.length() >= 2 {
      if x >= iv[0] && x <= iv[1] {
        return iv[0]
      }
    }
  }
  -1.0F
}

///|
/// Returns the end of the interval containing `x`, or -1.0F if none.
/// (Convenience counterpart to `start_interval` — useful in attack_decay
/// functions from `inputs.jl`.)
pub fn end_interval(
  x : Float,
  intervals : Array[Array[Float]],
) -> Float {
  for i in 0..<intervals.length() {
    let iv = intervals[i]
    if iv.length() >= 2 {
      if x >= iv[0] && x <= iv[1] {
        return iv[1]
      }
    }
  }
  -1.0F
}