// stimulus_timed.mbt — SpikeTimeStimulus (prescribed spike trains).
//
// Port of SNNModels.jl/src/stimuli/timed.jl.
//
// SpikeTimeStimulus delivers spikes to a post-synaptic receptor
// (glu or gaba) at specific times stored in a SpikeTimeParameter.
// Unlike PoissonStimulusIF which samples spikes randomly,
// SpikeTimeStimulus gives exact reproducible spike timing.
//
// The user supplies:
//   - spiketimes : Array[Float]   (sorted ascending, in ms)
//   - neurons    : Array[Int]    (which pre-synaptic index fires)
//
// At each step, the stimulus checks if the current simulation time
// has passed the next spike time. If so, it adds W to the target
// conductance g of the postsynaptic neuron specified by the connection
// matrix. Then it advances to the next spike time.
//
// This is the canonical way to inject exact spike trains in SNN.

///|
/// Parameters for SpikeTimeStimulus. `spiketimes` is sorted
/// ascending (in ms). `neurons[i]` is the pre-synaptic index that
/// fires at `spiketimes[i]`.
pub struct SpikeTimeParameter {
  spiketimes : Array[Float]
  neurons : Array[Int]
}

///|
/// Construct a SpikeTimeParameter from parallel arrays. Sorts by
/// spike time so the next-spike pointer walks monotonically.
pub fn SpikeTimeParameter::new(
  spiketimes : Array[Float],
  neurons : Array[Int],
) -> SpikeTimeParameter {
  // Simple insertion sort by spiketime — these arrays are small (≤ 100s).
  let n = spiketimes.length()
  let sorted_t : Array[Float] = spiketimes.copy()
  let sorted_n : Array[Int] = neurons.copy()
  let mut i = 1
  while i < n {
    let key_t = sorted_t[i]
    let key_n = sorted_n[i]
    let mut j = i
    while j > 0 && sorted_t[j - 1] > key_t {
      sorted_t[j] = sorted_t[j - 1]
      sorted_n[j] = sorted_n[j - 1]
      j = j - 1
    }
    sorted_t[j] = key_t
    sorted_n[j] = key_n
    i = i + 1
  }
  { spiketimes: sorted_t, neurons: sorted_n }
}

///|
/// SpikeTimeStimulus — injects spikes at exact times. Mirrors Julia's
/// `SNN.SpikeTimeStimulus(E, :ge; param, conn)`. Uses next_spike +
/// next_index as monotonic pointers into the param's spike list.
/// `g` is the post-synaptic receptor (e.g. `e_pop.glu` for :ge).
pub struct SpikeTimeStimulus {
  n : Int
  param : SpikeTimeParameter
  // Mutable pointer state — next spike to fire.
  next_spike : Array[Float]
  next_index : Array[Int]
  // Fire flag for each pre-synaptic index.
  fire : Array[Bool]
  // Target conductance (post-synaptic glu or gaba).
  g : Array[Float]
}

///|
/// Construct a SpikeTimeStimulus targeting the `g` receptor of a
/// post-synaptic IF population. `spiketimes` should be in ms and
/// ascending; `neurons` are pre-synaptic indices in [0, N).
pub fn SpikeTimeStimulus::new(
  e_pop : IF,
  sym : String,
  spiketimes : Array[Float],
  neurons : Array[Int],
) -> SpikeTimeStimulus {
  let n = e_pop.n
  let param = SpikeTimeParameter::new(spiketimes, neurons)
  let fire : Array[Bool] = Array::make(n, false)
  let g = if sym == "ge" { e_pop.glu } else { e_pop.gaba }
  let next_spike : Array[Float] = if param.spiketimes.length() > 0 {
    [param.spiketimes[0]]
  } else {
    [0.0F / 0.0F]  // +Inf
  }
  let next_index : Array[Int] = if param.spiketimes.length() > 0 {
    [0]
  } else {
    [-1]
  }
  { n, param, next_spike, next_index, fire, g }
}

///|
/// Advance the SpikeTimeStimulus by one step. If the current time
/// has reached the next spike time, deposit weight into `g[post_idx]`]
/// and advance the pointer. Mutates `fire[j]` to mark the pre-synaptic
/// neuron that fired.
pub fn stimulate_spiketime(
  s : SpikeTimeStimulus,
  t : Float,
  w : Float,
) -> Unit {
  // Reset all fire flags.
  let mut i = 0
  while i < s.n {
    s.fire[i] = false
    i = i + 1
  }
  // Process all spikes whose time ≤ t.
  while s.next_index[0] >= 0 && s.next_spike[0] <= t {
    let j = s.param.neurons[s.next_index[0]]
    s.fire[j] = true
    s.g[j] = s.g[j] + w
    // Advance.
    if s.next_index[0] + 1 < s.param.spiketimes.length() {
      s.next_index[0] = s.next_index[0] + 1
      s.next_spike[0] = s.param.spiketimes[s.next_index[0]]
    } else {
      s.next_spike[0] = 0.0F / 0.0F  // +Inf: no more spikes
      s.next_index[0] = -1
    }
  }
}