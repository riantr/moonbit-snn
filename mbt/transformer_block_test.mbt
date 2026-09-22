// Tests for the Pre-Norm Transformer block.

///|
fn tb_eps(a : Float, b : Float, eps : Float) -> Bool {
  let d = if a > b { a - b } else { b - a }
  d <= eps
}

///|
fn tb_zeros(n : Int) -> Array[Float] {
  Array::make(n, 0.0F)
}

///|
test "TransformerBlock::new: shapes + num_heads divides d_model" {
  let block = TransformerBlock::new(4, 2, 1UL)
  assert_eq(block.d_model, 4)
  assert_eq(block.num_heads, 2)
  assert_eq(block.d_ff, 16) // 4 * 4
  assert_eq(block.ln_1.gamma.length(), 4)
  assert_eq(block.ln_2.gamma.length(), 4)
  assert_eq(block.ffn_w1.in_features, 4)
  assert_eq(block.ffn_w1.out_features, 16)
  assert_eq(block.ffn_w2.in_features, 16)
  assert_eq(block.ffn_w2.out_features, 4)
}

///|
test "TransformerBlock::new: custom d_ff overrides default" {
  let block = TransformerBlock::new(8, 4, 1UL, d_ff=16)
  assert_eq(block.d_ff, 16)
}

///|
test "transformer_block_forward: output shape = input shape" {
  let seq_len = 3
  let d_model = 4
  let block = TransformerBlock::new(d_model, 2, 1UL)
  let x : Array[Float] = [0.1F, 0.2F, 0.3F, 0.4F,
                          0.5F, 0.6F, 0.7F, 0.8F,
                          0.9F, 1.0F, 1.1F, 1.2F]
  let (out, cache) = transformer_block_forward(x, block, tb_zeros(0))
  assert_eq(out.length(), seq_len * d_model)
  // Cache sanity
  assert_eq(cache.seq_len, seq_len)
  assert_eq(cache.x_norm1.length(), seq_len * d_model)
  assert_eq(cache.x_norm2.length(), seq_len * d_model)
  assert_eq(cache.ffn_hidden_pre_gelu.length(), seq_len * block.d_ff)
  assert_eq(cache.ffn_hidden_post_gelu.length(), seq_len * block.d_ff)
}

///|
test "transformer_block_forward: deterministic same-seed" {
  let block_a = TransformerBlock::new(4, 2, 7UL)
  let block_b = TransformerBlock::new(4, 2, 7UL)
  let x : Array[Float] = [0.1F, 0.2F, 0.3F, 0.4F,
                          0.5F, 0.6F, 0.7F, 0.8F,
                          0.9F, 1.0F, 1.1F, 1.2F]
  let (out_a, _) = transformer_block_forward(x, block_a, tb_zeros(0))
  let (out_b, _) = transformer_block_forward(x, block_b, tb_zeros(0))
  for i in 0..<out_a.length() {
    if out_a[i] != out_b[i] {
      fail("diff at \{i}: \{out_a[i]} vs \{out_b[i]}")
    }
  }
}

///|
test "transformer_block_backward: shapes correct" {
  let seq_len = 3
  let d_model = 4
  let block = TransformerBlock::new(d_model, 2, 1UL)
  let x : Array[Float] = Array::make(seq_len * d_model, 0.5F)
  let (_out, cache) = transformer_block_forward(x, block, tb_zeros(0))
  let d_output : Array[Float] = Array::make(seq_len * d_model, 0.25F)
  let (d_x, grads) = transformer_block_backward(cache, d_output, block)
  assert_eq(d_x.length(), seq_len * d_model)
  assert_eq(grads.ln1_d_gamma.length(), d_model)
  assert_eq(grads.ln1_d_beta.length(), d_model)
  assert_eq(grads.ln2_d_gamma.length(), d_model)
  assert_eq(grads.ln2_d_beta.length(), d_model)
  assert_eq(grads.ffn_d_w1.length(), d_model * block.d_ff)
  assert_eq(grads.ffn_d_b1.length(), block.d_ff)
  assert_eq(grads.ffn_d_w2.length(), block.d_ff * d_model)
  assert_eq(grads.ffn_d_b2.length(), d_model)
  assert_eq(grads.mha_grad.d_w_q.length(), d_model * d_model)
}

///|
test "transformer_block_backward: zero d_output → zero d_x + zero all grads" {
  let seq_len = 3
  let d_model = 4
  let block = TransformerBlock::new(d_model, 2, 1UL)
  let x : Array[Float] = Array::make(seq_len * d_model, 0.5F)
  let (_out, cache) = transformer_block_forward(x, block, tb_zeros(0))
  let zero_d : Array[Float] = Array::make(seq_len * d_model, 0.0F)
  let (d_x, grads) = transformer_block_backward(cache, zero_d, block)
  for i in 0..<d_x.length() {
    if d_x[i] != 0.0F {
      fail("d_x[\{i}] = \{d_x[i]} for zero d_output")
    }
  }
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
}

///|
test "transformer_block_backward: gradient check vs numerical (tiny d_model)" {
  // d_model=2, num_heads=1, d_ff=4, seq_len=2.
  let seq_len = 2
  let d_model = 2
  let block = TransformerBlock::new(d_model, 1, 3UL, d_ff=4)
  let x : Array[Float] = [0.1F, 0.2F, 0.3F, 0.4F]
  let d_output : Array[Float] = [0.5F, 0.6F, 0.7F, 0.8F]
  let (_out, cache) = transformer_block_forward(x, block, tb_zeros(0))
  let (_d_x, grads) = transformer_block_backward(cache, d_output, block)

  // Pick FFN linear2 weight [0, 0].
  let eps = 0.001F
  let orig = block.ffn_w2.weight[0]
  block.ffn_w2.weight[0] = orig + eps
  let (out_plus, _) = transformer_block_forward(x, block, tb_zeros(0))
  block.ffn_w2.weight[0] = orig - eps
  let (out_minus, _) = transformer_block_forward(x, block, tb_zeros(0))
  block.ffn_w2.weight[0] = orig
  let mut loss_plus = 0.0F
  let mut loss_minus = 0.0F
  for i in 0..<d_output.length() {
    loss_plus = loss_plus + d_output[i] * out_plus[i]
    loss_minus = loss_minus + d_output[i] * out_minus[i]
  }
  let numerical = (loss_plus - loss_minus) / (2.0F * eps)
  let analytical = grads.ffn_d_w2[0]
  if !tb_eps(analytical, numerical, 0.05F) {
    fail(
      "ffn_d_w2[0] mismatch: analytical = \{analytical}, numerical = \{numerical}",
    )
  }
}