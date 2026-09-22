// dropout.mbt — Inverted dropout regularisation for transformer training.
//
// Inverted dropout (PyTorch / TensorFlow convention):
//
//   Training:  mask[i] ~ Bernoulli(1 - p)
//              out[i]  = (input[i] * mask[i]) / (1 - p)
//   Inference: out[i]  = input[i]                       (identity)
//
// Backward:  d_input[i] = d_output[i] * mask[i] / (1 - p)
// (same scale, so expected gradient magnitude matches forward
// magnitude).
//
// The dropout mask is stored in the cache so that backward uses the
// SAME mask as forward (otherwise the gradient wouldn't correspond
// to the actual sparse activations that were computed).

///|
/// Dropout parameter container.
pub(all) struct Dropout {
  p : Float      // dropout probability (0 = off, 1 = drop all)
  mut training : Bool
}

///|
/// Construct a Dropout with dropout probability `p`. Defaults to 0.5.
pub fn Dropout::new(p? : Float = 0.5F) -> Dropout {
  if p < 0.0F || p > 1.0F {
    abort("Dropout::new: p \{p} out of [0, 1]")
  }
  { p, training: true }
}

///|
/// Forward pass. Returns `(out, mask)` where `mask` is the per-element
/// Bernoulli mask (length n). Use `mask` in the corresponding
/// backward pass to recover the exact same scaling.
pub fn dropout_forward(
  input : Array[Float],
  d : Dropout,
  rng : Xoshiro,
) -> (Array[Float], Array[Float]) {
  let n = input.length()
  let out : Array[Float] = Array::make(n, 0.0F)
  let mask : Array[Float] = Array::make(n, 1.0F)
  if !d.training || d.p == 0.0F {
    // Inference mode / no dropout: identity, mask = all ones.
    for i in 0..<n {
      out[i] = input[i]
      mask[i] = 1.0F
    }
    return (out, mask)
  }
  if d.p == 1.0F {
    // Full dropout: zero everything.
    for i in 0..<n {
      out[i] = 0.0F
      mask[i] = 0.0F
    }
    return (out, mask)
  }
  let keep_p = 1.0F - d.p
  let inv_keep = 1.0F / keep_p
  for i in 0..<n {
    let r = next_f32(rng)
    if r < keep_p {
      mask[i] = 1.0F
      out[i] = input[i] * inv_keep
    } else {
      mask[i] = 0.0F
      out[i] = 0.0F
    }
  }
  (out, mask)
}

///|
/// Backward pass. `mask` must be the same mask returned by
/// `dropout_forward`. `d_output` is the upstream gradient.
pub fn dropout_backward(
  d_output : Array[Float],
  mask : Array[Float],
  d : Dropout,
) -> Array[Float] {
  let n = d_output.length()
  let d_input : Array[Float] = Array::make(n, 0.0F)
  if !d.training || d.p == 0.0F {
    // Inference / off: identity.
    for i in 0..<n {
      d_input[i] = d_output[i]
    }
    return d_input
  }
  if d.p == 1.0F {
    // Full dropout: zero gradient.
    return d_input
  }
  let inv_keep = 1.0F / (1.0F - d.p)
  for i in 0..<n {
    d_input[i] = d_output[i] * mask[i] * inv_keep
  }
  d_input
}

///|
/// Convenience: dropout_forward + cache wrapper for chain-style use.
pub struct DropoutCache {
  mask : Array[Float]
}