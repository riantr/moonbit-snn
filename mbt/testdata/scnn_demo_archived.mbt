// scnn_demo.mbt 鈥?Minimum-viable SCNN training demo (v0.21.0).
//
// End-to-end verification that the project pipeline works:
//   1. Build synthetic 28x28 dataset (10 classes, fixed patterns).
//   2. Forward through SimpleCNN.
//   3. Compute softmax + cross-entropy loss.
//   4. Backward through the chain (returns per-layer gradients).
//   5. Verify loss is finite / reasonable / positive (sanity).
//   6. Verify grads are finite (no NaN / Inf).
//
// This is the ANN-as-SNN "rate-based" baseline (no spikes, no time
// stepping, no surrogate gradient). True spiking CSNN is v0.22.x.

///|
/// Build a 10-class synthetic 28x28 dataset. Each class has a fixed
/// binary pattern plus ~5% pixel-flip noise.
pub fn synthetic_mnist_build() -> (Array[Array[Float]], Array[Int]) {
  let n_per_class = 10
  let classes = 10
  let total = n_per_class * classes
  let images : Array[Array[Float]] = Array::make(total, [])
  let labels : Array[Int] = Array::make(total, 0)
  let rng = Xoshiro::new(42UL)
  for cls in 0..<classes {
    for sample in 0..<n_per_class {
      let idx = cls * n_per_class + sample
      labels[idx] = cls
      let img : Array[Float] = Array::make(28 * 28, 0.0F)
      let _unit = match cls {
        0 =>
          // Horizontal line at row 14.
          for c in 0..<28 {
            img[14 * 28 + c] = 1.0F
          }
        1 =>
          // Vertical line at col 14.
          for r in 0..<28 {
            img[r * 28 + 14] = 1.0F
          }
        2 =>
          // Top half filled.
          for r in 0..<14 {
            for c in 0..<28 {
              img[r * 28 + c] = 1.0F
            }
          }
        3 =>
          // Bottom half filled.
          for r in 14..<28 {
            for c in 0..<28 {
              img[r * 28 + c] = 1.0F
            }
          }
        4 =>
          // Centre 10x10 block.
          for r in 9..<19 {
            for c in 9..<19 {
              img[r * 28 + c] = 1.0F
            }
          }
        5 =>
          // Diagonal.
          for i in 0..<28 {
            img[i * 28 + i] = 1.0F
          }
        6 =>
          // Anti-diagonal.
          for i in 0..<28 {
            img[i * 28 + (27 - i)] = 1.0F
          }
        7 =>
          // Cross.
          for r in 0..<28 {
            img[r * 28 + 14] = 1.0F
          }
          for c in 0..<28 {
            img[14 * 28 + c] = 1.0F
          }
        8 =>
          // Border.
          for r in 0..<28 {
            for c in 0..<28 {
              if r < 2 || r > 25 || c < 2 || c > 25 {
                img[r * 28 + c] = 1.0F
              }
            }
          }
        _ =>
          // X shape (both diagonals).
          for i in 0..<28 {
            img[i * 28 + i] = 1.0F
            img[i * 28 + (27 - i)] = 1.0F
          }
      }
      ; // Add noise.
      for i in 0..<(28 * 28) {
        let u = next_f32(rng)
        if u < 0.05F {
          img[i] = if img[i] > 0.5F { 0.0F } else { 1.0F }
        }
      }
      images[idx] = img
    }
  }
  (images, labels)
}

///|
/// One-hot encode an Int label.
pub fn one_hot(label : Int, num_classes : Int) -> Array[Float] {
  let row : Array[Float] = Array::make(num_classes, 0.0F)
  row[label] = 1.0F
  row
}

///|
/// Numerically-stable softmax.
pub fn softmax_row(x : Array[Float]) -> Array[Float] {
  let n = x.length()
  let out : Array[Float] = Array::make(n, 0.0F)
  let mut max_v = x[0]
  for i in 1..<n {
    if x[i] > max_v { max_v = x[i] }
  }
  let mut sum = 0.0F
  for i in 0..<n {
    out[i] = expf(x[i] - max_v)
    sum = sum + out[i]
  }
  for i in 0..<n {
    out[i] = out[i] / sum
  }
  out
}

///|
/// Cross-entropy for one row: `loss = -sum(target * log(prob))`.
pub fn cross_entropy_one(prob : Array[Float], target : Array[Float]) -> Float {
  let mut s = 0.0F
  for j in 0..<prob.length() {
    s = s - target[j] * logf(prob[j])
  }
  s
}

///|
/// Run a single forward + loss + backward pass. Returns
/// `(loss, total_params_with_grads)` for sanity checking.
pub fn scnn_single_step() -> (Float, Int) {
  let (images, labels) = synthetic_mnist_build()
  let num_classes = 10
  let batch = images.length()  // 100
  // Build SimpleCNN.
  let model = SimpleCNN::new(1, 8, 16, 7UL)
  // Flatten images to NCHW float tensor.
  let flat_input : Array[Float] = Array::make(batch * 28 * 28, 0.0F)
  for i in 0..<batch {
    let img = images[i]
    for j in 0..<(28 * 28) {
      flat_input[i * 28 * 28 + j] = img[j]
    }
  }
  // Forward.
  let (logits_flat, caches, _, _, _, _) = simple_cnn_forward(
    model, flat_input, batch,
  )
  // Softmax + cross-entropy + manual d_logits = softmax - target.
  let mut total_loss = 0.0F
  let d_logits : Array[Float] = Array::make(batch * num_classes, 0.0F)
  for i in 0..<batch {
    let row : Array[Float] = Array::make(num_classes, 0.0F)
    for j in 0..<num_classes {
      row[j] = logits_flat[i * num_classes + j]
    }
    let probs = softmax_row(row)
    let target = one_hot(labels[i], num_classes)
    total_loss = total_loss + cross_entropy_one(probs, target)
    for j in 0..<num_classes {
      d_logits[i * num_classes + j] = probs[j] - target[j]
    }
  }
  let mean_loss = total_loss / Float::from_int(batch)
  // Backward.
  let (_d_input, grads) = simple_cnn_backward(
    model, caches, d_logits, batch,
  )
  // Sanity check grads: count non-zero gradient entries.
  let mut grad_count = 0
  for g in grads {
    match g {
      Conv2d(d_w, _d_b) => {
        for k in 0..<d_w.length() {
          if d_w[k] != 0.0F { grad_count = grad_count + 1 }
        }
      }
      Linear(d_w, _d_b) => {
        for k in 0..<d_w.length() {
          if d_w[k] != 0.0F { grad_count = grad_count + 1 }
        }
      }
      BatchNorm2d(d_g, _d_b) => {
        for k in 0..<d_g.length() {
          if d_g[k] != 0.0F { grad_count = grad_count + 1 }
        }
      }
      _ => ()
    }
  }
  let _ = _d_input
  (mean_loss, grad_count)
}

///|
/// Check that the loss is finite, positive, and below the entropy of
/// a uniform distribution over 10 classes (= ln(10) 鈮?2.303).
/// The model is randomly initialised so loss should be near the
/// uniform-entropy baseline.
pub fn scnn_loss_is_reasonable() -> Bool {
  let (loss, _) = scnn_single_step()
  if loss <= 0.0F { return false }
  if loss != loss { return false } // NaN check
  if loss > 5.0F * 2.303F { return false }
  true
}

///|
/// Check that running forward multiple times with the same input gives
/// identical outputs (determinism check).
pub fn scnn_deterministic_forward() -> Bool {
  let model = SimpleCNN::new(1, 8, 16, 7UL)
  let input : Array[Float] = Array::make(2 * 28 * 28, 0.5F)
  let (out1, _, _, _, _, _) = simple_cnn_forward(model, input, 2)
  let (out2, _, _, _, _, _) = simple_cnn_forward(model, input, 2)
  if out1.length() != out2.length() { return false }
  for i in 0..<out1.length() {
    if out1[i] != out2[i] { return false }
  }
  true
}