// classifiers.mbt — classification helpers (v0.42.0).
//
// Standalone classification utilities that don't depend on a specific
// model architecture. Useful for any supervised learning pipeline:
//   - `argmax` for prediction
//   - `softmax` for probability conversion
//   - `classification_accuracy` for evaluation
//   - `top_k` for top-k retrieval
//
// `dqn.mbt::q_argmax` already implements the same argmax logic for RL;
// we provide a generalised version here so the classifier code path
// stays out of RL modules.

///|
/// Index of the maximum element in `scores`. First occurrence wins on
/// ties. Returns 0 for an empty array.
pub fn argmax(scores : Array[Float]) -> Int {
  let n = scores.length()
  if n == 0 {
    return 0
  }
  let mut best = 0
  let mut best_v = scores[0]
  for i in 1..<n {
    if scores[i] > best_v {
      best_v = scores[i]
      best = i
    }
  }
  best
}

///|
/// Softmax: `out[i] = exp(scores[i]) / Σ exp(scores[j])`. Numerically
/// stable — subtracts the max score before exponentiating to avoid
/// overflow on large inputs.
pub fn softmax(scores : Array[Float]) -> Array[Float] {
  let n = scores.length()
  let out : Array[Float] = Array::make(n, 0.0F)
  if n == 0 {
    return out
  }
  // Find max for numerical stability.
  let mut m = scores[0]
  for i in 1..<n {
    if scores[i] > m {
      m = scores[i]
    }
  }
  // Compute exp(scores[i] - m) and accumulate sum.
  let mut sum : Float = 0.0F
  for i in 0..<n {
    let e = @math.exp((scores[i] - m).to_double())
    let f = e.to_float()
    out[i] = f
    sum = sum + f
  }
  // Normalise.
  if sum > 0.0F {
    let inv = 1.0F / sum
    for i in 0..<n {
      out[i] = out[i] * inv
    }
  }
  out
}

///|
/// Top-k indices from `scores`, sorted by score in descending order.
/// Returns the first k (or fewer if scores has fewer than k elements).
/// Ties: insertion order wins (stable).
pub fn top_k(scores : Array[Float], k : Int) -> Array[Int] {
  let n = scores.length()
  let take = if k < n { k } else { n }
  // Build (score, index) pairs and sort by score descending.
  let pairs : Array[(Float, Int)] = []
  for i in 0..<n {
    pairs.push((scores[i], i))
  }
  // Simple insertion sort (stable; n is typically small for k).
  let mut i = 1
  while i < pairs.length() {
    let key = pairs[i]
    let mut j = i
    while j > 0 {
      if pairs[j - 1].0 < key.0 {
        pairs[j] = pairs[j - 1]
      } else {
        break
      }
      j = j - 1
    }
    pairs[j] = key
    i = i + 1
  }
  let out : Array[Int] = []
  for i in 0..<take {
    out.push(pairs[i].1)
  }
  out
}

///|
/// Classification accuracy: fraction of (predicted[i] == labels[i]).
/// Returns 0.0F for empty input.
pub fn classification_accuracy(
  predicted : Array[Int],
  labels : Array[Int],
) -> Float {
  let n = predicted.length()
  if n == 0 || n != labels.length() {
    return 0.0F
  }
  let mut correct = 0
  for i in 0..<n {
    if predicted[i] == labels[i] {
      correct = correct + 1
    }
  }
  Float::from_int(correct) / Float::from_int(n)
}

///|
/// Predict class index = argmax(scores). Convenience wrapper.
pub fn prediction(scores : Array[Float]) -> Int {
  argmax(scores)
}

///|
/// Predict top-k class indices. Convenience wrapper.
pub fn top_k_prediction(scores : Array[Float], k : Int) -> Array[Int] {
  top_k(scores, k)
}