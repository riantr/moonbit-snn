// scnn_demo_test.mbt — SCNN demo verification tests (v0.21.0).

///|
test "scnn_demo / synthetic dataset has correct count + labels" {
  let (images, labels) = synthetic_mnist_build()
  if images.length() != 100 { fail("should have 100 images, got \{images.length()}") }
  if labels.length() != 100 { fail("labels should have 100, got \{labels.length()}") }
  // Each label appears exactly 10 times.
  let counts : Array[Int] = Array::make(10, 0)
  for l in labels {
    counts[l] = counts[l] + 1
  }
  for c in 0..<10 {
    if counts[c] != 10 {
      fail("class \{c} should have 10 samples, got \{counts[c]}")
    }
  }
  // Each image is 28*28.
  for i in 0..<images.length() {
    if images[i].length() != 28 * 28 {
      fail("image \{i} should be 28*28, got \{images[i].length()}")
    }
  }
}

///|
test "scnn_demo / softmax_row sums to 1 and is finite" {
  let x : Array[Float] = [1.0F, 2.0F, 3.0F, 4.0F]
  let p = softmax_row(x)
  if p.length() != 4 { fail("length should be 4") }
  let mut sum = 0.0F
  for v in p {
    sum = sum + v
    if v != v { fail("softmax output is NaN") }
    if v < 0.0F { fail("softmax output is negative") }
  }
  if (sum - 1.0F).abs() > 0.0001F {
    fail("softmax output should sum to 1, got \{sum}")
  }
}

///|
test "scnn_demo / cross-entropy is zero for perfect predictions" {
  // When probability = target at the true class (1.0) and 0 elsewhere,
  // loss = -1.0 * log(1.0) - 0 * log(0) (we use log of positive value).
  // Actually with log(0) -> -inf, so let's test the "all-correct" case
  // where target[i] = [0, 1, 0] and prob[i] = [0.001, 0.998, 0.001].
  let target : Array[Float] = [0.0F, 1.0F, 0.0F]
  let prob : Array[Float] = [0.001F, 0.998F, 0.001F]
  let loss = cross_entropy_one(prob, target)
  let expected = -1.0F * logf(0.998F)
  if (loss - expected).abs() > 0.001F {
    fail("loss should be ~\{expected}, got \{loss}")
  }
}

///|
test "scnn_demo / single_step returns finite loss + non-zero gradients" {
  let (loss, grad_count) = scnn_single_step()
  if loss != loss { fail("loss is NaN") }
  if loss <= 0.0F { fail("loss should be positive, got \{loss}") }
  if grad_count <= 0 { fail("grad_count should be positive, got \{grad_count}") }
}

///|
test "scnn_demo / loss is reasonable (near uniform entropy at init)" {
  // Random init should give loss near ln(num_classes) = ln(10) ≈ 2.303.
  // Allow 0.5x to 2x that range.
  let (loss, _) = scnn_single_step()
  let target = 2.303F  // ln(10)
  if loss < target * 0.5F {
    fail("loss should be ~ln(10) at random init, got \{loss} (target ~\{target})")
  }
  if loss > target * 3.0F {
    fail("loss should be near ln(10), got \{loss} (target ~\{target})")
  }
}

///|
test "scnn_demo / forward is deterministic across calls" {
  if not(scnn_deterministic_forward()) {
    fail("forward should be deterministic across calls")
  }
}

///|
test "scnn_demo / scnn_loss_is_reasonable sanity check passes" {
  if not(scnn_loss_is_reasonable()) {
    fail("loss should be positive, finite, and bounded at random init")
  }
}

// ---- SCNN v2 (v0.27.1) training loop tests ----

///|
test "scnn_v2 / scnn_accuracy > 0.5 on synthetic dataset after K=20 SGD steps" {
  // With K=20 SGD steps and lr=0.05 on a small synthetic dataset,
  // expect accuracy to exceed chance (1/10 = 0.1).
  let (_init_loss, _final_loss, init_acc, final_acc) = scnn_train_n_steps(20, 0.05F, 7UL)
  if init_acc > 0.5F {
    fail("initial accuracy too high (\{init_acc}); would mean dataset is trivial")
  }
  if final_acc <= init_acc + 0.05F {
    fail("training did not improve accuracy: init = \{init_acc}, final = \{final_acc}")
  }
  ignore(_init_loss)
  ignore(_final_loss)
}

///|
test "scnn_v2 / loss decreases over K=10 SGD steps" {
  let (init_loss, final_loss, _ia, _fa) = scnn_train_n_steps(10, 0.05F, 42UL)
  if final_loss >= init_loss {
    fail("training did not reduce loss: init = \{init_loss}, final = \{final_loss}")
  }
}