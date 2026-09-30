// classifiers_test.mbt — tests for v0.42.0 classifiers.mbt.

///|
test "argmax_basic" {
  let scores : Array[Float] = [0.1F, 0.5F, 0.3F, 0.9F, 0.2F]
  if argmax(scores) != 3 {
    fail("argmax = \{argmax(scores)}, expected 3")
  }
}

///|
test "argmax_first_occurrence_on_ties" {
  let scores : Array[Float] = [0.5F, 0.5F, 0.5F, 0.7F]
  if argmax(scores) != 3 {
    fail("argmax should pick 3 (highest), got \{argmax(scores)}")
  }
  // All-equal tie → first index (0).
  let ties : Array[Float] = [1.0F, 1.0F, 1.0F]
  if argmax(ties) != 0 {
    fail("argmax of all-equal should be 0, got \{argmax(ties)}")
  }
}

///|
test "argmax_empty_returns_zero" {
  let scores : Array[Float] = []
  if argmax(scores) != 0 {
    fail("argmax of empty should be 0")
  }
}

///|
test "argmax_negative_values" {
  // Works with all-negative scores.
  let scores : Array[Float] = [-2.0F, -1.0F, -3.0F, -0.5F]
  if argmax(scores) != 3 {
    fail("argmax of negatives = \{argmax(scores)}, expected 3")
  }
}

///|
test "softmax_sums_to_one" {
  let scores : Array[Float] = [1.0F, 2.0F, 3.0F, 4.0F]
  let probs = softmax(scores)
  let mut sum : Float = 0.0F
  for i in 0..<probs.length() {
    sum = sum + probs[i]
    if probs[i] < 0.0F || probs[i] > 1.0F {
      fail("softmax[\{i}] = \{probs[i]}, expected in [0, 1]")
    }
  }
  if (sum - 1.0F).abs() > 1.0e-5F {
    fail("softmax sum = \{sum}, expected ~1.0")
  }
}

///|
test "softmax_argmax_matches_input_argmax" {
  let scores : Array[Float] = [0.1F, 0.9F, 0.4F]
  let probs = softmax(scores)
  if argmax(probs) != argmax(scores) {
    fail("softmax argmax = \{argmax(probs)}, expected \{argmax(scores)}")
  }
}

///|
test "softmax_numerically_stable_for_large_inputs" {
  // Large scores — naive softmax would overflow exp.
  let scores : Array[Float] = [1000.0F, 1001.0F, 1002.0F]
  let probs = softmax(scores)
  // Probabilities should be valid.
  let mut sum : Float = 0.0F
  for i in 0..<probs.length() {
    if probs[i] != probs[i] {  // NaN check
      fail("softmax[\{i}] = NaN (overflow?)")
    }
    if probs[i] < 0.0F {
      fail("softmax[\{i}] = \{probs[i]}, expected ≥ 0")
    }
    sum = sum + probs[i]
  }
  if (sum - 1.0F).abs() > 1.0e-5F {
    fail("softmax sum = \{sum}, expected ~1.0")
  }
  // argmax of input is 2 (highest), softmax argmax should also be 2.
  if argmax(probs) != 2 {
    fail("argmax of softmax(probs) = \{argmax(probs)}, expected 2")
  }
}

///|
test "softmax_constant_input_returns_uniform" {
  // All same score → uniform distribution.
  let scores : Array[Float] = [0.5F, 0.5F, 0.5F, 0.5F]
  let probs = softmax(scores)
  let expected = 1.0F / 4.0F
  for i in 0..<probs.length() {
    if (probs[i] - expected).abs() > 1.0e-5F {
      fail("probs[\{i}] = \{probs[i]}, expected ~\{expected}")
    }
  }
}

///|
test "top_k_basic" {
  let scores : Array[Float] = [0.1F, 0.5F, 0.9F, 0.3F, 0.7F]
  let top = top_k(scores, 3)
  if top.length() != 3 {
    fail("top.length() = \{top.length()}, expected 3")
  }
  // Expected top-3: indices 2 (0.9), 4 (0.7), 1 (0.5).
  if top[0] != 2 || top[1] != 4 || top[2] != 1 {
    fail("top = \{top}, expected [2, 4, 1]")
  }
}

///|
test "top_k_k_larger_than_n" {
  let scores : Array[Float] = [0.5F, 0.3F]
  let top = top_k(scores, 10)
  // Should return all indices, sorted by score desc.
  if top.length() != 2 {
    fail("top.length() = \{top.length()}, expected 2")
  }
  if top[0] != 0 || top[1] != 1 {
    fail("top = \{top}, expected [0, 1]")
  }
}

///|
test "top_k_k_zero_returns_empty" {
  let scores : Array[Float] = [0.5F, 0.3F]
  let top = top_k(scores, 0)
  if top.length() != 0 {
    fail("top_k with k=0 should return empty, got \{top.length()}")
  }
}

///|
test "classification_accuracy_perfect" {
  let predicted : Array[Int] = [0, 1, 2, 1, 0]
  let labels : Array[Int] = [0, 1, 2, 1, 0]
  if classification_accuracy(predicted, labels) != 1.0F {
    fail("perfect accuracy should be 1.0")
  }
}

///|
test "classification_accuracy_zero" {
  let predicted : Array[Int] = [0, 0, 0, 0]
  let labels : Array[Int] = [1, 1, 1, 1]
  if classification_accuracy(predicted, labels) != 0.0F {
    fail("zero accuracy should be 0.0")
  }
}

///|
test "classification_accuracy_partial" {
  let predicted : Array[Int] = [0, 1, 2, 1, 0]
  let labels : Array[Int] = [0, 1, 2, 0, 0]
  // Matches at indices 0, 1, 2, 4 → 4/5 = 0.8.
  if (classification_accuracy(predicted, labels) - 0.8F).abs() > 1.0e-6F {
    fail("expected ~0.8, got \{classification_accuracy(predicted, labels)}")
  }
}

///|
test "classification_accuracy_empty_returns_zero" {
  let predicted : Array[Int] = []
  let labels : Array[Int] = []
  if classification_accuracy(predicted, labels) != 0.0F {
    fail("empty input should return 0.0")
  }
}

///|
test "classification_accuracy_length_mismatch_returns_zero" {
  let predicted : Array[Int] = [0, 1]
  let labels : Array[Int] = [0]
  if classification_accuracy(predicted, labels) != 0.0F {
    fail("length mismatch should return 0.0 (guard)")
  }
}

///|
test "prediction_equals_argmax" {
  let scores : Array[Float] = [0.2F, 0.8F, 0.1F, 0.5F]
  if prediction(scores) != argmax(scores) {
    fail("prediction should equal argmax")
  }
}

///|
test "top_k_prediction_equals_top_k" {
  let scores : Array[Float] = [0.5F, 0.3F, 0.9F, 0.1F]
  let k = 2
  let p1 = top_k_prediction(scores, k)
  let p2 = top_k(scores, k)
  if p1.length() != p2.length() {
    fail("length mismatch")
  }
  for i in 0..<p1.length() {
    if p1[i] != p2[i] {
      fail("top_k_prediction differs from top_k at \{i}")
    }
  }
}