// cross_entropy.mbt 鈥?cross-entropy loss (v0.13.2).
//
// Given:
//   - `log_probs`: Tensor of shape [batch, n_classes], the
//     log-softmax output of a network.
//   - `targets`: Tensor of shape [batch], integer class indices
//     (each value in [0, n_classes)).
//
// Returns a Tensor of shape [1] containing the **mean** loss:
//   loss = -mean over batch of log_probs[batch, targets[batch]]
//
// Also returns the per-batch loss as a Tensor of shape [batch].
// We bundle both into a struct so callers can choose.

///|
/// Cross-entropy loss result. `mean` is a 1-element Tensor
/// (`shape=[1]`) and `per_batch` has shape `[batch]`.
pub struct CrossEntropyLoss {
  mean : Tensor
  per_batch : Tensor
}

///|
/// Compute the cross-entropy loss given log-probabilities and
/// integer target indices.
pub fn cross_entropy_loss(
  log_probs : Tensor,
  targets : Tensor,
) -> CrossEntropyLoss {
  if log_probs.shape.length() != 2 {
    abort("cross_entropy_loss: log_probs must be 2D [batch, n_classes]")
  }
  if targets.shape.length() != 1 {
    abort("cross_entropy_loss: targets must be 1D [batch]")
  }
  let batch = log_probs.shape[0]
  let n_classes = log_probs.shape[1]
  let per_batch : Array[Float] = Array::make(batch, 0.0F)
  if targets.data.length() != batch {
    abort("cross_entropy_loss: targets length mismatch")
  }
  let mut total : Float = 0.0F
  for b in 0..<batch {
    let t = targets.data[b]
    // The target is an integer class index. Convert via Float -> Int.
    // (This assumes the user stored integer-valued floats.)
    let t_int = t.to_int()
    if t_int < 0 || t_int >= n_classes {
      abort("cross_entropy_loss: target index out of range")
    }
    let lp = log_probs.data[b * n_classes + t_int]
    let loss = -lp
    per_batch[b] = loss
    total = total + loss
  }
  let mean = Tensor::from([total / Float::from_int(batch)], [1])
  { mean, per_batch: Tensor::from(per_batch, [batch]) }
}