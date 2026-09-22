// PoissonStimulus — port of SNNModels.jl/src/stimuli/poisson.jl
//
// A Poisson stimulus adds `μ * k` to a post-synaptic neuron's
// conductance per step, where `k ~ Poisson(λ)` and `λ = rate * dt`.
// The `rate` can be fixed (a single Float), per-neuron (a vector),
// or time-varying (a function over `time`).
//
// Bit-exact note: the Poisson sampler here uses Knuth's algorithm with
// our Xoshiro RNG. Julia's `Distributions.Poisson{Float32}(λ).rand`
// uses its own implementation; sequences will NOT be bit-exact for
// any given Float32 λ. The expected value (`μ * E[k] = μ * λ`) and
// the qualitative dynamics are matched.

///|
/// Fixed-rate Poisson stimulus parameter.
pub struct PoissonFixed {
  rate : Float
  mut mu : Float
  active : Array[Bool]
}

///|
pub fn PoissonFixed::new(rate : Float) -> PoissonFixed {
  { rate, mu: 1.0F, active: [true] }
}

///|
/// Set the per-event amplitude μ. Used by Julia-style constructors
/// that pass `μ = 1.0f0` etc.
pub fn PoissonFixed::set_mu(p : PoissonFixed, mu : Float) -> Unit {
  p.mu = mu
}

///|
/// Per-step Poisson sampling with Knuth's algorithm. For each
/// iteration we multiply `p` by a uniform draw and check whether
/// `p` has dropped below `exp(-λ)`. The number of rands consumed
/// before the drop is the Poisson(λ) sample.
///
/// Standard form:
///   p = 1
///   k = 0
///   repeat:
///     k += 1
///     p *= rand()
///   until p < exp(-λ)
///   return k - 1
///
/// The `k - 1` corrects for the off-by-one in the iteration count
/// (k counts the number of rands drawn; the number of rands strictly
/// greater than the threshold is k - 1).
///
/// Safety: capped at `max(100, ceil(10*λ))` iterations to prevent
/// runaway loops in pathological cases.
pub fn sample_poisson(rng : Xoshiro, lambda : Float) -> Int {
  if lambda <= 0.0F {
    return 0
  }
  let l : Double = expf(-lambda).to_double()
  let mut p : Double = 1.0
  let mut k = 0
  let cap : Int = {
    let ten_lam = (lambda * 10.0F).to_int()
    if ten_lam > 100 { ten_lam } else { 100 }
  }
  while true {
    k = k + 1
    p = p * next_f64(rng)
    if p < l {
      return k - 1
    }
    if k > cap {
      return k - 1
    }
  }
  k - 1
}

///|
/// A Poisson stimulus targeting a list of neurons on a post-synaptic
/// population. The `g` array is the receptor (e.g. `glu` or `gaba`).
pub struct PoissonStimulusIF {
  param : PoissonFixed
  neurons : Array[Int]
  g : Array[Float]
  rng : Xoshiro
}

///|
/// Construct a Poisson stimulus targeting all neurons of a population.
/// `g` is the post-synaptic receptor (`glu` for :ge, `gaba` for :gi).
pub fn PoissonStimulusIF::new(
  pop : IF,
  sym : String,
  rate : Float,
  rng : Xoshiro,
) -> PoissonStimulusIF {
  let n = pop.n
  let neurons : Array[Int] = []
  for k in 0..<n {
    neurons.push(k)
  }
  let g = if sym == "ge" { pop.glu } else { pop.gaba }
  { param: PoissonFixed::new(rate), neurons, g, rng }
}

///|
/// Apply the Poisson stimulus for one time step.
/// `time` is the current simulation time (ms), unused for `PoissonFixed`
/// but kept for compatibility with `PoissonInterval` / `PoissonVariable`.
pub fn stimulate_if(
  s : PoissonStimulusIF,
  time : Float,
  dt : Float,
) -> Unit {
  if !s.param.active[0] {
    return
  }
  let lambda = s.param.rate * dt
  if lambda <= 0.0F {
    return
  }
  for n in s.neurons {
    let k = sample_poisson(s.rng, lambda)
    if k > 0 {
      s.g[n] = s.g[n] + s.param.mu * Float::from_int(k)
    }
  }
}

///|
/// Ornstein-Uhlenbeck process step. Returns the new state X(t+dt).
///
///   X(t+dt) = X(t) + θ * (μ - X(t)) * dt + σ * ξ * sqrt(dt)
///
/// where ξ ~ N(0, 1) is drawn via Box-Muller from the supplied
/// Xoshiro RNG. Mirrors Julia's `OrnsteinUhlenbeckProcess(x, param)`
/// used in Lagzi2022 Assembly Formation experiments.
///
/// Result is clamped at X >= 0 (rates must be non-negative).
pub fn ornstein_uhlenbeck_step(
  rng : Xoshiro,
  x : Float,
  theta : Float,
  mu : Float,
  sigma : Float,
  dt : Float,
) -> Float {
  // Box-Muller: one N(0, 1) sample.
  let (z1, _) = box_muller(rng)
  let next = x + theta * (mu - x) * dt + sigma * z1.to_float() * Float::sqrt(dt)
  if next > 0.0F {
    next
  } else {
    0.0F
  }
}
