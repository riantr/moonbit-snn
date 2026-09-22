// cross_entropy_test.mbt 鈥?tests for cross_entropy_loss (v0.13.2).

///|
test "cross_entropy_loss / perfect predictions give loss = 0" {
  // log_probs with prob 1 at target class -> log_prob = 0 -> loss = 0.
  // log_probs = [[0, -inf, -inf, ...]] (batch 1, 3 classes).
  // For target=0, log_prob[0,0] = 0 -> loss = -0 = 0.
  let log_probs = Tensor::from([0.0F, -100.0F, -100.0F], [1, 3])
  let targets = Tensor::from([0.0F], [1])
  let loss = cross_entropy_loss(log_probs, targets)
  if (loss.mean.data[0] - 0.0F).abs() > 1.0e-5 {
    fail("perfect predictions should give loss 0, got \{loss.mean.data[0]}")
  }
}

///|
test "cross_entropy_loss / uniform over 3 classes -> log(3)" {
  // log_probs = [-log(3), -log(3), -log(3)]; loss = -log(3) ≈ 1.0986.
  // We use -1.0F as a rough stand-in: loss = 1.0.
  let log_probs = Tensor::from([-1.0F, -1.0F, -1.0F], [1, 3])
  let targets = Tensor::from([0.0F], [1])
  let loss = cross_entropy_loss(log_probs, targets)
  if (loss.mean.data[0] - 1.0F).abs() > 1.0e-5 {
    fail("expected loss 1.0, got \{loss.mean.data[0]}")
  }
}

///|
test "cross_entropy_loss / batch with mixed targets" {
  // batch = 3, classes = 4. Target losses = [0.5, 1.5, 2.5] -> mean = 1.5.
  // Construct log_probs so that picking the target gives the desired loss.
  // log_prob[batch, target] = -desired_loss.
  let log_probs = Tensor::from(
    [
      // batch 0, target 1 -> loss 0.5
      -10.0F, -0.5F, -10.0F, -10.0F,
      // batch 1, target 3 -> loss 1.5
      -10.0F, -10.0F, -10.0F, -1.5F,
      // batch 2, target 0 -> loss 2.5
      -2.5F, -10.0F, -10.0F, -10.0F,
    ],
    [3, 4],
  )
  let targets = Tensor::from([1.0F, 3.0F, 0.0F], [3])
  let loss = cross_entropy_loss(log_probs, targets)
  if (loss.mean.data[0] - 1.5F).abs() > 1.0e-5 {
    fail("mean expected 1.5, got \{loss.mean.data[0]}")
  }
  // Per-batch values.
  let expected : Array[Float] = [0.5F, 1.5F, 2.5F]
  for i in 0..<3 {
    if (loss.per_batch.data[i] - expected[i]).abs() > 1.0e-5 {
      fail("per_batch[\{i}] expected \{expected[i]}, got \{loss.per_batch.data[i]}")
    }
  }
}

///|
test "cross_entropy_loss / softmax + cross_entropy pipeline" {
  // Realistic pipeline:
  //   logits -> log_softmax -> cross_entropy -> mean loss
  // logits = [[1, 2, 3], [0, 0, 10]] -> log_softmax produces
  // log-probabilities. Cross entropy against targets [2, 2] picks
  // the highest-logit class. Verify loss values are positive and the
  // second batch's loss is much smaller (since target=2 is the very-
  // dominant class).
  let logits = Tensor::from(
    [1.0F, 2.0F, 3.0F,
     0.0F, 0.0F, 10.0F],
    [2, 3],
  )
  let log_probs = tensor_log_softmax(logits)
  let targets = Tensor::from([2.0F, 2.0F], [2])
  let loss = cross_entropy_loss(log_probs, targets)
  // Both losses should be small (target is the max-logit class).
  let l0 = loss.per_batch.data[0]
  let l1 = loss.per_batch.data[1]
  if l0 < 0.0F || l1 < 0.0F {
    fail("losses should be non-negative, got \{l0} \{l1}")
  }
  // l1 should be smaller than l0 because batch 1 has a much stronger
  // preference for class 2 (10 vs 0).
  if l1 > l0 {
    fail("batch 1 loss (\{l1}) should be smaller than batch 0 loss (\{l0})")
  }
  // Mean should be (l0 + l1) / 2.
  let mean_calc = (l0 + l1) / 2.0F
  if (loss.mean.data[0] - mean_calc).abs() > 1.0e-5 {
    fail("mean (\{loss.mean.data[0]}) should match (l0+l1)/2 (\{mean_calc})")
  }
}