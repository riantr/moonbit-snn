// siamese_trainer.mbt -- Siamese training loop (v0.132.0).
//
// Scope of v0.132.0:
//   - SiameseBatch: (anchors, positives, negatives) triplet batch.
//   - build_siamese_batch: construct a batch by sampling positives of
//     the same class and negatives of a different class.
//   - siamese_triplet_step: forward the batch, compute the mean triplet
//     loss, and return it.
//   - SiameseTrainer struct + one training iteration over a batch.
//   - recall_at_k: retrieval metric over a query/gallery/label split.
//
// Parameter updates are deferred to a follow-up batch, consistent with
// the forward-only pattern used across this project.
//
// Reference: FaceNet (Schroff et al. 2015); N-pair (Soh et al. 2016).

///|
/// SiameseBatch: a set of (anchor, positive, negative) triplets.
pub struct SiameseBatch {
  anchors : Array[Array[Float]]
  positives : Array[Array[Float]]
  negatives : Array[Array[Float]]
  size : Int
}

///|
/// Build a triplet batch. For each anchor at index `i`:
///   - the positive is another sample with the same label,
///   - the negative is a sample with a different label.
///
/// Sampling is deterministic given the RNG. Anchors whose class has
/// fewer than two members are skipped (they have no valid positive).
pub fn build_siamese_batch(
  samples : Array[Array[Float]],
  labels : Array[Int],
  batch_size : Int,
  rng : Xoshiro,
) -> SiameseBatch {
  let n = samples.length()
  let anchors : Array[Array[Float]] = Array::make(batch_size, Array::make(0, 0.0F))
  let positives : Array[Array[Float]] = Array::make(batch_size, Array::make(0, 0.0F))
  let negatives : Array[Array[Float]] = Array::make(batch_size, Array::make(0, 0.0F))
  let n_u64 = n.to_uint64()
  let mut filled = 0
  let mut attempts = 0
  let max_attempts = batch_size * 20
  while filled < batch_size && attempts < max_attempts {
    attempts = attempts + 1
    if n == 0 {
      break
    }
    let ai = (next_u64(rng) % n_u64).to_int()
    let label = labels[ai]
    // Find a positive with the same label.
    let pi = (next_u64(rng) % n_u64).to_int()
    if pi == ai || labels[pi] != label {
      continue
    }
    // Find a negative with a different label.
    let ni = (next_u64(rng) % n_u64).to_int()
    if labels[ni] == label {
      continue
    }
    anchors[filled] = samples[ai]
    positives[filled] = samples[pi]
    negatives[filled] = samples[ni]
    filled = filled + 1
  }
  { anchors, positives, negatives, size: filled }
}

///|
/// Forward a triplet batch through the shared weights and return the
/// mean triplet loss plus the mean positive / negative squared
/// distances (useful for monitoring whether the embedding is actually
/// separating the classes).
pub fn siamese_triplet_step(
  s : SiameseNet,
  batch : SiameseBatch,
) -> (Float, Float, Float) {
  if batch.size == 0 {
    return (0.0F, 0.0F, 0.0F)
  }
  let mut loss_sum = 0.0F
  let mut pos_sum = 0.0F
  let mut neg_sum = 0.0F
  for i in 0..<batch.size {
    let (ea, ep, en) = siamese_embed_triplet(
      s, batch.anchors[i], batch.positives[i], batch.negatives[i],
    )
    loss_sum = loss_sum + triplet_loss(ea, ep, en, s.margin)
    pos_sum = pos_sum + pairwise_squared_distance(ea, ep)
    neg_sum = neg_sum + pairwise_squared_distance(ea, en)
  }
  let n = Float::from_int(batch.size)
  (loss_sum / n, pos_sum / n, neg_sum / n)
}

///|
/// SiameseTrainer: wraps a SiameseNet with training hyperparameters.
pub struct SiameseTrainer {
  s : SiameseNet
  lr : Float
  batch_size : Int
}

///|
/// Build a fresh SiameseTrainer.
pub fn SiameseTrainer::new(
  s : SiameseNet,
  lr : Float,
  batch_size : Int,
) -> SiameseTrainer {
  { s, lr, batch_size }
}

///|
/// One training iteration: build a batch from the pool and evaluate the
/// triplet objective. Returns (loss, mean_pos_d2, mean_neg_d2).
pub fn siamese_train_step(
  trainer : SiameseTrainer,
  samples : Array[Array[Float]],
  labels : Array[Int],
  rng : Xoshiro,
) -> (Float, Float, Float) {
  let batch = build_siamese_batch(
    samples, labels, trainer.batch_size, rng,
  )
  let (loss, pd, nd) = siamese_triplet_step(trainer.s, batch)
  let _ = trainer.lr
  (loss, pd, nd)
}

///|
/// Recall@K over an embedding set with known labels. For each query,
/// count how many of the K nearest gallery items share its label. This
/// is the standard retrieval metric for face / image retrieval.
pub fn recall_at_k(
  s : SiameseNet,
  queries : Array[Array[Float]],
  gallery : Array[Array[Float]],
  labels : Array[Int],
  k : Int,
) -> Float {
  let nq = queries.length()
  let ng = gallery.length()
  if nq == 0 || ng == 0 {
    return 0.0F
  }
  let kk = if k > ng { ng } else { k }
  // Pre-embed the gallery.
  let gallery_emb : Array[Array[Float]] = Array::make(
    ng, Array::make(0, 0.0F),
  )
  for j in 0..<ng {
    gallery_emb[j] = siamese_embed(s, gallery[j])
  }
  let mut hits = 0.0F
  let mut total = 0.0F
  for i in 0..<nq {
    let eq = siamese_embed(s, queries[i])
    let label = labels[i]
    // Find the K nearest gallery items by squared distance.
    let best_d : Array[Float] = Array::make(ng, 0.0F)
    for j in 0..<ng {
      best_d[j] = pairwise_squared_distance(eq, gallery_emb[j])
    }
    for _ in 0..<kk {
      // Find the argmin among not-yet-selected items.
      let mut best_idx = -1
      let mut best_val = 0.0F
      for j in 0..<ng {
        if best_idx < 0 || best_d[j] < best_val {
          best_val = best_d[j]
          best_idx = j
        }
        let _ = j
      }
      if best_idx >= 0 {
        if labels[best_idx] == label {
          hits = hits + 1.0F
        }
        // Mark as selected by setting its distance to +inf.
        best_d[best_idx] = 1.0e30F
        total = total + 1.0F
      }
    }
  }
  if total == 0.0F {
    0.0F
  } else {
    hits / total
  }
}