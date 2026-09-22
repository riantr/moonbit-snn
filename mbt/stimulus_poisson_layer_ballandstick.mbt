// stimulus_poisson_layer_ballandstick.mbt — PoissonLayer Stimulus
// targeting BallAndStick compartments (:soma or :d).
//
// Mirrors PoissonLayerStimulusTripod (v0.10.27) but for BallAndStick
// (soma + 1 dendrite). Used by stimuli.jl which routes Poisson inputs
// to both :d1 (Tripod dendrite) and :d (BallAndStick dendrite).
//
// Wiring:
//   for each pre-neuron i:
//     draw k ~ Poisson(rate * dt)
//     if k > 0:
//       for each post-neuron j where weights[j, i] is connected:
//         post.glu_d[j] += weights[j, i]   (or post.glu_s)

///|
/// PoissonLayerStimulusBallAndStick — PoissonLayer + sparse weights
/// targeting a BallAndStick compartment buffer.
pub struct PoissonLayerStimulusBallAndStick {
  param : PoissonLayer
  post : BallAndStick
  weights : Array[Float]
  connectivity : Array[Bool]
  target_compartment : String  // "soma" or "d"
  target_kind : String         // "glu" or "gaba"
  rng : Xoshiro
}

///|
/// Construct a PoissonLayerStimulusBallAndStick. Draws per-(pre, post)
/// sparse weights at construction.
pub fn PoissonLayerStimulusBallAndStick::new(
  param : PoissonLayer,
  post : BallAndStick,
  target_compartment : String,
  target_kind : String,
  mu : Float,
  sigma : Float,
  p_conn : Float,
  rng : Xoshiro,
) -> PoissonLayerStimulusBallAndStick {
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
/// Pick the right buffer for (target_compartment, target_kind).
fn target_buffer_ball(s : PoissonLayerStimulusBallAndStick) -> Array[Float] {
  match (s.target_compartment, s.target_kind) {
    ("soma", "glu") => s.post.glu_s
    ("soma", _) => s.post.gaba_s
    ("d", "glu") => s.post.glu_d
    ("d", _) => s.post.gaba_d
    _ => s.post.glu_s
  }
}

///|
/// One-step stimulation: draw Poisson for each active source, and
/// if it fires, add the (pre, post) weight to the post-synaptic
/// compartment buffer.
pub fn stimulate_layer_ball(
  s : PoissonLayerStimulusBallAndStick,
  time : Float,
  dt : Float,
) -> Unit {
  let _ = time
  let n_pre = s.param.n_sources
  let n_post = s.post.n
  let lambda = s.param.rate * dt
  let buf = target_buffer_ball(s)
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