// Tests for Multi-Head Self-Attention (forward + backward + mask).

///|
fn mha_eps(a : Float, b : Float, eps : Float) -> Bool {
  let d = if a > b { a - b } else { b - a }
  d <= eps
}

///|
fn zeros(n : Int) -> Array[Float] {
  Array::make(n, 0.0F)
}

///|
test "MultiHeadAttention::new weight shapes + num_heads divides d_model" {
  let mha = MultiHeadAttention::new(8, 2, 1UL)
  assert_eq(mha.d_model, 8)
  assert_eq(mha.num_heads, 2)
  assert_eq(mha.d_k, 4)
  assert_eq(mha.w_q.in_features, 8)
  assert_eq(mha.w_q.out_features, 8)
  assert_eq(mha.w_q.weight.length(), 64)
  assert_eq(mha.w_q.bias.length(), 8)
  // Biases are zero by construction
  for i in 0..<8 {
    assert_eq(mha.w_q.bias[i], 0.0F)
    assert_eq(mha.w_k.bias[i], 0.0F)
    assert_eq(mha.w_v.bias[i], 0.0F)
    assert_eq(mha.w_o.bias[i], 0.0F)
  }
}

///|


///|
test "forward: output shape + softmax weights sum to 1 across key axis" {
  let seq_len = 3
  let d_model = 4
  let num_heads = 2
  let mha = MultiHeadAttention::new(d_model, num_heads, 42UL)
  // Input: simple constant 0.1
  let x : Array[Float] = Array::make(seq_len * d_model, 0.1F)
  let (out, cache) = multi_head_attention_forward(x, mha, zeros(0))
  assert_eq(out.length(), seq_len * d_model)
  // Weights shape + sum-to-1 per (h, s)
  assert_eq(cache.weights.length(), num_heads * seq_len * seq_len)
  for h in 0..<num_heads {
    for s in 0..<seq_len {
      let mut sum_w = 0.0F
      for t in 0..<seq_len {
        sum_w = sum_w + cache.weights[h * seq_len * seq_len + s * seq_len + t]
      }
      if !mha_eps(sum_w, 1.0F, 0.0001F) {
        fail("weights[(\{h}, \{s})] sum = \{sum_w}, expected 1.0")
      }
    }
  }
}

///|
test "forward: same seed + same input → identical output (deterministic)" {
  let mha_a = MultiHeadAttention::new(4, 2, 7UL)
  let mha_b = MultiHeadAttention::new(4, 2, 7UL)
  let x : Array[Float] = [0.1F, 0.2F, 0.3F, 0.4F, 0.5F, 0.6F, 0.7F, 0.8F]
  let (out_a, _) = multi_head_attention_forward(x, mha_a, zeros(0))
  let (out_b, _) = multi_head_attention_forward(x, mha_b, zeros(0))
  for i in 0..<out_a.length() {
    if out_a[i] != out_b[i] {
      fail("diff at \{i}: \{out_a[i]} vs \{out_b[i]}")
    }
  }
}

///|
test "forward: heads are independent — different seeds produce different outputs" {
  let mha_a = MultiHeadAttention::new(4, 2, 7UL)
  let mha_b = MultiHeadAttention::new(4, 2, 8UL)
  let x : Array[Float] = [0.1F, 0.2F, 0.3F, 0.4F, 0.5F, 0.6F, 0.7F, 0.8F]
  let (out_a, _) = multi_head_attention_forward(x, mha_a, zeros(0))
  let (out_b, _) = multi_head_attention_forward(x, mha_b, zeros(0))
  let mut differ = false
  for i in 0..<out_a.length() {
    if out_a[i] != out_b[i] {
      differ = true
      break
    }
  }
  if !differ {
    fail("different seeds produced identical output (impossible with non-zero init)")
  }
}

///|
test "forward + mask: blocked positions contribute zero weight" {
  let seq_len = 3
  let d_model = 4
  let num_heads = 2
  let mha = MultiHeadAttention::new(d_model, num_heads, 1UL)
  let x : Array[Float] = Array::make(seq_len * d_model, 0.5F)
  // Mask: block position t=2 for (h=0, s=0,1) and all positions for (h=0, s=2).
  // Use a very large negative value to simulate -inf for softmax.
  let mask : Array[Float] = zeros(num_heads * seq_len * seq_len)
  for h in 0..<num_heads {
    for s in 0..<seq_len {
      for t in 0..<seq_len {
        // Block diagonal above-right: positions where t > s.
        if t > s {
          mask[h * seq_len * seq_len + s * seq_len + t] = -1000000.0F
        }
      }
    }
  }
  let (_out, cache) = multi_head_attention_forward(x, mha, mask)
  for h in 0..<num_heads {
    for s in 0..<seq_len {
      let s_row = h * seq_len * seq_len + s * seq_len
      for t in 0..<seq_len {
        if t > s {
          // Weight at a masked-out position should be 0.
          if cache.weights[s_row + t] > 0.0001F {
            fail("masked weight[(\{h},\{s},\{t})] = \{cache.weights[s_row + t]}, expected ≈ 0")
          }
        }
      }
      // Sum-to-1 across unmasked positions still holds.
      let mut sum_w = 0.0F
      for t in 0..<seq_len {
        sum_w = sum_w + cache.weights[s_row + t]
      }
      if !mha_eps(sum_w, 1.0F, 0.001F) {
        fail("masked sum = \{sum_w} at (\{h},\{s}), expected 1.0")
      }
    }
  }
}

///|
test "backward: d_input / d_weight shapes match" {
  let seq_len = 3
  let d_model = 4
  let num_heads = 2
  let mha = MultiHeadAttention::new(d_model, num_heads, 1UL)
  let x : Array[Float] = Array::make(seq_len * d_model, 0.5F)
  let (_out, cache) = multi_head_attention_forward(x, mha, zeros(0))
  let d_output : Array[Float] = Array::make(seq_len * d_model, 0.25F)
  let (d_x, grads) = multi_head_attention_backward(cache, d_output, mha)
  assert_eq(d_x.length(), seq_len * d_model)
  assert_eq(grads.d_w_q.length(), d_model * d_model)
  assert_eq(grads.d_b_q.length(), d_model)
  assert_eq(grads.d_w_o.length(), d_model * d_model)
}

///|
test "backward: zero d_output → zero d_input + zero grads" {
  let seq_len = 3
  let d_model = 4
  let num_heads = 2
  let mha = MultiHeadAttention::new(d_model, num_heads, 1UL)
  let x : Array[Float] = Array::make(seq_len * d_model, 0.5F)
  let (_out, cache) = multi_head_attention_forward(x, mha, zeros(0))
  let zero_d : Array[Float] = Array::make(seq_len * d_model, 0.0F)
  let (d_x, grads) = multi_head_attention_backward(cache, zero_d, mha)
  for i in 0..<d_x.length() {
    if d_x[i] != 0.0F {
      fail("d_x[\{i}] = \{d_x[i]} for zero d_output")
    }
  }
  for i in 0..<grads.d_w_q.length() {
    if grads.d_w_q[i] != 0.0F {
      fail("d_w_q[\{i}] = \{grads.d_w_q[i]} for zero d_output")
    }
  }
  for i in 0..<grads.d_b_q.length() {
    if grads.d_b_q[i] != 0.0F {
      fail("d_b_q[\{i}] = \{grads.d_b_q[i]} for zero d_output")
    }
  }
  if grads.d_w_o.length() != d_model * d_model {
    fail("d_w_o length mismatch")
  }
}

///|
test "backward: gradient check vs numerical (central difference)" {
  // Tiny setup so the test runs fast: d_model=2, num_heads=1, seq_len=2.
  let seq_len = 2
  let d_model = 2
  let num_heads = 1
  let mha = MultiHeadAttention::new(d_model, num_heads, 3UL)
  let x : Array[Float] = [0.1F, 0.2F, 0.3F, 0.4F]
  let d_output : Array[Float] = [0.5F, 0.6F, 0.7F, 0.8F]
  let (_out, cache) = multi_head_attention_forward(x, mha, zeros(0))
  let (_d_x, grads) = multi_head_attention_backward(cache, d_output, mha)

  // Numerical gradient via central difference on a single scalar weight.
  // Pick weight w_o[0, 0] (in_features=0, out_features=0).
  let eps = 0.001F
  let orig = mha.w_o.weight[0]
  mha.w_o.weight[0] = orig + eps
  let (out_plus, _) = multi_head_attention_forward(x, mha, zeros(0))
  mha.w_o.weight[0] = orig - eps
  let (out_minus, _) = multi_head_attention_forward(x, mha, zeros(0))
  mha.w_o.weight[0] = orig
  let mut loss_plus = 0.0F
  let mut loss_minus = 0.0F
  for i in 0..<d_output.length() {
    loss_plus = loss_plus + d_output[i] * out_plus[i]
    loss_minus = loss_minus + d_output[i] * out_minus[i]
  }
  let numerical = (loss_plus - loss_minus) / (2.0F * eps)
  let analytical = grads.d_w_o[0]
  if !mha_eps(analytical, numerical, 0.01F) {
    fail(
      "d_w_o[0] mismatch: analytical = \{analytical}, numerical = \{numerical}",
    )
  }
}