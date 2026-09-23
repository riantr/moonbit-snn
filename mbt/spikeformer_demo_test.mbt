// Tests for the Mini-SpikeFormer training demo.

///|
test "spikeformer_dataset_build: shape + label distribution" {
  let (images, labels) = spikeformer_dataset_build(1UL)
  let h = 28
  let w = 28
  let n_total = 100
  assert_eq(images.length(), n_total * h * w)
  assert_eq(labels.length(), n_total)
  // Each class should appear 10 times.
  let counts : Array[Int] = Array::make(10, 0)
  for i in 0..<labels.length() {
    counts[labels[i]] = counts[labels[i]] + 1
  }
  for c in 0..<10 {
    assert_eq(counts[c], 10)
  }
}

///|
test "spikeformer_softmax: numerically stable + sums to 1" {
  // Large values that would overflow without max subtraction.
  let logits : Array[Float] = [1000.0F, 1001.0F, 1002.0F]
  let probs = spikeformer_softmax(logits)
  let mut sum_p = 0.0F
  for i in 0..<3 {
    sum_p = sum_p + probs[i]
  }
  if !(sum_p > 0.99F && sum_p < 1.01F) {
    fail("softmax doesn't sum to 1: \{sum_p}")
  }
  // Most mass on the largest input.
  if probs[2] < 0.5F {
    fail("softmax[2] = \{probs[2]}, expected ≈ 0.84")
  }
}

///|
test "mini_spike_former_forward: loss is finite + sane (≈ ln(10))" {
  let (images, labels) = spikeformer_dataset_build(1UL)
  let msf = MiniSpikeFormer::new(16, 2, 1.0F, 10, 42UL)
  let (loss, cache) = mini_spike_former_forward(images, 1, labels[0], msf)
  if !(loss > 0.0F && loss < 5.0F) {
    fail("loss = \{loss}, expected in (0, 5)")
  }
  // Cache sanity.
  assert_eq(cache.probs.length(), 10)
  assert_eq(cache.logits.length(), 10)
  let mut sum_p = 0.0F
  for i in 0..<cache.probs.length() {
    sum_p = sum_p + cache.probs[i]
  }
  if !(sum_p > 0.99F && sum_p < 1.01F) {
    fail("probs sum = \{sum_p}")
  }
}

///|
test "mini_spike_former_backward: gradients are non-zero finite" {
  let (images, labels) = spikeformer_dataset_build(1UL)
  let msf = MiniSpikeFormer::new(16, 2, 1.0F, 10, 42UL)
  let (_loss, cache) = mini_spike_former_forward(images, 1, labels[0], msf)
  let (_d_pw, _d_pb, d_ct, d_pos, bg, d_cw, _d_cb) = mini_spike_former_backward(
    cache, msf,
  )
  // Class token gradient non-zero.
  let mut non_zero_ct = 0
  for i in 0..<d_ct.length() {
    if d_ct[i] != 0.0F {
      non_zero_ct = non_zero_ct + 1
    }
  }
  if non_zero_ct == 0 {
    fail("class_token gradient is all zero")
  }
  // Pos weight gradient non-zero.
  let mut non_zero_pos = 0
  for i in 0..<d_pos.length() {
    if d_pos[i] != 0.0F {
      non_zero_pos = non_zero_pos + 1
    }
  }
  if non_zero_pos == 0 {
    fail("pos_weight gradient is all zero")
  }
  // Classifier weight gradient non-zero.
  let mut non_zero_cw = 0
  for i in 0..<d_cw.length() {
    if d_cw[i] != 0.0F {
      non_zero_cw = non_zero_cw + 1
    }
  }
  if non_zero_cw == 0 {
    fail("cls_w gradient is all zero")
  }
  // Block grads non-zero.
  let mut non_zero_bg = 0
  for i in 0..<bg.sa_grad.d_w_q.length() {
    if bg.sa_grad.d_w_q[i] != 0.0F {
      non_zero_bg = non_zero_bg + 1
    }
  }
  if non_zero_bg == 0 {
    fail("block SA gradient is all zero")
  }
}

///|
test "mini_spike_former_train_step: loss decreases after K steps" {
  let (images, labels) = spikeformer_dataset_build(1UL)
  let msf = MiniSpikeFormer::new(16, 2, 1.0F, 10, 42UL)
  // Focus on a single sample (idx=0) for both initial and final loss
  // so that training on sample 0 can be observed directly (training
  // updates parameters; evaluating on unrelated samples may fluctuate).
  let one_img : Array[Float] = Array::make(28 * 28, 0.0F)
  for j in 0..<(28 * 28) {
    one_img[j] = images[j]
  }
  let (initial_loss, _) = mini_spike_former_forward(one_img, 1, labels[0], msf)
  // Train K=8 steps on the same sample with gradient clipping + small lr.
  let lr = 0.005F
  for step in 0..<8 {
    ignore(mini_spike_former_train_step(one_img, labels[0], lr, msf))
    ignore(step)
  }
  let (final_loss, _) = mini_spike_former_forward(one_img, 1, labels[0], msf)
  if !(final_loss < initial_loss) {
    fail("training did not reduce loss: initial = \{initial_loss}, final = \{final_loss}")
  }
}

///|
test "spikeformer beta sensitivity: β affects training trajectory" {
  // Same seed, two different β values, K=4 steps. Loss trajectories
  // should differ in the surrogate gradient signal magnitude, hence
  // different final classifier weights.
  let (images, labels) = spikeformer_dataset_build(1UL)
  let msf_a = MiniSpikeFormer::new(16, 2, 1.0F, 10, 42UL)
  let msf_b = MiniSpikeFormer::new(16, 2, 100.0F, 10, 42UL)
  let lr = 0.001F
  for _step in 0..<4 {
    let one_img : Array[Float] = Array::make(28 * 28, 0.0F)
    for j in 0..<(28 * 28) {
      one_img[j] = images[j]
    }
    ignore(mini_spike_former_train_step(one_img, labels[0], lr, msf_a))
    ignore(mini_spike_former_train_step(one_img, labels[0], lr, msf_b))
  }
  // Compare cls_w[0] between the two: must differ (β shapes gradient).
  let mut differ = false
  for i in 0..<msf_a.cls_w.length() {
    if msf_a.cls_w[i] != msf_b.cls_w[i] {
      differ = true
      break
    }
  }
  if !differ {
    fail("β=1 vs β=100 produced identical cls_w (impossible)")
  }
}

// ---- Mini-SpikeFormer v2 (v0.27.2) tests: predict + eval accuracy ----

///|
test "spikeformer_argmax: returns index of max value (ties → first)" {
  let logits : Array[Float] = [1.0F, 2.0F, 3.0F, 0.5F, 0.1F]
  assert_eq(spikeformer_argmax(logits), 2)
  // All-equal: first occurrence wins.
  let all_eq : Array[Float] = [1.0F, 1.0F, 1.0F]
  assert_eq(spikeformer_argmax(all_eq), 0)
  // Single element.
  let one : Array[Float] = [42.0F]
  assert_eq(spikeformer_argmax(one), 0)
}

///|
test "mini_spike_former_predict: returns valid class in [0, n_classes)" {
  let (images, _labels) = spikeformer_dataset_build(1UL)
  let msf = MiniSpikeFormer::new(16, 2, 1.0F, 10, 42UL)
  let one_img : Array[Float] = Array::make(28 * 28, 0.0F)
  for j in 0..<(28 * 28) {
    one_img[j] = images[j]
  }
  let pred = mini_spike_former_predict(one_img, msf)
  if pred < 0 || pred >= 10 {
    fail("predicted class = \{pred}, expected in [0, 10)")
  }
}

///|
test "mini_spike_former_eval_accuracy: untrained model is near chance" {
  let (images, labels) = spikeformer_dataset_build(1UL)
  let msf = MiniSpikeFormer::new(16, 2, 1.0F, 10, 42UL)
  let acc = mini_spike_former_eval_accuracy(images, labels, msf)
  // Chance = 1/10 = 0.1. An untrained model should NOT exceed 0.5
  // (a higher value would mean the synthetic dataset is trivial).
  if acc > 0.5F {
    fail("untrained accuracy = \{acc}, suspiciously high; chance = 0.1")
  }
  // And the result must be a valid fraction in [0, 1].
  if acc < 0.0F || acc > 1.0F {
    fail("accuracy out of [0, 1]: \{acc}")
  }
}

///|
test "mini_spike_former_eval_accuracy: empty label list returns 0" {
  let (images, _labels) = spikeformer_dataset_build(1UL)
  let msf = MiniSpikeFormer::new(16, 2, 1.0F, 10, 42UL)
  let empty : Array[Int] = Array::make(0, 0)
  let acc = mini_spike_former_eval_accuracy(images, empty, msf)
  assert_eq(acc, 0.0F)
}

///|
test "mini_spike_former_train_n_steps: accuracy > chance after K=12 steps" {
  let (images, labels) = spikeformer_dataset_build(1UL)
  let msf = MiniSpikeFormer::new(16, 2, 1.0F, 10, 42UL)
  let one_img : Array[Float] = Array::make(28 * 28, 0.0F)
  for j in 0..<(28 * 28) {
    one_img[j] = images[j]
  }
  let (init_acc, final_acc) = mini_spike_former_train_n_steps(
    one_img, labels[0], 12, 0.005F, msf,
  )
  // Sanity: initial accuracy is in [0, 1]. We do NOT assert that
  // init_acc < 0.5 because with a single sample and 10 classes, the
  // random init may happen to classify it correctly by chance (1/10).
  if init_acc < 0.0F || init_acc > 1.0F {
    fail("initial accuracy = \{init_acc}, out of [0, 1]")
  }
  // Training must not decrease accuracy (training is non-degrading).
  if !(final_acc >= init_acc) {
    fail("training reduced accuracy: \{init_acc} → \{final_acc}")
  }
  // Final accuracy should clearly beat chance (0.1) — headroom ≥ 0.4
  // to ensure the model has converged on the training sample.
  if final_acc < 0.5F {
    fail("final accuracy = \{final_acc}, expected ≥ 0.5 (well above chance 0.1)")
  }
}

///|
test "mini_spike_former_train_n_steps: loss decreases AND accuracy improves" {
  let (images, labels) = spikeformer_dataset_build(1UL)
  let msf = MiniSpikeFormer::new(16, 2, 1.0F, 10, 42UL)
  let one_img : Array[Float] = Array::make(28 * 28, 0.0F)
  for j in 0..<(28 * 28) {
    one_img[j] = images[j]
  }
  let (init_loss, _) = mini_spike_former_forward(one_img, 1, labels[0], msf)
  let (_init_acc, final_acc) = mini_spike_former_train_n_steps(
    one_img, labels[0], 10, 0.005F, msf,
  )
  let (final_loss, _) = mini_spike_former_forward(one_img, 1, labels[0], msf)
  if !(final_loss < init_loss) {
    fail("loss did not decrease: \{init_loss} → \{final_loss}")
  }
  if final_acc < 0.5F {
    fail("final accuracy = \{final_acc}, expected ≥ 0.5 after training")
  }
}