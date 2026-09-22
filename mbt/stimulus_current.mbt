// stimulus_current.mbt — port of SNNModels.jl/src/stimuli/current.jl
//
// A CurrentStimulus injects a current (constant or time-varying) into
// a target population's `I` array each step. Matches Julia's
// `CurrentStimulus(post; param=CurrentNoise(...))` and
// `CurrentStimulus(post, :I; param=CurrentFixed(I_base))` patterns.
//
// Two variants are provided:
//   - `CurrentStimulusIF` — bound to an IF population. Uses the IF's
//     `i` field. The compose layer dispatches via the `CurrentIF_` variant.
//   - `CurrentStimulusArray` — bound to a raw `Array[Float]` (the
//     `i` array of any population). This lets AdEx, IZ, HH, Poisson
//     etc. share the same current-injection logic without needing a
//     dedicated struct per population type. The compose layer
//     dispatches via the `CurrentArr_` variant.

///|
/// CurrentStimulus — bound to an IF population. Injects current into
/// the IF's `i` array each step. With `active=true` and `noise_sigma=0`,
/// sets `i[i] = i_base` for every neuron. With `noise_sigma > 0`,
/// adds `N(0, noise_sigma)` noise per step.
pub struct CurrentStimulusIF {
  mut i_base : Float
  active : Array[Bool]
  pop : IF
  noise_sigma : Float
  rng : Xoshiro
}

///|
/// Construct a constant-current stimulus targeting an IF population.
pub fn CurrentStimulusIF::new(
  pop : IF,
  i_base : Float,
  rng : Xoshiro,
  noise_sigma? : Float = 0.0F,
) -> CurrentStimulusIF {
  { i_base, active: [true], pop, noise_sigma, rng }
}

///|
/// Disable / enable the stimulus.
pub fn CurrentStimulusIF::set_active(s : CurrentStimulusIF, v : Bool) -> Unit {
  s.active[0] = v
}

///|
/// Update the baseline current at runtime.
pub fn CurrentStimulusIF::set_i_base(s : CurrentStimulusIF, v : Float) -> Unit {
  s.i_base = v
}

///|
/// Apply the current stimulus for one step.
pub fn stimulate_current_if(s : CurrentStimulusIF) -> Unit {
  if !s.active[0] {
    return
  }
  let pop = s.pop
  let n = pop.n
  if s.noise_sigma <= 0.0F {
    for i in 0..<n {
      pop.i[i] = s.i_base
    }
  } else {
    let sigma = s.noise_sigma
    let mut k = 0
    while k < n {
      let (z1, _) = box_muller(s.rng)
      pop.i[k] = s.i_base + sigma * Float::from_double(z1)
      k = k + 1
    }
  }
}

///|
/// CurrentStimulusArray — generic current stimulus that writes into
/// a raw `Array[Float]`. Works for any population (AdEx, IZ, HH,
/// Poisson, WilsonCowan, etc.) — the caller passes the population's
/// `i` array directly. Same noise / base semantics as `CurrentStimulusIF`.
pub struct CurrentStimulusArray {
  mut i_base : Float
  active : Array[Bool]
  i : Array[Float]
  n : Int
  noise_sigma : Float
  rng : Xoshiro
}

///|
/// Construct a constant-current stimulus targeting any population's
/// `i` array.
pub fn CurrentStimulusArray::new(
  i : Array[Float],
  n : Int,
  i_base : Float,
  rng : Xoshiro,
  noise_sigma? : Float = 0.0F,
) -> CurrentStimulusArray {
  { i_base, active: [true], i, n, noise_sigma, rng }
}

///|
/// Disable / enable the array-targeted stimulus.
pub fn CurrentStimulusArray::set_active(
  s : CurrentStimulusArray,
  v : Bool,
) -> Unit {
  s.active[0] = v
}

///|
/// Update the baseline current at runtime.
pub fn CurrentStimulusArray::set_i_base(
  s : CurrentStimulusArray,
  v : Float,
) -> Unit {
  s.i_base = v
}

///|
/// Apply the array-targeted current stimulus for one step.
pub fn stimulate_current_array(s : CurrentStimulusArray) -> Unit {
  if !s.active[0] {
    return
  }
  if s.noise_sigma <= 0.0F {
    for k in 0..<s.n {
      s.i[k] = s.i_base
    }
  } else {
    let sigma = s.noise_sigma
    let mut k = 0
    while k < s.n {
      let (z1, _) = box_muller(s.rng)
      s.i[k] = s.i_base + sigma * Float::from_double(z1)
      k = k + 1
    }
  }
}
