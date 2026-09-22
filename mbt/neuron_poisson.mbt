// Poisson population neuron — bit-exact port of
// SNNModels.jl/src/populations/poisson.jl.
//
// Julia reference (homogeneous rate):
//   randcache[i] = rand()  for i in 1:N
//   fire[i] = randcache[i] < rate * dt
//
// Bit-exact note: Julia's `rand!` uses the default RNG (Xoshiro).
// We use `next_f32(rng)` from our Xoshiro stream to get Float32 values
// in [0, 1). The Float32 stream matches Julia's XoshiroFloat32, so
// the fire pattern is bit-exact with Julia's `rand(rng, Float32, N)`.

///|
/// PoissonParameter — homogeneous rate.
pub struct PoissonHomoParameter {
  rate : Float
}

///|
pub fn PoissonHomoParameter::new(rate : Float) -> PoissonHomoParameter {
  { rate }
}

///|
/// Poisson neuron population — each "neuron" fires according to a
/// homogeneous Poisson process.
pub struct Poisson {
  param : PoissonHomoParameter
  n : Int
  fire : Array[Bool]
  randcache : Array[Float]
  rng : Xoshiro
}

///|
/// Construct a new Poisson population with `n` neurons and `rate` (Hz).
pub fn Poisson::new(n : Int, rate : Float, rng : Xoshiro) -> Poisson {
  { param: PoissonHomoParameter::new(rate),
    n,
    fire: Array::make(n, false),
    randcache: Array::make(n, 0.0F),
    rng }
}

///|
/// Update the Poisson population state for one step.
/// Bit-exact port of `integrate!(p::Poisson, param, dt)`.
pub fn step_poisson(p : Poisson, dt : Float) -> Unit {
  let n = p.n
  let rate_dt = p.param.rate * dt
  for i in 0..<n {
    let u = next_f32(p.rng)
    p.randcache[i] = u
    p.fire[i] = u < rate_dt
  }
  ()
}
