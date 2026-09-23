// Tests for the consolidated SAttention module
// (multi-head + cross-attention variants).

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
test "SpikingMultiHeadAttention: shape + d_k = d_model / num_heads" {
  let sa = SpikingMultiHeadAttention::new(8, 2, 1.0F, 1UL)
  assert_eq(sa.d_model, 8)
  assert_eq(sa.num_heads, 2)
  assert_eq(sa.d_k, 4)
}

///|
test "SpikingSelfAttention: num_heads=1 convenience" {
  let sa = spiking_self_attention_new(8, 1.0F, 7UL)
  assert_eq(sa.num_heads, 1)
  assert_eq(sa.d_k, 8)  // d_k == d_model for single-head
}

///|
test "SpikingCrossAttention::new: shape" {
  let sa = SpikingCrossAttention::new(4, 2, 1.0F, 1UL)
  assert_eq(sa.d_model, 4)
  assert_eq(sa.num_heads, 2)
  assert_eq(sa.d_k, 2)
  assert_eq(sa.w_q.weight.length(), 16)
}

///|
test "spiking_cross_attention_forward: output shape = (seq_len_q, d_model)" {
  let sa = SpikingCrossAttention::new(4, 2, 1.0F, 1UL)
  let seq_len_q = 3
  let seq_len_kv = 5  // Different from seq_len_q (cross-attention).
  let x_q : Array[Float] = Array::make(seq_len_q * 4, 0.5F)
  let x_kv : Array[Float] = Array::make(seq_len_kv * 4, 0.3F)
  let (out, cache) = spiking_cross_attention_forward(x_q, x_kv, sa, sa_zeros(0))
  // Output shape uses seq_len_q, not seq_len_kv.
  assert_eq(out.length(), seq_len_q * 4)
  // Cache shape sanity.
  assert_eq(cache.seq_len_q, seq_len_q)
  assert_eq(cache.seq_len_kv, seq_len_kv)
  assert_eq(cache.q.length(), seq_len_q * 4)
  assert_eq(cache.k.length(), seq_len_kv * 4)
  assert_eq(cache.scores.length(), 2 * seq_len_q * seq_len_kv)
}

///|
test "spiking_cross_attention_forward: degenerates to multi-head self when x_q == x_kv" {
  let seq_len = 3
  let d_model = 4
  let num_heads = 2
  let sa_mh = SpikingMultiHeadAttention::new(d_model, num_heads, 1.0F, 1UL)
  let sa_cr = SpikingCrossAttention::new(d_model, num_heads, 1.0F, 1UL)
  // Copy weights from self to cross so they produce identical output
  // when given the same input as both x_q and x_kv.
  let n_w = sa_cr.w_q.weight.length()
  for i in 0..<n_w {
    sa_cr.w_q.weight[i] = sa_mh.w_q.weight[i]
    sa_cr.w_k.weight[i] = sa_mh.w_k.weight[i]
    sa_cr.w_v.weight[i] = sa_mh.w_v.weight[i]
    sa_cr.w_o.weight[i] = sa_mh.w_o.weight[i]
  }
  for i in 0..<sa_cr.w_q.bias.length() {
    sa_cr.w_q.bias[i] = sa_mh.w_q.bias[i]
    sa_cr.w_k.bias[i] = sa_mh.w_k.bias[i]
    sa_cr.w_v.bias[i] = sa_mh.w_v.bias[i]
    sa_cr.w_o.bias[i] = sa_mh.w_o.bias[i]
  }
  let x : Array[Float] = [0.1F, 0.2F, 0.3F, 0.4F, 0.5F, 0.6F, 0.7F, 0.8F, 0.9F, 1.0F, 1.1F, 1.2F]
  let (out_mh, _) = spiking_multi_head_attention_forward(x, sa_mh, sa_zeros(0))
  let (out_cr, _) = spiking_cross_attention_forward(x, x, sa_cr, sa_zeros(0))
  for i in 0..<out_mh.length() {
    if !sa_eps(out_mh[i], out_cr[i], 0.0001F) {
      fail("at \{i}: multi-head = \{out_mh[i]}, cross-degenerate = \{out_cr[i]}")
    }
  }
}

///|
test "spiking_cross_attention_backward: shapes + zero d_output → zero" {
  let seq_len_q = 3
  let seq_len_kv = 5
  let d_model = 4
  let sa = SpikingCrossAttention::new(d_model, 2, 1.0F, 1UL)
  let x_q : Array[Float] = Array::make(seq_len_q * d_model, 0.5F)
  let x_kv : Array[Float] = Array::make(seq_len_kv * d_model, 0.5F)
  let (_out, cache) = spiking_cross_attention_forward(
    x_q, x_kv, sa, sa_zeros(0),
  )
  let zero : Array[Float] = Array::make(seq_len_q * d_model, 0.0F)
  let (d_x_q, d_x_kv, grads) = spiking_cross_attention_backward(
    cache, zero, sa,
  )
  assert_eq(d_x_q.length(), seq_len_q * d_model)
  assert_eq(d_x_kv.length(), seq_len_kv * d_model)
  for i in 0..<d_x_q.length() {
    if d_x_q[i] != 0.0F {
      fail("d_x_q[\{i}] = \{d_x_q[i]} for zero d_output")
    }
  }
  for i in 0..<d_x_kv.length() {
    if d_x_kv[i] != 0.0F {
      fail("d_x_kv[\{i}] = \{d_x_kv[i]} for zero d_output")
    }
  }
  // SA grads all zero
  for i in 0..<grads.d_w_q.length() {
    if grads.d_w_q[i] != 0.0F {
      fail("non-zero d_w_q[\{i}]")
    }
  }
}

///|
test "spiking_cross_attention_backward: gradient check vs numerical" {
  let seq_len_q = 2
  let seq_len_kv = 3
  let d_model = 2
  let sa = SpikingCrossAttention::new(d_model, 1, 1.0F, 3UL)
  let x_q : Array[Float] = [0.1F, 0.2F, 0.3F, 0.4F]
  let x_kv : Array[Float] = [0.5F, 0.6F, 0.7F, 0.8F, 0.9F, 1.0F]
  let d_output : Array[Float] = [0.5F, 0.6F, 0.7F, 0.8F]
  let (_out, cache) = spiking_cross_attention_forward(
    x_q, x_kv, sa, sa_zeros(0),
  )
  let (_d_x_q, _d_x_kv, grads) = spiking_cross_attention_backward(
    cache, d_output, sa,
  )

  // Perturb w_o[0] and check analytical vs numerical.
  let eps = 0.001F
  let orig = sa.w_o.weight[0]
  sa.w_o.weight[0] = orig + eps
  let (out_plus, _) = spiking_cross_attention_forward(x_q, x_kv, sa, sa_zeros(0))
  sa.w_o.weight[0] = orig - eps
  let (out_minus, _) = spiking_cross_attention_forward(x_q, x_kv, sa, sa_zeros(0))
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
      "cross-attn d_w_o[0] mismatch: analytical = \{analytical}, numerical = \{numerical}",
    )
  }
}

///|
test "SAttention: all three variants coexist under sattention.mbt module" {
  // Compile-time check that all three types can be constructed.
  let _ = SpikingMultiHeadAttention::new(4, 2, 1.0F, 1UL)
  let _ = spiking_self_attention_new(4, 1.0F, 1UL)
  let _ = SpikingCrossAttention::new(4, 2, 1.0F, 1UL)
  // Backward-compat SpikingAttention still works.
  let sa = SpikingAttention::new(4, 2, 1.0F, 1UL)
  assert_eq(sa.num_heads, 2)
}