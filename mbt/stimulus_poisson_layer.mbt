// PoissonLayer — port of SNNModels.jl/src/stimuli/poisson_layer.jl
//
// A PoissonLayer is a population of `n_sources` independent Poisson
// sources (each firing at `rate` Hz) projecting sparsely onto a target
// IF population. Each source i ∈ [0, n_sources) connects to each
// post-neuron j with probability `p`, with weight drawn from Normal(μ, σ)
// if `dist == "Normal"` or Fixed(μ) if `dist == "Fixed"`.
//
// At each step:
//   for i in [0, n_sources):
//     if active[i]:
//       draw k ~ Poisson(rate * dt)
//       if k > 0:
//         fire[i] = true
//         for j in [0, post.N) where connectivity[j, i]:
//           post.glu[j] += weights[j, i]   (or .gaba for :gi)
//
// Mirrors Julia's `PoissonLayer` parameter + `Stimulus` wrapper.
//
// Bit-exact note: per-step Poisson sample sequences differ from Julia's
// Distributions.jl (we use Knuth + Xoshiro Float64 internal; Julia uses
// Float32-specific RNGs). Expected per-step conductance (μ * rate * dt)
// and qualitative dynamics match.

///|
/// PoissonLayer parameter — a population of `n_sources` independent
/// Poisson sources with shared rate, drawn from optional Normal/Fixed
/// connection weights. Mirrors Julia's `PoissonLayer` struct.
pub(all) struct PoissonLayer {
  rate : Float
  n_sources : Int
  mut active : Array[Bool]
  mu : Float
  sigma : Float
  p : Float
  dist : String
  rule : String
}

///|
/// Default PoissonLayer: rate in Hz, n_sources=1, fully active,
/// μ=1.0, σ=0.0, p=1.0, Fixed weight, Fixed (no-plasticity) rule.
pub fn PoissonLayer::new(rate : Float) -> PoissonLayer {
  { rate, n_sources: 1, active: [true], mu: 1.0F, sigma: 0.0F, p: 1.0F,
    dist: "Fixed", rule: "Fixed" }
}

///|
/// PoissonLayer with `n_sources` sources, all initially active.
pub fn PoissonLayer::with_n(rate : Float, n_sources : Int) -> PoissonLayer {
  let active : Array[Bool] = []
  for _ in 0..<n_sources {
    active.push(true)
  }
  { rate, n_sources, active, mu: 1.0F, sigma: 0.0F, p: 1.0F,
    dist: "Fixed", rule: "Fixed" }
}

///|
/// PoissonLayer with explicit active flags (Julia: active = [false]).
pub fn PoissonLayer::with_active(
  rate : Float,
  n_sources : Int,
  active : Array[Bool],
) -> PoissonLayer {
  { rate, n_sources, active, mu: 1.0F, sigma: 0.0F, p: 1.0F,
    dist: "Fixed", rule: "Fixed" }
}

///|
/// PoissonLayer with full connection parameters (Julia: conn = (p, μ, σ,
/// dist, rule)).
pub fn PoissonLayer::with_conn(
  rate : Float,
  n_sources : Int,
  active : Array[Bool],
  mu : Float,
  sigma : Float,
  p : Float,
  dist : String,
  rule : String,
) -> PoissonLayer {
  { rate, n_sources, active, mu, sigma, p, dist, rule }
}

///|
/// PoissonLayerStimulus — wraps a PoissonLayer with a target IF
/// population and the per-(pre, post) connection weights drawn at
/// construction time. Sym is "ge" or "gi" (which receptor to write).
pub struct PoissonLayerStimulus {
  param : PoissonLayer
  post : IF
  sym : String
  fire : Array[Bool]
  weights : Array[Float]
  connectivity : Array[Bool]
  rng : Xoshiro
}

///|
/// Construct a PoissonLayerStimulus. Draws each connection weight
/// independently: with probability `p` the connection exists, and
/// if it does, weight is drawn Normal(μ, σ) or Fixed(μ) per `dist`.
/// `weights` and `connectivity` are stored row-major with shape
/// `[post.N, param.n_sources]` (weights[j, i] = weight from source i
/// to neuron j).
pub fn PoissonLayerStimulus::new(
  param : PoissonLayer,
  post : IF,
  sym : String,
  rng : Xoshiro,
) -> PoissonLayerStimulus {
  let n_pre = param.n_sources
  let n_post = post.n
  let fire : Array[Bool] = Array::make(n_pre, false)
  let weights : Array[Float] = Array::make(n_post * n_pre, 0.0F)
  let connectivity : Array[Bool] = Array::make(n_post * n_pre, false)
  let p_conn = param.p
  let mu = param.mu
  let sigma = param.sigma
  let use_normal = param.dist == "Normal"
  for i in 0..<n_pre {
    for j in 0..<n_post {
      let idx = j * n_pre + i
      if next_f32(rng) < p_conn {
        connectivity[idx] = true
        weights[idx] = if use_normal {
          // Box-Muller: draw N(0, 1), scale to N(μ, σ).
          let (z, _) = box_muller(rng)
          mu + Float::from_double(z) * sigma
        } else {
          mu
        }
      }
    }
  }
  { param, post, sym, fire, weights, connectivity, rng }
}

///|
/// One-step stimulation: draw Poisson for each active source, and
/// if it fires, add the (pre, post) weight to the post-synaptic
/// receptor (glu for "ge", gaba for "gi").
///
/// `time` is the current simulation time (ms); unused for fixed-rate
/// Poisson but kept for API symmetry with time-varying variants.
pub fn stimulate_layer(
  s : PoissonLayerStimulus,
  time : Float,
  dt : Float,
) -> Unit {
  let _ = time  // unused for fixed-rate; suppress warning
  let n_pre = s.param.n_sources
  let n_post = s.post.n
  let lambda = s.param.rate * dt
  // Reset fire buffer (single-step firing record).
  for i in 0..<n_pre {
    s.fire[i] = false
  }
  if lambda <= 0.0F {
    return
  }
  let g_target = if s.sym == "ge" { s.post.glu } else { s.post.gaba }
  for i in 0..<n_pre {
    if !s.param.active[i] {
      continue
    }
    let k = sample_poisson(s.rng, lambda)
    if k > 0 {
      s.fire[i] = true
      for j in 0..<n_post {
        let idx = j * n_pre + i
        if s.connectivity[idx] {
          g_target[j] = g_target[j] + s.weights[idx]
        }
      }
    }
  }
}
