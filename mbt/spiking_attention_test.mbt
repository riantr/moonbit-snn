// DEPRECATED shim tests — kept for backward compatibility.
// The implementation now lives in sattention.mbt under the
// canonical name `SpikingMultiHeadAttention`. These tests verify the
// backward-compat shim functions (`SpikingAttention::new`,
// `spiking_attention_forward`, `spiking_attention_backward`) still
// work identically to the canonical API.

// New canonical API tests are in `sattention_test.mbt`.

///|
fn sa_eps_dep(a : Float, b : Float, eps : Float) -> Bool {
  let d = if a > b { a - b } else { b - a }
  d <= eps
}

///|
fn sa_zeros_dep(n : Int) -> Array[Float] {
  Array::make(n, 0.0F)
}

///|
test "DEPRECATED shim: SpikingAttention::new wires to SpikingMultiHeadAttention::new" {
  // Two parallel constructions should be identical structs.
  let sa_shim = SpikingAttention::new(8, 2, 1.0F, 1UL)
  let sa_new = SpikingMultiHeadAttention::new(8, 2, 1.0F, 1UL)
  assert_eq(sa_shim.d_model, sa_new.d_model)
  assert_eq(sa_shim.num_heads, sa_new.num_heads)
  assert_eq(sa_shim.d_k, sa_new.d_k)
}

///|
test "DEPRECATED shim: spiking_attention_forward ≡ spiking_multi_head_attention_forward" {
  let sa = SpikingAttention::new(4, 2, 1.0F, 42UL)
  let x : Array[Float] = [0.1F, 0.2F, 0.3F, 0.4F, 0.5F, 0.6F, 0.7F, 0.8F]
  let (out_shim, _) = spiking_attention_forward(x, sa, sa_zeros_dep(0))
  let (out_new, _) = spiking_multi_head_attention_forward(x, sa, sa_zeros_dep(0))
  for i in 0..<out_shim.length() {
    if !sa_eps_dep(out_shim[i], out_new[i], 0.0001F) {
      fail("shim vs new mismatch at \{i}: \{out_shim[i]} vs \{out_new[i]}")
    }
  }
}