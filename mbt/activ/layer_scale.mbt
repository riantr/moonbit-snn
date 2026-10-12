// layer_scale.mbt — LayerScale (Touvron 2021).
//
// LayerScale is a learnable per-channel scale on the residual
// branch in deep transformers. The forward is:
//
//   y = x + gamma * sublayer(x)
//
// where `gamma` is a learnable vector of length `dim` (typically
// d_model or d_ff) initialised to a small value (1e-4 or 1e-6).
// This stabilises training of very deep transformers by allowing
// the residual contribution to start near zero and grow as needed.
//
// Backward:
//   d_sublayer = gamma * d_output
//   d_x        = d_output
//   d_gamma[i] = sum_{batch, spatial} d_output[*, i] * sublayer[*, i]
//
// Float32 throughout. `gamma` is mutable so the optimiser can
// update it in place.

///|
/// LayerScale parameter container.
pub struct LayerScale {
  dim : Int
  gamma : Array[Float]
}

///|
/// Construct a LayerScale with `gamma` initialised to `init_value`
/// (typically 1e-4 or smaller). Default: 1e-4.
pub fn LayerScale::new(
  dim : Int,
  init_value? : Float = 0.0001F,
) -> LayerScale {
  { dim, gamma: Array::make(dim, init_value) }
}

///|
/// Forward pass: out[i] = gamma[i] * sublayer[i] (per-channel).
pub fn layer_scale_forward(
  sublayer : Array[Float],
  ls : LayerScale,
) -> Array[Float] {
  let n = sublayer.length()
  if n % ls.dim != 0 {
    abort("layer_scale_forward: length \{n} not divisible by dim \{ls.dim}")
  }
  let n_batch = n / ls.dim
  let out : Array[Float] = Array::make(n, 0.0F)
  for b in 0..<n_batch {
    for i in 0..<ls.dim {
      out[b * ls.dim + i] = ls.gamma[i] * sublayer[b * ls.dim + i]
    }
  }
  out
}

///|
/// Backward pass. Returns `(d_sublayer, d_gamma)`.
/// `d_sublayer` is `gamma * d_output` (element-wise).
/// `d_gamma[i]` = sum over batch of `d_output[*, i] * sublayer[*, i]`.
pub fn layer_scale_backward(
  sublayer : Array[Float],
  d_output : Array[Float],
  ls : LayerScale,
) -> (Array[Float], Array[Float]) {
  let n = sublayer.length()
  let d_sublayer : Array[Float] = Array::make(n, 0.0F)
  let d_gamma : Array[Float] = Array::make(ls.dim, 0.0F)
  let n_batch = n / ls.dim
  for b in 0..<n_batch {
    for i in 0..<ls.dim {
      let d_o = d_output[b * ls.dim + i]
      d_sublayer[b * ls.dim + i] = ls.gamma[i] * d_o
      d_gamma[i] = d_gamma[i] + d_o * sublayer[b * ls.dim + i]
    }
  }
  (d_sublayer, d_gamma)
}