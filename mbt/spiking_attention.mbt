// spiking_attention.mbt — DEPRECATED shim around the consolidated
// SAttention module (sattention.mbt).
//
// The multi-head self-attention implementation now lives in
// `sattention.mbt` under the canonical name `SpikingMultiHeadAttention`.
// This file keeps the v0.24.0 function signatures (`SpikingAttention::new`,
// `spiking_attention_forward`, `spiking_attention_backward`) wired to
// the new implementation so existing tests continue to work.
//
// New code should import directly from `sattention.mbt`:
//
//   `SpikingMultiHeadAttention::new(...)`  (multi-head, default)
//   `SpikingCrossAttention::new(...)`     (cross-attention)
//
// This file remains only for backward compatibility and will be removed
// in a future release.

///|
/// Backward-compat: build a SpikingMultiHeadAttention under the
/// historical `SpikingAttention` name (same struct shape).
pub fn SpikingAttention::new(
  d_model : Int,
  num_heads : Int,
  beta : Float,
  seed : UInt64,
) -> SpikingMultiHeadAttention {
  SpikingMultiHeadAttention::new(d_model, num_heads, beta, seed)
}

///|
/// Backward-compat: forward signature unchanged.
pub fn spiking_attention_forward(
  x : Array[Float],
  sa : SpikingMultiHeadAttention,
  mask : Array[Float],
) -> (Array[Float], SpikingMultiHeadAttnCache) {
  spiking_multi_head_attention_forward(x, sa, mask)
}

///|
/// Backward-compat: backward signature unchanged.
pub fn spiking_attention_backward(
  cache : SpikingMultiHeadAttnCache,
  d_output : Array[Float],
  sa : SpikingMultiHeadAttention,
) -> (Array[Float], MHAGrad) {
  spiking_multi_head_attention_backward(cache, d_output, sa)
}