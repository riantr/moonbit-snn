// WilsonCowan rate-model neuron — port of SNNModels.jl/src/populations/wilsoncowan.jl.
//
// Wilson-Cowan (1972) is a simplified rate model. Each "neuron"
// stores a real-valued activity x[i] and produces an instantaneous
// firing rate r[i] = tanh(x[i]).
//
// Julia reference (wilsoncowan.jl):
//   x[i] += dt * (-x[i] + g[i] + I[i])
//   r[i] = tanh(x[i])

///|
/// WilsonCowan parameters — just a marker struct for the population type.
pub(all) struct WCParameter {
  // No fields; tag type for the population.
  dummy : Float
}

///|
pub fn WCParameter::new() -> WCParameter {
  // WCParameter has no fields; just a tag for the population type.
  // Use a dummy Float to make the struct well-formed in MoonBit.
  { dummy: 0.0F }
}

///|
/// WilsonCowan population — a 1-D ODE per neuron with tanh output.
/// State variables:
///   - `x[i]`   : internal activity (Float)
///   - `r[i]`   : output firing rate (Float) = tanh(x[i])
///   - `g[i]`   : synaptic input from RateSynapse (Float)
///   - `I[i]`   : external input current (Float)
pub struct WilsonCowan {
  param : WCParameter
  n : Int
  x : Array[Float]
  r : Array[Float]
  g : Array[Float]
  i : Array[Float]
}

///|
/// Build a WilsonCowan population. Initial x is Normal(0, 0.5),
/// matching `0.5randn(N)` in the Julia source.
pub fn WilsonCowan::new(n : Int, rng : Xoshiro) -> WilsonCowan {
  let x : Array[Float] = Array::make(n, 0.0F)
  let r : Array[Float] = Array::make(n, 0.0F)
  for k in 0..<n {
    let (z1, _) = box_muller(rng)
    x[k] = 0.5F * Float::from_double(z1)
    r[k] = tanhf(x[k])
  }
  let g : Array[Float] = Array::make(n, 0.0F)
  let i : Array[Float] = Array::make(n, 0.0F)
  { param: WCParameter::new(), n, x, r, g, i }
}

///|
/// Integrate one Euler step. Resets `g` to 0 after using it (matches
/// Julia's `fill!(g, zero(eltype(g)))` which is uncommented in some
/// versions of RateSynapse's forward! — required to prevent g from
/// accumulating over time).
pub fn step_wc(p : WilsonCowan, dt : Float) -> Unit {
  let n = p.n
  for k in 0..<n {
    p.x[k] = p.x[k] + dt * (-p.x[k] + p.g[k] + p.i[k])
    p.r[k] = tanhf(p.x[k])
    p.g[k] = 0.0F
  }
}