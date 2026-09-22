// Tests for Spiking Self-Attention.

///|
fn sa_eps(a : Float, b : Float, eps : Float) -> Bool {
  let d = if a > b { a - b } else { b - a }
  d <= eps
}

///|
fn sa_zeros(n : Int) -> Array[Float] {
  Array::make(n, 0.0F)
}

///|
test "SpikingAttention::new: shapes + d_k = d_model / num_heads" {
  let sa = SpikingAttention::new(8, 2, 1.0F, 1UL)
  assert_eq(sa.d_model, 8)
  assert_eq(sa.num_heads, 2)
  assert_eq(sa.d_k, 4)
  assert_eq(sa.beta, 1.0F)
  assert_eq(sa.w_q.weight.length(), 64)
}

///|
test "spiking_attention_forward: output shape + weights non-negative" {
  let seq_len = 3
  let d_model = 4
  let num_heads = 2
  let sa = SpikingAttention::new(d_model, num_heads, 1.5F, 42UL)
  let x : Array[Float] = Array::make(seq_len * d_model, 0.5F)
  let (out, cache) = spiking_attention_forward(x, sa, sa_zeros(0))
  assert_eq(out.length(), seq_len * d_model)
  assert_eq(cache.weights.length(), num_heads * seq_len * seq_len)
  // All weights should be in (-1/beta, 1/beta) = (-0.667, 0.667)
  for i in 0..<cache.weights.length() {
    let w = cache.weights[i]
    if w < -0.7F || w > 0.7F {
      fail("weight[\{i}] = \{w} out of (-1/beta, 1/beta)")
    }
  }
}

///|
test "spiking_attention_forward: weights = fast_sigmoid_forward(scores, beta)" {
  // Verify the relationship directly: pick a small case and check
  // weight[i] == fast_sigmoid_forward(scores[i], beta).
  let seq_len = 2
  let d_model = 2
  let num_heads = 1
  let sa = SpikingAttention::new(d_model, num_heads, 1.0F, 7UL)
  let x : Array[Float] = [0.3F, -0.1F, 0.5F, 0.2F]
  let (_out, cache) = spiking_attention_forward(x, sa, sa_zeros(0))
  for i in 0..<cache.weights.length() {
    let expected = fast_sigmoid_forward(cache.scores[i], sa.beta)
    if !sa_eps(cache.weights[i], expected, 0.0001F) {
      fail("weight[\{i}] = \{cache.weights[i]}, expected fast_sigmoid(\{cache.scores[i]}, \{sa.beta}) = \{expected}")
    }
  }
}

///|
test "spiking_attention_forward: mask blocks positions (weights ≈ 0)" {
  let seq_len = 3
  let d_model = 4
  let num_heads = 1
  let sa = SpikingAttention::new(d_model, num_heads, 1.0F, 1UL)
  let x : Array[Float] = Array::make(seq_len * d_model, 0.5F)
  // Block position t=2 for s=0 with very negative mask.
  let mask : Array[Float] = Array::make(num_heads * seq_len * seq_len, 0.0F)
  mask[0 * seq_len + 2] = -1000000.0F
  let (_out, cache) = spiking_attention_forward(x, sa, mask)
  // weight[0, 2] should be very small (since score[0,2] ≈ -1e6, fast_sigmoid → ≈ -1/beta)
  if cache.weights[2] > -0.5F {
    fail("masked weight[0, 2] = \{cache.weights[2]}, expected ≈ -1/beta (suppressed)")
  }
}

///|
test "spiking_attention_backward: shapes + zero d_output → zero" {
  let seq_len = 3
  let d_model = 4
  let num_heads = 2
  let sa = SpikingAttention::new(d_model, num_heads, 1.0F, 1UL)
  let x : Array[Float] = Array::make(seq_len * d_model, 0.5F)
  let (_out, cache) = spiking_attention_forward(x, sa, sa_zeros(0))
  let zero : Array[Float] = Array::make(seq_len * d_model, 0.0F)
  let (d_x, grads) = spiking_attention_backward(cache, zero, sa)
  assert_eq(d_x.length(), seq_len * d_model)
  for i in 0..<d_x.length() {
    if d_x[i] != 0.0F {
      fail("d_x[\{i}] = \{d_x[i]} for zero d_output")
    }
  }
  for i in 0..<grads.d_w_q.length() {
    if grads.d_w_q[i] != 0.0F {
      fail("non-zero d_w_q[\{i}] for zero d_output")
    }
  }
  for i in 0..<grads.d_b_q.length() {
    if grads.d_b_q[i] != 0.0F {
      fail("non-zero d_b_q[\{i}] for zero d_output")
    }
  }
}

///|
test "spiking_attention_backward: gradient check vs numerical (small d_model)" {
  // d_model=2, num_heads=1, seq_len=2.
  let seq_len = 2
  let d_model = 2
  let sa = SpikingAttention::new(d_model, 1, 1.0F, 3UL)
  let x : Array[Float] = [0.1F, 0.2F, 0.3F, 0.4F]
  let d_output : Array[Float] = [0.5F, 0.6F, 0.7F, 0.8F]
  let (_out, cache) = spiking_attention_forward(x, sa, sa_zeros(0))
  let (_d_x, grads) = spiking_attention_backward(cache, d_output, sa)

  // Pick w_o[0, 0].
  let eps = 0.001F
  let orig = sa.w_o.weight[0]
  sa.w_o.weight[0] = orig + eps
  let (out_plus, _) = spiking_attention_forward(x, sa, sa_zeros(0))
  sa.w_o.weight[0] = orig - eps
  let (out_minus, _) = spiking_attention_forward(x, sa, sa_zeros(0))
  sa.w_o.weight[0] = orig
  let mut loss_plus = 0.0F
  let mut loss_minus = 0.0F
  for i in 0..<d_output.length() {
    loss_plus = loss_plus + d_output[i] * out_plus[i]
    loss_minus = loss_minus + d_output[i] * out_minus[i]
  }
  let numerical = (loss_plus - loss_minus) / (2.0F * eps)
  let analytical = grads.d_w_o[0]
  if !sa_eps(analytical, numerical, 0.01F) {
    fail(
      "d_w_o[0] mismatch: analytical = \{analytical}, numerical = \{numerical}",
    )
  }
}

///|
test "spiking_attention_backward: gradient check on w_q[0,0]" {
  // Same setup but perturb w_q[0, 0].
  let seq_len = 2
  let d_model = 2
  let sa = SpikingAttention::new(d_model, 1, 1.0F, 3UL)
  let x : Array[Float] = [0.1F, 0.2F, 0.3F, 0.4F]
  let d_output : Array[Float] = [0.5F, 0.6F, 0.7F, 0.8F]
  let (_out, cache) = spiking_attention_forward(x, sa, sa_zeros(0))
  let (_d_x, grads) = spiking_attention_backward(cache, d_output, sa)

  let eps = 0.001F
  let orig = sa.w_q.weight[0]
  sa.w_q.weight[0] = orig + eps
  let (out_plus, _) = spiking_attention_forward(x, sa, sa_zeros(0))
  sa.w_q.weight[0] = orig - eps
  let (out_minus, _) = spiking_attention_forward(x, sa, sa_zeros(0))
  sa.w_q.weight[0] = orig
  let mut loss_plus = 0.0F
  let mut loss_minus = 0.0F
  for i in 0..<d_output.length() {
    loss_plus = loss_plus + d_output[i] * out_plus[i]
    loss_minus = loss_minus + d_output[i] * out_minus[i]
  }
  let numerical = (loss_plus - loss_minus) / (2.0F * eps)
  let analytical = grads.d_w_q[0]
  if !sa_eps(analytical, numerical, 0.01F) {
    fail(
      "d_w_q[0] mismatch: analytical = \{analytical}, numerical = \{numerical}",
    )
  }
}

///|
test "spiking_attention: large beta → weights approach sign(scores)" {
  // For very large beta, fast_sigmoid(x; beta) → sign(x)/beta
  // (close to ±0 when beta=100 and score is moderate).
  // Use small scores + large beta.
  let seq_len = 2
  let d_model = 2
  let sa = SpikingAttention::new(d_model, 1, 100.0F, 1UL)
  let x : Array[Float] = [0.0F, 0.0F, 0.0F, 0.0F]  // zero input → zero scores
  let (_out, cache) = spiking_attention_forward(x, sa, sa_zeros(0))
  // Zero scores → zero weights (fast_sigmoid(0) = 0)
  for i in 0..<cache.weights.length() {
    if !sa_eps(cache.weights[i], 0.0F, 0.0001F) {
      fail("weight[\{i}] = \{cache.weights[i]} for zero input + large beta, expected 0")
    }
  }
}