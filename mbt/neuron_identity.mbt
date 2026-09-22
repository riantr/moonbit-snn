// neuron_identity.mbt — Identity "pass-through" neuron.
//
// Port of SNNModels.jl/src/populations/identity.jl.
//
// Identity is a simple population type that acts as an identity
// function: every neuron fires whenever its conductance g > 0.
// Used in Lagzi2022 and similar network experiments where one
// population receives inputs and broadcasts to another.
//
// Each step:
//   h[i] += g[i]            # accumulate input
//   fire[i] = g[i] > 0      # fire when there's input
//   g[i] = 0                # reset for next step

///|
/// Identity neuron parameters (empty struct).
pub(all) struct IdentityParameter {
  dummy : Float
}

///|
/// Construct an Identity neuron population.
pub struct Identity {
  n : Int
  param : IdentityParameter
  // Conductance (input).
  g : Array[Float]
  // Output (accumulated spike count).
  h : Array[Float]
  // Spike flag.
  fire : Array[Bool]
  // Spike count for monitoring.
  spikecount : Array[Float]
}

///|
/// Construct an Identity population with N neurons.
pub fn Identity::new(n : Int, param : IdentityParameter) -> Identity {
  let g : Array[Float] = Array::make(n, 0.0F)
  let h : Array[Float] = Array::make(n, 0.0F)
  let fire : Array[Bool] = Array::make(n, false)
  let spikecount : Array[Float] = Array::make(n, 0.0F)
  { n, param, g, h, fire, spikecount }
}

///|
/// Integrate Identity for one timestep. Fire[i] is true if g[i] > 0.
pub fn step_neuron_id(p : Identity, dt : Float) -> Unit {
  let mut i = 0
  while i < p.n {
    p.h[i] = p.h[i] + p.g[i]
    p.spikecount[i] = 0.0F
    if p.g[i] > 0.0F {
      p.fire[i] = true
      p.spikecount[i] = p.g[i]
    } else {
      p.fire[i] = false
    }
    p.g[i] = 0.0F
    i = i + 1
  }
}