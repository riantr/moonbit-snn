// capsule_network.mbt -- CapsuleNetwork: full CapsNet (v0.136.0).
//
// The full two-layer CapsNet:
//
//   x [c_in, h, w]
//   -> ReLU convolution (c_in -> ch, KxK, stride 1)          [c1_h, c1_w]
//   -> PrimaryCapsule (ch -> c_base*cd, K, K)                [n_patches x cd]
//   -> dynamic routing over the patches                       [n_classes x cd]
//
// The class prediction is the argmax over the length (existence
// probability) of each class capsule; the pose vector is discarded
// for classification.
//
// Reference: Sabour et al. 2017; Hinton et al. 2018.

///|
/// CapsuleNetwork: a PrimaryCapsule bank + a digit-capsule layer
/// connected by dynamic routing.
pub struct CapsuleNetwork {
  c_in : Int
  in_h : Int
  in_w : Int
  ch : Int
  conv_k : Int
  c1_h : Int
  c1_w : Int
  primary : PrimaryCapsule
  n_low : Int
  n_classes : Int
  capsule_dim : Int
  n_routing : Int
  // ReLU conv before the primary capsules: (ch rows x c_in*conv_k*conv_k).
  conv_w : Array[Array[Float]]
  conv_b : Array[Float]
  // Routing transformation W: n_classes rows of (n_low * capsule_dim).
  route_w : Array[Array[Float]]
}

///|
/// Build a fresh CapsuleNetwork.
pub fn CapsuleNetwork::new(
  c_in : Int,
  in_h : Int,
  in_w : Int,
  ch : Int,
  conv_k : Int,
  c_base : Int,
  capsule_dim : Int,
  n_classes : Int,
  n_routing : Int,
  seed : UInt64,
) -> CapsuleNetwork {
  // ReLU convolution.
  let conv_in = c_in * conv_k * conv_k
  let conv_w : Array[Array[Float]] = Array::make(
    ch, Array::make(conv_in, 0.0F),
  )
  let std_c = sqrtf(2.0F / Float::from_int(conv_in))
  let rng_c = Xoshiro::from_state(
    seed + 1UL, seed + 2UL, seed + 3UL, seed + 4UL,
  )
  for o in 0..<ch {
    for i in 0..<conv_in {
      conv_w[o][i] = std_c * (next_f32(rng_c) * 2.0F - 1.0F)
    }
  }
  let conv_b : Array[Float] = Array::make(ch, 0.0F)
  let c1_h = in_h - conv_k + 1
  let c1_w = in_w - conv_k + 1
  // Primary capsules: kernel == stride, so patches do not overlap.
  let primary = PrimaryCapsule::new(
    ch, c_base, capsule_dim, conv_k, conv_k, c1_h, c1_w, seed,
  )
  let n_low = c_base * primary.n_patches
  // Routing transformation.
  let rdim = n_low * capsule_dim
  let route_w : Array[Array[Float]] = Array::make(
    n_classes, Array::make(rdim, 0.0F),
  )
  let std_r = sqrtf(2.0F / Float::from_int(rdim))
  let rng_r = Xoshiro::from_state(
    seed + 21UL, seed + 22UL, seed + 23UL, seed + 24UL,
  )
  for j in 0..<n_classes {
    for i in 0..<rdim {
      route_w[j][i] = std_r * (next_f32(rng_r) * 2.0F - 1.0F)
    }
  }
  { c_in, in_h, in_w, ch, conv_k, c1_h, c1_w, primary, n_low, n_classes, capsule_dim, n_routing, conv_w, conv_b, route_w }
}

///|
/// Forward pass: image [c_in, in_h, in_w] -> flat row-major
/// [n_classes x capsule_dim] class-capsule poses.
pub fn capsule_network_forward(
  net : CapsuleNetwork,
  image : Array[Float],
) -> Array[Float] {
  // 1. ReLU convolution.
  let k = net.conv_k
  let chn = net.ch
  let c1 : Array[Float] = Array::make(chn * net.c1_h * net.c1_w, 0.0F)
  for o in 0..<chn {
    let dst = o * net.c1_h * net.c1_w
    for y in 0..<net.c1_h {
      for x in 0..<net.c1_w {
        let mut acc = net.conv_b[o]
        for c in 0..<net.c_in {
          for ky in 0..<k {
            for kx in 0..<k {
              let iy = y + ky
              let ix = x + kx
              let ki = (c * k + ky) * k + kx
              acc = acc + net.conv_w[o][ki] * image[
                c * net.in_h * net.in_w + iy * net.in_w + ix
              ]
            }
          }
        }
        // ReLU.
        c1[dst + y * net.c1_w + x] = if acc > 0.0F { acc } else { 0.0F }
      }
    }
  }
  // 2. Primary capsules: [n_patches * c_base x capsule_dim].
  let low = primary_capsule_forward(net.primary, c1)
  // 3. Dynamic routing to the class capsules.
  dynamic_routing(
    low, net.n_low, net.route_w, net.n_classes, net.capsule_dim, net.n_routing,
  )
}

///|
/// Class probabilities from the class-capsule outputs: the norm of each
/// capsule is the existence probability.
pub fn capsule_network_probs(
  net : CapsuleNetwork,
  image : Array[Float],
) -> Array[Float] {
  let caps = capsule_network_forward(net, image)
  let out : Array[Float] = Array::make(net.n_classes, 0.0F)
  for k in 0..<net.n_classes {
    let base = k * net.capsule_dim
    let mut sq = 0.0F
    for d in 0..<net.capsule_dim {
      sq = sq + caps[base + d] * caps[base + d]
    }
    out[k] = sqrtf(sq)
  }
  out
}

///|
/// Predicted class index: the argmax over the existence probabilities.
pub fn capsule_network_classify(
  net : CapsuleNetwork,
  image : Array[Float],
) -> Int {
  let probs = capsule_network_probs(net, image)
  let mut best = 0
  let mut best_val = probs[0]
  for k in 1..<net.n_classes {
    if probs[k] > best_val {
      best_val = probs[k]
      best = k
    }
  }
  best
}

///|
/// CapsuleNetworkTrainer: pairs a CapsuleNetwork with training
/// hyperparameters.
pub struct CapsuleNetworkTrainer {
  net : CapsuleNetwork
  lr : Float
  m_pos : Float
  m_neg : Float
  absent_weight : Float
}

///|
/// Build a fresh CapsuleNetworkTrainer. The default margins are the
/// paper's m+ = 0.9, m- = 0.1, absent weight = 0.9.
pub fn CapsuleNetworkTrainer::new(
  net : CapsuleNetwork,
  lr : Float,
  m_pos : Float,
  m_neg : Float,
  absent_weight : Float,
) -> CapsuleNetworkTrainer {
  { net, lr, m_pos, m_neg, absent_weight }
}

///|
/// One training step: forward pass + margin loss. Returns
/// (loss, predicted_class). Parameter updates are deferred to a
/// follow-up batch, consistent with the forward-only pattern.
pub fn capsule_train_step(
  trainer : CapsuleNetworkTrainer,
  image : Array[Float],
  target : Int,
) -> (Float, Int) {
  let caps = capsule_network_forward(trainer.net, image)
  let loss = capsule_margin_loss(
    caps,
    trainer.net.n_classes,
    trainer.net.capsule_dim,
    target,
    trainer.m_pos,
    trainer.m_neg,
    trainer.absent_weight,
  )
  let _ = trainer.lr
  let probs = capsule_network_probs(trainer.net, image)
  let mut best = 0
  let mut best_val = probs[0]
  for k in 1..<trainer.net.n_classes {
    if probs[k] > best_val {
      best_val = probs[k]
      best = k
    }
  }
  (loss, best)
}

///|
/// Classification accuracy over a batch.
pub fn capsule_eval_accuracy(
  trainer : CapsuleNetworkTrainer,
  batch : Array[Array[Float]],
  targets : Array[Int],
) -> Float {
  let m = batch.length()
  if m == 0 {
    return 0.0F
  }
  let mut correct = 0.0F
  for i in 0..<m {
    let (_loss, pred) = capsule_train_step(trainer, batch[i], targets[i])
    if pred == targets[i] {
      correct = correct + 1.0F
    }
  }
  correct / Float::from_int(m)
}

///|
/// Count trainable scalars across the conv, the primary capsules and
/// the routing transformation.
pub fn capsule_network_num_params(net : CapsuleNetwork) -> Int {
  let mut total = 0
  total = total + net.conv_w.length() * net.conv_w[0].length()
  total = total + net.conv_b.length()
  total = total + primary_capsule_num_params(net.primary)
  total = total + net.route_w.length() * net.route_w[0].length()
  total
}