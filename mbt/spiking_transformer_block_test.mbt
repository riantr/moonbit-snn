// Tests for the Spiking Pre-Norm Transformer block.

///|
fn stb_eps(a : Float, b : Float, eps : Float) -> Bool {
  let d = if a > b { a - b } else { b - a }
  d <= eps
}

///|
fn stb_zeros(n : Int) -> Array[Float] {
  Array::make(n, 0.0F)
}

///|
test "SpikingTransformerBlock::new: shapes + default d_ff = 4·d_model" {
  let block = SpikingTransformerBlock::new(4, 2, 1.0F, 1UL)
  assert_eq(block.d_model, 4)
  assert_eq(block.num_heads, 2)
  assert_eq(block.d_ff, 16)
  assert_eq(block.beta, 1.0F)
  assert_eq(block.ln_1.gamma.length(), 4)
  assert_eq(block.ffn_w1.out_features, 16)
}

///|
test "spiking_transformer_block_forward: output shape = input shape" {
  let seq_len = 3
  let d_model = 4
  let block = SpikingTransformerBlock::new(d_model, 2, 1.5F, 1UL)
  let x : Array[Float] = Array::make(seq_len * d_model, 0.5F)
  let (out, cache) = spiking_transformer_block_forward(x, block, stb_zeros(0))
  assert_eq(out.length(), seq_len * d_model)
  assert_eq(cache.seq_len, seq_len)
  assert_eq(cache.x_norm1.length(), seq_len * d_model)
  assert_eq(cache.x_norm2.length(), seq_len * d_model)
  assert_eq(cache.sa_cache.weights.length(), 2 * seq_len * seq_len)
}

///|
test "spiking_transformer_block_forward: deterministic same-seed" {
  let block_a = SpikingTransformerBlock::new(4, 2, 1.0F, 7UL)
  let block_b = SpikingTransformerBlock::new(4, 2, 1.0F, 7UL)
  let x : Array[Float] = Array::make(8, 0.5F)
  let (out_a, _) = spiking_transformer_block_forward(x, block_a, stb_zeros(0))
  let (out_b, _) = spiking_transformer_block_forward(x, block_b, stb_zeros(0))
  for i in 0..<out_a.length() {
    if out_a[i] != out_b[i] {
      fail("diff at \{i}: \{out_a[i]} vs \{out_b[i]}")
    }
  }
}

///|
test "spiking_transformer_block_backward: shapes + zero d_output → zero" {
  let seq_len = 3
  let d_model = 4
  let block = SpikingTransformerBlock::new(d_model, 2, 1.0F, 1UL)
  let x : Array[Float] = Array::make(seq_len * d_model, 0.5F)
  let (_out, cache) = spiking_transformer_block_forward(x, block, stb_zeros(0))
  let zero : Array[Float] = Array::make(seq_len * d_model, 0.0F)
  let (d_x, grads) = spiking_transformer_block_backward(cache, zero, block)
  assert_eq(d_x.length(), seq_len * d_model)
  for i in 0..<d_x.length() {
    if d_x[i] != 0.0F {
      fail("d_x[\{i}] = \{d_x[i]} for zero d_output")
    }
  }
  // All 8 grad arrays zero
  for i in 0..<grads.ln1_d_gamma.length() {
    if grads.ln1_d_gamma[i] != 0.0F || grads.ln1_d_beta[i] != 0.0F {
      fail("non-zero LN1 grad at \{i}")
    }
  }
  for i in 0..<grads.ffn_d_w1.length() {
    if grads.ffn_d_w1[i] != 0.0F {
      fail("non-zero ffn_d_w1 at \{i}")
    }
  }
  for i in 0..<grads.ffn_d_b1.length() {
    if grads.ffn_d_b1[i] != 0.0F {
      fail("non-zero ffn_d_b1 at \{i}")
    }
  }
  for i in 0..<grads.ffn_d_w2.length() {
    if grads.ffn_d_w2[i] != 0.0F {
      fail("non-zero ffn_d_w2 at \{i}")
    }
  }
  for i in 0..<grads.ffn_d_b2.length() {
    if grads.ffn_d_b2[i] != 0.0F {
      fail("non-zero ffn_d_b2 at \{i}")
    }
  }
  for i in 0..<grads.sa_grad.d_w_q.length() {
    if grads.sa_grad.d_w_q[i] != 0.0F {
      fail("non-zero sa_grad.d_w_q at \{i}")
    }
  }
}

///|
test "spiking_transformer_block_backward: gradient check vs numerical" {
  let seq_len = 2
  let d_model = 2
  let block = SpikingTransformerBlock::new(d_model, 1, 1.0F, 3UL, d_ff=4)
  let x : Array[Float] = [0.1F, 0.2F, 0.3F, 0.4F]
  let d_output : Array[Float] = [0.5F, 0.6F, 0.7F, 0.8F]
  let (_out, cache) = spiking_transformer_block_forward(x, block, stb_zeros(0))
  let (_d_x, grads) = spiking_transformer_block_backward(cache, d_output, block)

  // Perturb ffn_w2[0] and check analytical vs numerical.
  let eps = 0.001F
  let orig = block.ffn_w2.weight[0]
  block.ffn_w2.weight[0] = orig + eps
  let (out_plus, _) = spiking_transformer_block_forward(x, block, stb_zeros(0))
  block.ffn_w2.weight[0] = orig - eps
  let (out_minus, _) = spiking_transformer_block_forward(x, block, stb_zeros(0))
  block.ffn_w2.weight[0] = orig
  let mut loss_plus = 0.0F
  let mut loss_minus = 0.0F
  for i in 0..<d_output.length() {
    loss_plus = loss_plus + d_output[i] * out_plus[i]
    loss_minus = loss_minus + d_output[i] * out_minus[i]
  }
  let numerical = (loss_plus - loss_minus) / (2.0F * eps)
  let analytical = grads.ffn_d_w2[0]
  if !stb_eps(analytical, numerical, 0.05F) {
    fail("ffn_d_w2[0] mismatch: analytical = \{analytical}, numerical = \{numerical}")
  }
}

///|
test "spiking_transformer_block_backward: gradient check on sa.w_q[0]" {
  // Same setup; perturb spiking-attention w_q[0].
  let seq_len = 2
  let d_model = 2
  let block = SpikingTransformerBlock::new(d_model, 1, 1.0F, 3UL, d_ff=4)
  let x : Array[Float] = [0.1F, 0.2F, 0.3F, 0.4F]
  let d_output : Array[Float] = [0.5F, 0.6F, 0.7F, 0.8F]
  let (_out, cache) = spiking_transformer_block_forward(x, block, stb_zeros(0))
  let (_d_x, grads) = spiking_transformer_block_backward(cache, d_output, block)

  let eps = 0.001F
  let orig = block.sa.w_q.weight[0]
  block.sa.w_q.weight[0] = orig + eps
  let (out_plus, _) = spiking_transformer_block_forward(x, block, stb_zeros(0))
  block.sa.w_q.weight[0] = orig - eps
  let (out_minus, _) = spiking_transformer_block_forward(x, block, stb_zeros(0))
  block.sa.w_q.weight[0] = orig
  let mut loss_plus = 0.0F
  let mut loss_minus = 0.0F
  for i in 0..<d_output.length() {
    loss_plus = loss_plus + d_output[i] * out_plus[i]
    loss_minus = loss_minus + d_output[i] * out_minus[i]
  }
  let numerical = (loss_plus - loss_minus) / (2.0F * eps)
  let analytical = grads.sa_grad.d_w_q[0]
  if !stb_eps(analytical, numerical, 0.01F) {
    fail("sa_grad.d_w_q[0] mismatch: analytical = \{analytical}, numerical = \{numerical}")
  }
}