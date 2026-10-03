// vit_trainer.mbt -- ViT trainer: cross-entropy loss + SGD step (v0.112.0).
//
// Scope of v0.112.0:
//   - vit_cross_entropy: cross-entropy loss from logits + true class.
//   - vit_softmax: stable softmax over logits.
//   - vit_train_step_head: one SGD step on the classification head only
//     (analytic gradient through the softmax+CE+linear chain).
//   - vit_train_step: forward + loss + SGD-on-head.
//
// BPTT through N ViTBlocks (so that the full ViT can be trained end-to-end)
// is deferred to a follow-up batch.
//
// Reference: Dosovitskiy et al. 2020; standard softmax+CE+linear
// gradient is computed analytically here.

///|
/// Stable softmax over logits (subtracts the max for numerical stability).
pub fn vit_softmax(logits : Array[Float]) -> Array[Float] {
  let n = logits.length()
  // Find max for stability.
  let mut m = logits[0]
  for i in 1..<n {
    if logits[i] > m {
      m = logits[i]
    }
  }
  let exp_logits : Array[Float] = Array::make(n, 0.0F)
  let mut sum_exp = 0.0F
  for i in 0..<n {
    let e = expf(logits[i] - m)
    exp_logits[i] = e
    sum_exp = sum_exp + e
  }
  for i in 0..<n {
    exp_logits[i] = exp_logits[i] / sum_exp
  }
  exp_logits
}

///|
/// Cross-entropy loss from logits and the true class index.
/// Returns -log(softmax(logits)[target]).
pub fn vit_cross_entropy(logits : Array[Float], target : Int) -> Float {
  let probs = vit_softmax(logits)
  // Add a tiny epsilon for numerical safety (avoid log(0)).
  let eps = 1.0e-12F
  let p = if probs[target] > eps { probs[target] } else { eps }
  -logf(p)
}

///|
/// One SGD step on the classification head (Linear d_model -> num_classes)
/// given the cached CLS representation, the logits, and the target.
/// Mutates vit.head_w and vit.head_b in place. Returns the loss before
/// the step.
pub fn vit_train_step_head(
  vit : ViT,
  cls_repr : Array[Float],
  logits : Array[Float],
  target : Int,
  lr : Float,
) -> Float {
  let num_classes = vit.num_classes
  let d_model = vit.d_model
  // Cross-entropy gradient: dL/dlogits[k] = softmax[k] - 1[k == target].
  let probs = vit_softmax(logits)
  let loss = -logf(if probs[target] > 1.0e-12F { probs[target] } else { 1.0e-12F })
  // d_logits[k] = probs[k] - (k == target ? 1 : 0).
  let d_logits : Array[Float] = Array::make(num_classes, 0.0F)
  for k in 0..<num_classes {
    let t = if k == target { 1.0F } else { 0.0F }
    d_logits[k] = probs[k] - t
  }
  // Head linear: logits[k] = head_b[k] + sum_i head_w[k][i] * cls_repr[i].
  // d_logits -> head_w: dW[k][i] += d_logits[k] * cls_repr[i]
  // d_logits -> head_b: dB[k] += d_logits[k]
  for k in 0..<num_classes {
    let g = d_logits[k]
    // Update head_b[k].
    vit.head_b[k] = vit.head_b[k] - lr * g
    // Update head_w[k][i] for all i.
    for i in 0..<d_model {
      vit.head_w[k][i] = vit.head_w[k][i] - lr * g * cls_repr[i]
    }
  }
  // Note: BPTT through the ViTBlocks would back-prop d_logits into cls_repr,
  // which is deferred to a follow-up batch.
  let _ = cls_repr
  loss
}

///|
/// Extract the CLS-token representation from a ViT forward pass
/// (the d_model values at index 0 of the final token sequence).
pub fn vit_extract_cls(vit : ViT, image : Array[Float]) -> Array[Float] {
  let d_model = vit.d_model
  let n_patches = vit.n_patches
  let seq_len = n_patches + 1
  let patch_tokens = patch_embed_image(vit.patch_embed, image)
  let tokens : Array[Float] = Array::make(seq_len * d_model, 0.0F)
  for i in 0..<d_model {
    tokens[i] = vit.cls_token[i]
  }
  for n in 0..<n_patches {
    let src = n * d_model
    let dst = (n + 1) * d_model
    for i in 0..<d_model {
      tokens[dst + i] = patch_tokens[src + i]
    }
  }
  for i in 0..<(seq_len * d_model) {
    tokens[i] = tokens[i] + vit.pos_embed[i]
  }
  let mut h = tokens
  for b in 0..<vit.blocks.length() {
    h = vit_block_forward(vit.blocks[b], h, seq_len)
  }
  let cls_repr : Array[Float] = Array::make(d_model, 0.0F)
  for i in 0..<d_model {
    cls_repr[i] = h[i]
  }
  cls_repr
}

///|
/// One ViT training step:
///   1. Forward image through the ViT (logits + cached CLS repr).
///   2. Apply SGD on the classification head with cross-entropy loss.
/// Returns the loss before the step.
pub fn vit_train_step(
  vit : ViT,
  image : Array[Float],
  target : Int,
  lr : Float,
) -> Float {
  let cls_repr = vit_extract_cls(vit, image)
  // Compute logits from cls_repr through head_w/head_b.
  let num_classes = vit.num_classes
  let d_model = vit.d_model
  let logits : Array[Float] = Array::make(num_classes, 0.0F)
  for k in 0..<num_classes {
    let mut a = vit.head_b[k]
    for i in 0..<d_model {
      a = a + vit.head_w[k][i] * cls_repr[i]
    }
    logits[k] = a
  }
  vit_train_step_head(vit, cls_repr, logits, target, lr)
}

///|
/// Mini-batch cross-entropy loss (mean over examples). Computes the
/// forward pass for each example but does NOT mutate the ViT. Useful
/// for logging training/validation loss.
pub fn vit_eval_loss(
  vit : ViT,
  images : Array[Array[Float]],
  targets : Array[Int],
) -> Float {
  let n = images.length()
  let mut total = 0.0F
  for e in 0..<n {
    let logits = vit_forward(vit, images[e])
    let probs = vit_softmax(logits)
    let p = if probs[targets[e]] > 1.0e-12F {
      probs[targets[e]]
    } else {
      1.0e-12F
    }
    total = total + (-logf(p))
  }
  total / Float::from_int(n)
}
