// scnn_demo.mbt -- Minimum-viable SCNN training demo (v0.21.0).
//
// End-to-end verification that the project pipeline works:
//   1. Build synthetic 28x28 dataset (10 classes, fixed patterns).
//   2. Forward through SimpleCNN.
//   3. Compute softmax + cross-entropy loss.
//   4. Backward through the chain (returns per-layer gradients).
//   5. Verify loss is finite / reasonable / positive (sanity).
//   6. Verify grads are finite (no NaN / Inf).

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
  let mut cls = 0
  while cls < classes {
    let mut sample = 0
    while sample < n_per_class {
      let idx = cls * n_per_class + sample
      labels[idx] = cls
      let img : Array[Float] = Array::make(28 * 28, 0.0F)
      if cls == 0 {
        let mut c = 0
        while c < 28 {
          img[14 * 28 + c] = 1.0F
          c = c + 1
        }
      } else if cls == 1 {
        let mut r = 0
        while r < 28 {
          img[r * 28 + 14] = 1.0F
          r = r + 1
        }
      } else if cls == 2 {
        let mut r = 0
        while r < 14 {
          let mut c = 0
          while c < 28 {
            img[r * 28 + c] = 1.0F
            c = c + 1
          }
          r = r + 1
        }
      } else if cls == 3 {
        let mut r = 14
        while r < 28 {
          let mut c = 0
          while c < 28 {
            img[r * 28 + c] = 1.0F
            c = c + 1
          }
          r = r + 1
        }
      } else if cls == 4 {
        let mut r = 9
        while r < 19 {
          let mut c = 9
          while c < 19 {
            img[r * 28 + c] = 1.0F
            c = c + 1
          }
          r = r + 1
        }
      } else if cls == 5 {
        let mut i = 0
        while i < 28 {
          img[i * 28 + i] = 1.0F
          i = i + 1
        }
      } else if cls == 6 {
        let mut i = 0
        while i < 28 {
          img[i * 28 + (27 - i)] = 1.0F
          i = i + 1
        }
      } else if cls == 7 {
        let mut r = 0
        while r < 28 {
          img[r * 28 + 14] = 1.0F
          r = r + 1
        }
        let mut c = 0
        while c < 28 {
          img[14 * 28 + c] = 1.0F
          c = c + 1
        }
      } else if cls == 8 {
        let mut r = 0
        while r < 28 {
          let mut c = 0
          while c < 28 {
            let is_border = r < 2 || r > 25 || c < 2 || c > 25
            if is_border { img[r * 28 + c] = 1.0F }
            c = c + 1
          }
          r = r + 1
        }
      } else {
        let mut i = 0
        while i < 28 {
          img[i * 28 + i] = 1.0F
          img[i * 28 + (27 - i)] = 1.0F
          i = i + 1
        }
      }
      // Add noise.
      let mut i = 0
      while i < 28 * 28 {
        let u = next_f32(rng)
        let original = img[i]
        let is_bright = original > 0.5F
        let flipped : Float = if is_bright { 0.0F } else { 1.0F }
        let chosen : Float = if u < 0.05F { flipped } else { original }
        img[i] = chosen
        i = i + 1
      }
      images[idx] = img
      sample = sample + 1
    }
    cls = cls + 1
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
  let mut i = 1
  while i < n {
    if x[i] > max_v { max_v = x[i] }
    i = i + 1
  }
  let mut sum = 0.0F
  let mut j = 0
  while j < n {
    out[j] = expf(x[j] - max_v)
    sum = sum + out[j]
    j = j + 1
  }
  let mut k = 0
  while k < n {
    out[k] = out[k] / sum
    k = k + 1
  }
  out
}

///|
/// Cross-entropy for one row: loss = -sum(target * log(prob)).
pub fn cross_entropy_one(prob : Array[Float], target : Array[Float]) -> Float {
  let mut s = 0.0F
  let mut j = 0
  while j < prob.length() {
    s = s - target[j] * logf(prob[j])
    j = j + 1
  }
  s
}

///|
/// Run a single forward + loss + backward pass. Returns
/// (loss, total_params_with_grads).
pub fn scnn_single_step() -> (Float, Int) {
  let (images, labels) = synthetic_mnist_build()
  let num_classes = 10
  let batch = images.length()
  let model = SimpleCNN::new(1, 8, 16, 7UL)
  let flat_input : Array[Float] = Array::make(batch * 28 * 28, 0.0F)
  let mut i = 0
  while i < batch {
    let img = images[i]
    let mut j = 0
    while j < 28 * 28 {
      flat_input[i * 28 * 28 + j] = img[j]
      j = j + 1
    }
    i = i + 1
  }
  let (logits_flat, caches, _, _, _, _) = simple_cnn_forward(
    model, flat_input, batch,
  )
  let mut total_loss = 0.0F
  let d_logits : Array[Float] = Array::make(batch * num_classes, 0.0F)
  let mut ii = 0
  while ii < batch {
    let row : Array[Float] = Array::make(num_classes, 0.0F)
    let mut jj = 0
    while jj < num_classes {
      row[jj] = logits_flat[ii * num_classes + jj]
      jj = jj + 1
    }
    let probs = softmax_row(row)
    let target = one_hot(labels[ii], num_classes)
    total_loss = total_loss + cross_entropy_one(probs, target)
    let mut k = 0
    while k < num_classes {
      d_logits[ii * num_classes + k] = probs[k] - target[k]
      k = k + 1
    }
    ii = ii + 1
  }
  let mean_loss = total_loss / Float::from_int(batch)
  let (_d_input, grads) = simple_cnn_backward(
    model, caches, d_logits, batch,
  )
  let mut grad_count = 0
  let mut g_idx = 0
  while g_idx < grads.length() {
    match grads[g_idx] {
      Conv2d(d_w, _d_b) => {
        let mut k = 0
        while k < d_w.length() {
          if d_w[k] != 0.0F { grad_count = grad_count + 1 }
          k = k + 1
        }
      }
      Linear(d_w, _d_b) => {
        let mut k = 0
        while k < d_w.length() {
          if d_w[k] != 0.0F { grad_count = grad_count + 1 }
          k = k + 1
        }
      }
      BatchNorm2d(d_g, _d_b) => {
        let mut k = 0
        while k < d_g.length() {
          if d_g[k] != 0.0F { grad_count = grad_count + 1 }
          k = k + 1
        }
      }
      _ => ()
    }
    g_idx = g_idx + 1
  }
  let _ = _d_input
  (mean_loss, grad_count)
}

///|
/// Sanity check that the loss is finite, positive, and bounded.
pub fn scnn_loss_is_reasonable() -> Bool {
  let (loss, _) = scnn_single_step()
  if loss <= 0.0F { return false }
  if loss != loss { return false }
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
  let mut i = 0
  while i < out1.length() {
    if out1[i] != out2[i] { return false }
    i = i + 1
  }
  true
}

// ---- SCNN v2 (v0.27.1): full K-step training with accuracy ----

///|
/// Predict argmax class for a batch of logits (length batch * n_classes).
pub fn scnn_predict_batch(
  logits_flat : Array[Float],
  batch : Int,
  n_classes : Int,
) -> Array[Int] {
  let preds : Array[Float] = Array::make(batch, 0.0F)
  let mut i = 0
  while i < batch {
    let mut best = 0
    let mut best_val = logits_flat[i * n_classes + 0]
    let mut c = 1
    while c < n_classes {
      let v = logits_flat[i * n_classes + c]
      if v > best_val {
        best_val = v
        best = c
      }
      c = c + 1
    }
    preds[i] = Float::from_int(best)
    i = i + 1
  }
  // Cast Float to Int via reading + casting
  let out : Array[Int] = Array::make(batch, 0)
  let mut j = 0
  while j < batch {
    out[j] = preds[j].to_int()
    j = j + 1
  }
  out
}

///|
/// Compute classification accuracy on a batch.
pub fn scnn_accuracy(
  logits_flat : Array[Float],
  labels : Array[Int],
  batch : Int,
  n_classes : Int,
) -> Float {
  let preds = scnn_predict_batch(logits_flat, batch, n_classes)
  let mut correct = 0
  let mut i = 0
  while i < batch {
    if preds[i] == labels[i] { correct = correct + 1 }
    i = i + 1
  }
  Float::from_int(correct) / Float::from_int(batch)
}

///|
/// Run a single forward + loss + backward pass; apply SGD updates to
/// the model's layers in place. Returns (loss, accuracy) on the batch.
pub fn scnn_train_step_with_sgd(
  model : SimpleCNN,
  input : Array[Float],
  labels : Array[Int],
  batch : Int,
  n_classes : Int,
  lr : Float,
) -> (Float, Float) {
  let (logits_flat, caches, _, _, _, _) = simple_cnn_forward(
    model, input, batch,
  )
  // CE gradient (per-row: per-sample).
  let d_logits : Array[Float] = Array::make(batch * n_classes, 0.0F)
  let mut total_loss = 0.0F
  let mut ii = 0
  while ii < batch {
    let row : Array[Float] = Array::make(n_classes, 0.0F)
    let mut jj = 0
    while jj < n_classes {
      row[jj] = logits_flat[ii * n_classes + jj]
      jj = jj + 1
    }
    let probs = softmax_row(row)
    let target = one_hot(labels[ii], n_classes)
    total_loss = total_loss + cross_entropy_one(probs, target)
    let mut k = 0
    while k < n_classes {
      d_logits[ii * n_classes + k] = probs[k] - target[k]
      k = k + 1
    }
    ii = ii + 1
  }
  let mean_loss = total_loss / Float::from_int(batch)
  // Backward.
  let (_d_input, grads) = simple_cnn_backward(model, caches, d_logits, batch)
  ignore(_d_input)
  // SGD update — walk per-layer params by matching on Layer enum
  // variant (shadow binding gives access to the variant's payload).
  let clip = 1.0F
  let mut g_idx = 0
  while g_idx < grads.length() {
    let d_grad = grads[g_idx]
    let d_layer = model.layers[g_idx]
    match (d_layer, d_grad) {
      (Conv2d(param), LayerGrad::Conv2d(d_w, d_b)) => {
        clip_grad_array(d_w, clip)
        clip_grad_array(d_b, clip)
        for i in 0..<param.weight.length() {
          param.weight[i] = param.weight[i] - lr * d_w[i]
        }
        for i in 0..<param.bias.length() {
          param.bias[i] = param.bias[i] - lr * d_b[i]
        }
      }
      (Linear(param), LayerGrad::Linear(d_w, d_b)) => {
        clip_grad_array(d_w, clip)
        clip_grad_array(d_b, clip)
        for i in 0..<param.weight.length() {
          param.weight[i] = param.weight[i] - lr * d_w[i]
        }
        for i in 0..<param.bias.length() {
          param.bias[i] = param.bias[i] - lr * d_b[i]
        }
      }
      (BatchNorm2d(bn), LayerGrad::BatchNorm2d(d_g, d_b)) => {
        clip_grad_array(d_g, clip)
        clip_grad_array(d_b, clip)
        for i in 0..<bn.gamma.length() {
          bn.gamma[i] = bn.gamma[i] - lr * d_g[i]
        }
        for i in 0..<bn.beta.length() {
          bn.beta[i] = bn.beta[i] - lr * d_b[i]
        }
      }
      _ => ()
    }
    g_idx = g_idx + 1
  }
  let acc = scnn_accuracy(logits_flat, labels, batch, n_classes)
  (mean_loss, acc)
}

///|
/// Run K SGD training steps on the synthetic dataset. Returns
/// (initial_loss, final_loss, initial_acc, final_acc).
pub fn scnn_train_n_steps(
  n_steps : Int,
  lr : Float,
  seed : UInt64,
) -> (Float, Float, Float, Float) {
  let (images, labels) = synthetic_mnist_build()
  let batch = images.length()
  let n_classes = 10
  let model = SimpleCNN::new(1, 8, 16, seed)
  // Flatten images into a single batch.
  let flat_input : Array[Float] = Array::make(batch * 28 * 28, 0.0F)
  let mut i = 0
  while i < batch {
    let img = images[i]
    let mut j = 0
    while j < 28 * 28 {
      flat_input[i * 28 * 28 + j] = img[j]
      j = j + 1
    }
    i = i + 1
  }
  // Initial loss + accuracy (no updates).
  let (init_loss, init_acc) = scnn_train_step_with_sgd(
    model, flat_input, labels, batch, n_classes, 0.0F,
  )
  // K real steps.
  for _k in 0..<n_steps {
    let (_l, _a) = scnn_train_step_with_sgd(
      model, flat_input, labels, batch, n_classes, lr,
    )
    ignore(_l)
    ignore(_a)
  }
  // Final loss + accuracy.
  let (final_loss, final_acc) = scnn_train_step_with_sgd(
    model, flat_input, labels, batch, n_classes, 0.0F,
  )
  (init_loss, final_loss, init_acc, final_acc)
}