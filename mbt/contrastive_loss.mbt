// contrastive_loss.mbt -- Metric learning losses (v0.130.0).
//
// Three canonical embedding-space losses:
//
// 1. Contrastive loss (Bromley et al. 1993; Hadsell et al. 2006):
//        L = (1 - y) * d^2 + y * max(0, margin^2 - d^2)
//    where y = 1 for a dissimilar pair and y = 0 for a similar pair,
//    and d^2 is the squared distance between the two embeddings.
//
// 2. Triplet loss (FaceNet, Schroff et al. 2015):
//        L = max(0, d(a, p)^2 - d(a, n)^2 + margin)
//    for an anchor a, a positive p (same class) and a negative n.
//
// 3. N-pair loss (Soh et al. 2016): a softmax cross-entropy over the
//    similarity matrix, which is numerically better behaved than
//    triplet loss when mining hard negatives.
//
// Scope of v0.130.0:
//   - contrastive_pair_loss
//   - triplet_loss
//   - n_pair_loss
//   - hard_negative_triplet_loss (max over candidate negatives)
//
// Reference: Bromley 1993; Hadsell 2006; FaceNet 2015; N-pair 2016.

///|
/// Contrastive loss on one pair of embeddings. `similar` is 1.0 for a
/// same-class pair (should be close) and 0.0 for a different-class pair
/// (should be far).
pub fn contrastive_pair_loss(
  a : Array[Float],
  b : Array[Float],
  similar : Float,
  margin : Float,
) -> Float {
  let d2 = pairwise_squared_distance(a, b)
  if similar > 0.5F {
    // Dissimilar pair: push apart until d^2 > margin^2.
    let m2 = margin * margin
    if d2 < m2 {
      m2 - d2
    } else {
      0.0F
    }
  } else {
    // Similar pair: pull together.
    d2
  }
}

///|
/// Triplet loss: max(0, d(a,p)^2 - d(a,n)^2 + margin).
pub fn triplet_loss(
  anchor : Array[Float],
  positive : Array[Float],
  negative : Array[Float],
  margin : Float,
) -> Float {
  let d_pos = pairwise_squared_distance(anchor, positive)
  let d_neg = pairwise_squared_distance(anchor, negative)
  let l = d_pos - d_neg + margin
  if l > 0.0F {
    l
  } else {
    0.0F
  }
}

///|
/// Hard-negative triplet loss: take the maximum loss over a set of
/// candidate negatives. This is online hard-negative mining (FaceNet
/// section 3.1) and is what makes triplet training work in practice.
pub fn hard_negative_triplet_loss(
  anchor : Array[Float],
  positive : Array[Float],
  negatives : Array[Array[Float]],
  margin : Float,
) -> Float {
  let m = negatives.length()
  if m == 0 {
    return 0.0F
  }
  let mut worst = 0.0F
  for i in 0..<m {
    let l = triplet_loss(anchor, positive, negatives[i], margin)
    if l > worst {
      worst = l
    }
  }
  worst
}

///|
/// N-pair loss (Soh et al. 2016). Given an anchor and `num_classes`
/// positive/negative pairs (one positive + one negative per class),
/// computes the softmax cross-entropy over the similarity logits:
///
///   logits_j = -2 * ||a - p_j||^2 - ||a - n_j||^2
///   L = -log( softmax(logits)_correct )
///
/// Negative similarity logits are offset by a constant (the paper uses
/// 0.1) for numerical stability, which is a known wart of the N-pair
/// formulation.
pub fn n_pair_loss(
  anchor : Array[Float],
  positives : Array[Array[Float]],
  negatives : Array[Array[Float]],
) -> Float {
  let k = positives.length()
  if k == 0 {
    return 0.0F
  }
  let logits : Array[Float] = Array::make(k, 0.0F)
  let mut m = 0.0F
  for j in 0..<k {
    let d_pos = pairwise_squared_distance(anchor, positives[j])
    let d_neg = pairwise_squared_distance(anchor, negatives[j])
    let s = -2.0F * d_pos - d_neg
    logits[j] = s
    if s > m {
      m = s
    }
  }
  // Stable softmax; the correct class is index 0.
  let mut sum_exp = 0.0F
  for j in 0..<k {
    sum_exp = sum_exp + expf(logits[j] - m)
  }
  // L = log(sum_exp) - logits[0] (since the max cancels).
  logf(sum_exp) - (logits[0] - m)
}