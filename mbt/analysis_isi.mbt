// analysis_isi.mbt — Interspike Interval (ISI) statistics.
//
// Bit-exact port of the ISI_CV2 helpers from
// `refs/SNNModels.jl/src/analysis/spikes.jl`.
//
// Julia's `ISI_CV2`:
//   function ISI_CV2(spiketime::Vector{Float32}; interval=nothing)
//       ISI = diff(spiketime)
//       CV2 = Float32[]
//       for i in eachindex(ISI)
//           i == 1 && continue
//           x = 2(abs(ISI[i] - ISI[i-1]) / (ISI[i] + ISI[i-1]))
//           push!(CV2, x)
//       end
//       _cv = mean(CV2)
//       return isnan(_cv) ? 0.0 : _cv
//   end
//
//   # population variant: returns one CV2 per neuron
//   function ISI_CV2(x::Spiketimes; interval=nothing)
//       return ISI_CV2.(x, ; interval)
//   end

///|
/// Compute ISI_CV2 for a single spike train (one neuron).
///
/// Formula:
///   ISI[i] = spike_times[i] - spike_times[i-1]   for i = 1 .. N-1
///   CV2[i] = 2 * |ISI[i] - ISI[i-1]| / (ISI[i] + ISI[i-1])
///           for i = 2 .. N-1
///   return mean(CV2)
///
/// Returns 0.0 for:
///   - empty spike trains (no ISIs)
///   - single-spike trains (only 1 ISI, no CV2 pair)
///   - NaN (zero ISIs)
///
/// Mirrors Julia's `ISI_CV2(spiketime::Vector{Float32})`.
pub fn isi_cv2_one(spike_times : Array[Float]) -> Float {
  let n = spike_times.length()
  if n < 3 {
    // Need at least 3 spikes → 2 ISIs → 1 CV2 pair.
    return 0.0F
  }
  // Compute ISIs.
  let n_isi = n - 1
  let isi : Array[Float] = Array::make(n_isi, 0.0F)
  let mut k = 0
  while k < n_isi {
    isi[k] = spike_times[k + 1] - spike_times[k]
    k = k + 1
  }
  // Compute CV2 for k = 1 .. n_isi-1 (skip the first ISI).
  let n_cv2 = n_isi - 1
  let cv2 : Array[Float] = Array::make(n_cv2, 0.0F)
  let mut i = 0
  while i < n_cv2 {
    let isi_cur = isi[i + 1]
    let isi_prev = isi[i]
    let sum = isi_cur + isi_prev
    if sum == 0.0F {
      // NaN in Julia (0/0); we return 0.0 like Julia's `isnan(_) ? 0.0 : _`.
      cv2[i] = 0.0F
    } else {
      let diff = if isi_cur > isi_prev {
        isi_cur - isi_prev
      } else {
        isi_prev - isi_cur
      }
      cv2[i] = 2.0F * diff / sum
    }
    i = i + 1
  }
  // Mean.
  let mut total = 0.0F
  let mut j = 0
  while j < n_cv2 {
    total = total + cv2[j]
    j = j + 1
  }
  let mean_cv2 = total / Float::from_int(n_cv2)
  // NaN check (NaN != NaN by IEEE 754).
  if mean_cv2 != mean_cv2 {
    0.0F
  } else {
    mean_cv2
  }
}

///|
/// Compute ISI_CV2 for every neuron in a population. Input is
/// `Array[Array[Float]]` indexed by neuron id, each inner array
/// is the sorted spike times for that neuron.
///
/// Mirrors Julia's `ISI_CV2(spiketimes::Spiketimes)` which returns
/// one CV2 per neuron via broadcasting.
pub fn isi_cv2(spike_times_per_neuron : Array[Array[Float]]) -> Array[Float] {
  let n = spike_times_per_neuron.length()
  let out : Array[Float] = Array::make(n, 0.0F)
  let mut i = 0
  while i < n {
    out[i] = isi_cv2_one(spike_times_per_neuron[i])
    i = i + 1
  }
  out
}