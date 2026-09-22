// stimulus_poisson_layer_tripod.mbt — PoissonLayer Stimulus targeting
// TripodHet compartments (:soma, :d1, or :d2).
//
// Mirrors SNNModels.jl's `Stimulus(poisson_layer, E, :glu, :d1,
// conn=(μ, ρ))` pattern. Uses the PoissonLayer parameter from
// v0.10.21 but writes to TripodHet's compartment-specific glu/gaba
// buffer instead of an IF population's glu/gaba.
//
// Wiring:
//   for each pre-neuron i:
//     draw k ~ Poisson(rate * dt)
//     if k > 0:
//       for each post-neuron j where weights[j, i] is connected:
//         post.glu_d1[j] += weights[j, i]   (or :soma / :d2)
//   target_compartment ∈ {"soma", "d1", "d2"}
//   target_kind ∈ {"glu", "gaba"} (excitatory vs inhibitory)

///|
/// PoissonLayerStimulusTripod — wraps a PoissonLayer + per-(pre, post)
/// sparse weights to deliver Poisson spikes to a specific TripodHet
/// compartment buffer.
pub struct PoissonLayerStimulusTripod {
  param : PoissonLayer
  post : TripodHet
  weights : Array[Float]        // row-major [post.N, param.n_sources]
  connectivity : Array[Bool]   // row-major [post.N, param.n_sources]
  target_compartment : String  // "soma" | "d1" | "d2"
  target_kind : String         // "glu" | "gaba"
  rng : Xoshiro
}

///|
/// Construct a PoissonLayerStimulusTripod. Draws per-(pre, post)
/// weights at construction: with probability `p_conn` the
/// connection exists, weight ~ Normal(μ, σ) (Fixed if dist=="Fixed").
pub fn PoissonLayerStimulusTripod::new(
  param : PoissonLayer,
  post : TripodHet,
  target_compartment : String,
  target_kind : String,
  mu : Float,
  sigma : Float,
  p_conn : Float,
  rng : Xoshiro,
) -> PoissonLayerStimulusTripod {
  let n_pre = param.n_sources
  let n_post = post.n
  let weights : Array[Float] = Array::make(n_post * n_pre, 0.0F)
  let connectivity : Array[Bool] = Array::make(n_post * n_pre, false)
  let use_normal = param.dist == "Normal"
  for i in 0..<n_pre {
    for j in 0..<n_post {
      let idx = j * n_pre + i
      if next_f32(rng) < p_conn {
        connectivity[idx] = true
        weights[idx] = if use_normal {
          mu + next_f32(rng) * sigma
        } else {
          mu
        }
      }
    }
  }
  { param, post, weights, connectivity, target_compartment, target_kind, rng }
}

///|
/// Pick the right buffer for the configured (target_compartment, target_kind).
fn target_buffer_tripod(s : PoissonLayerStimulusTripod) -> Array[Float] {
  match (s.target_compartment, s.target_kind) {
    ("soma", "glu") => s.post.glu_s
    ("soma", _) => s.post.gaba_s
    ("d1", "glu") => s.post.glu_d1
    ("d1", _) => s.post.gaba_d1
    ("d2", "glu") => s.post.glu_d2
    ("d2", _) => s.post.gaba_d2
    _ => s.post.glu_d1
  }
}

///|
/// One-step stimulation: draw Poisson for each active source, and
/// if it fires, add the (pre, post) weight to the post-synaptic
/// compartment buffer (glu or gaba, :soma/:d1/:d2).
pub fn stimulate_layer_tripod(
  s : PoissonLayerStimulusTripod,
  time : Float,
  dt : Float,
) -> Unit {
  let _ = time
  let n_pre = s.param.n_sources
  let n_post = s.post.n
  let lambda = s.param.rate * dt
  let buf = target_buffer_tripod(s)
  if lambda <= 0.0F {
    return
  }
  for i in 0..<n_pre {
    if !s.param.active[i] {
      continue
    }
    let k = sample_poisson(s.rng, lambda)
    if k > 0 {
      for j in 0..<n_post {
        let idx = j * n_pre + i
        if s.connectivity[idx] {
          buf[j] = buf[j] + s.weights[idx]
        }
      }
    }
  }
}