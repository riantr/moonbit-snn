// chain.mbt — Generic layer Chain / Sequential (v0.19.0).
//
// Tagged-enum dispatch over the project's forward / backward layers.
// The user composes a sequence of layers, calls chain_forward to run
// them in order, and chain_backward to reverse the order. Caches per
// layer are returned alongside the output, and per-layer parameter
// gradients are returned alongside d_input.
//
// Conventions:
//   - Layer enum carries each layer's parameter struct (or unit for
//     stateless layers like ReLU / Flatten / MaxPool2d).
//   - LayerCache enum carries whatever the backward pass needs (input
//     activations, BN stats, maxpool argmax indices, ...).
//   - LayerGrad enum carries per-layer parameter gradients (Empty for
//     layers with no learnable params).
//   - Shape `(n, c, h, w)` is tracked explicitly. After Flatten or
//     Linear, the chain treats the data as `(n, k, 1, 1)` where `k` is
//     the number of features; subsequent Conv2d / MaxPool2d layers
//     will fail (semantically wrong topology).
//
// Not in scope: residual / skip / branching connections. Those are
// v0.20.x material (Function API).

///|
/// Layer enum: each variant holds the parameter struct (or is unit
/// for stateless layers).
pub enum Layer {
  Conv2d(Conv2dParam)
  ReLU
  MaxPool2d(MaxPool2dParam)
  Flatten
  Linear(LinearParam)
  BatchNorm2d(BatchNorm2d)
  LayerNorm(LayerNorm)
}

// ---------------------------------------------------------------------------
// Public constructor helpers (MoonBit's read-only enum variants cannot
// be constructed from another file; callers must use these wrappers).
// ---------------------------------------------------------------------------

///|
pub fn Layer::conv2d(param : Conv2dParam) -> Layer { Layer::Conv2d(param) }

///|
pub fn Layer::relu() -> Layer { Layer::ReLU }

///|
pub fn Layer::max_pool2d(param : MaxPool2dParam) -> Layer { Layer::MaxPool2d(param) }

///|
pub fn Layer::flatten() -> Layer { Layer::Flatten }

///|
pub fn Layer::linear(param : LinearParam) -> Layer { Layer::Linear(param) }

///|
pub fn Layer::batch_norm2d(bn : BatchNorm2d) -> Layer { Layer::BatchNorm2d(bn) }

///|
pub fn Layer::layer_norm(ln : LayerNorm) -> Layer { Layer::LayerNorm(ln) }

///|
/// Per-layer forward cache. Each variant holds the data the backward
/// pass needs (input activations, BN stats, max-pool argmax indices,
/// etc.).
pub enum LayerCache {
  // input + shape (for conv2d_backward)
  Conv2d(Array[Float], Int, Int, Int, Int)
  // input (for relu_backward)
  ReLU(Array[Float])
  // argmax_idx + shape + kernel/stride/pad (for maxpool2d_backward)
  MaxPool2d(Array[Int], Int, Int, Int, Int, Int, Int, Int, Int)
  // no cache
  Flatten
  // input + n (for linear_backward)
  Linear(Array[Float], Int)
  // full BN cache (mean, inv_std, x_centered, ...)
  BatchNorm2d(BatchNormCache)
  // full LN cache
  LayerNorm(LayerNormCache)
}

///|
/// Per-layer parameter gradients. `Empty` for layers without
/// learnable params (ReLU, Flatten, MaxPool2d).
pub enum LayerGrad {
  Empty
  Conv2d(Array[Float], Array[Float]) // d_weight, d_bias
  Linear(Array[Float], Array[Float])
  BatchNorm2d(Array[Float], Array[Float]) // d_gamma, d_beta
  LayerNorm(Array[Float], Array[Float])
  MaxPool2d
}

///|
/// Forward pass over the chain. Returns the final output, one cache
/// per layer (in forward order), and the final `(n, c, h, w)` shape.
///
/// Note: after Flatten or Linear the chain internally treats the data
/// as `(n, k, 1, 1)`. Subsequent Conv2d / MaxPool2d / BN / LN layers
/// will fail at runtime — this is a semantic topology error.
pub fn chain_forward(
  layers : Array[Layer],
  input : Array[Float],
  n : Int,
  c : Int,
  h : Int,
  w : Int,
) -> (Array[Float], Array[LayerCache], Int, Int, Int, Int) {
  let caches : Array[LayerCache] = []
  let mut data = input
  let cur_n = n
  let mut cur_c = c
  let mut cur_h = h
  let mut cur_w = w
  for i in 0..<layers.length() {
    let layer = layers[i]
    match layer {
      Conv2d(param) => {
        caches.push(LayerCache::Conv2d(data, cur_n, cur_c, cur_h, cur_w))
        let out = conv2d_forward(data, cur_n, cur_c, cur_h, cur_w, param)
        data = out
        // Compute new spatial dims: ho = (h + 2*pad - kh) / stride + 1
        let ho = (cur_h + 2 * param.pad - param.kh) / param.stride + 1
        let wo = (cur_w + 2 * param.pad - param.kw) / param.stride + 1
        cur_c = param.c_out
        cur_h = ho
        cur_w = wo
      }
      ReLU => {
        caches.push(LayerCache::ReLU(data))
        data = relu_forward(data)
        // shape unchanged
      }
      MaxPool2d(param) => {
        let (out, argmax) = maxpool2d_forward_with_idx(
          data, cur_n, cur_c, cur_h, cur_w, param,
        )
        caches.push(
          LayerCache::MaxPool2d(
            argmax, cur_n, cur_c, cur_h, cur_w, param.kh, param.kw,
            param.stride, param.pad,
          ),
        )
        data = out
        let ho = (cur_h + 2 * param.pad - param.kh) / param.stride + 1
        let wo = (cur_w + 2 * param.pad - param.kw) / param.stride + 1
        cur_h = ho
        cur_w = wo
      }
      Flatten => {
        caches.push(LayerCache::Flatten)
        // Treat the current (c, h, w) plane as a flat feature vector
        // per sample: feature dim = c * h * w.
        let k = cur_c * cur_h * cur_w
        cur_c = k
        cur_h = 1
        cur_w = 1
        // data is already in the right memory order (NCHW with c*h*w
        // contiguous per sample) so we don't need to copy.
      }
      Linear(param) => {
        caches.push(LayerCache::Linear(data, cur_n))
        let out = linear_forward(data, cur_n, param)
        data = out
        cur_c = param.out_features
        cur_h = 1
        cur_w = 1
      }
      BatchNorm2d(bn) => {
        let (out, cache) = batch_norm2d_forward(data, cur_n, cur_c, cur_h, cur_w, bn)
        caches.push(LayerCache::BatchNorm2d(cache))
        data = out
      }
      LayerNorm(ln) => {
        let (out, cache) = layer_norm_forward(data, cur_n, cur_c, cur_h, cur_w, ln)
        caches.push(LayerCache::LayerNorm(cache))
        data = out
      }
    }
  }
  (data, caches, cur_n, cur_c, cur_h, cur_w)
}

///|
/// Backward pass. Iterates the chain in reverse, threading `d_output`
/// through each layer's backward and emitting per-layer parameter
/// gradients. Returns `(d_input, grads)` where `grads[i]` corresponds
/// to `layers[i]`.
pub fn chain_backward(
  layers : Array[Layer],
  caches : Array[LayerCache],
  d_output : Array[Float],
  n : Int,
  c : Int,
  h : Int,
  w : Int,
) -> (Array[Float], Array[LayerGrad]) {
  let grads : Array[LayerGrad] = Array::make(layers.length(), LayerGrad::Empty)
  let mut d = d_output
  let mut cur_n = n
  let mut cur_c = c
  let mut cur_h = h
  let mut cur_w = w
  // Walk the chain in reverse.
  for i in 0..<layers.length() {
    let idx = layers.length() - 1 - i
    let layer = layers[idx]
    let cache = caches[idx]
    match (layer, cache) {
      // ---- Layers without parameters ----
      (ReLU, LayerCache::ReLU(input)) => {
        d = relu_backward(input, d)
        // shape unchanged
      }
      (Flatten, LayerCache::Flatten) => {
        // d has shape (cur_n, cur_c, 1, 1) where cur_c = c*h*w of the
        // previous (pre-Flatten) tensor. We need to interpret it as
        // (cur_n, prev_c, prev_h, prev_w). The data is already in the
        // right memory order (NCHW row-major with feature dim
        // contiguous), so the only change is logical shape.
        // We can't restore prev_c/h/w here without tracking it, so
        // we leave the data unchanged and pass it forward. The next
        // (earlier) layer's backward receives this flat-shape array
        // but treats it using the shape recorded in its own cache.
        let _ = cur_n
      }
      (MaxPool2d(param), LayerCache::MaxPool2d(argmax, n0, c0, h0, w0, kh, kw, st, pad)) => {
        // The d here is the upstream d, which has shape (n, c, h_out, w_out).
        let d_in = maxpool2d_backward(d, argmax, n0, c0, h0, w0, param)
        // d_in has shape (n, c, h0, w0). Update shape.
        d = d_in
        cur_n = n0
        cur_c = c0
        cur_h = h0
        cur_w = w0
        let _ = kh
        let _ = kw
        let _ = st
        let _ = pad
        grads[idx] = LayerGrad::MaxPool2d
      }
      // ---- Layers with parameters ----
      (Conv2d(param), LayerCache::Conv2d(input, n0, c0, h0, w0)) => {
        let (d_in, d_w, d_b) = conv2d_backward(input, d, n0, c0, h0, w0, param)
        d = d_in
        cur_n = n0
        cur_c = c0
        cur_h = h0
        cur_w = w0
        grads[idx] = LayerGrad::Conv2d(d_w, d_b)
      }
      (Linear(param), LayerCache::Linear(input, n0)) => {
        let (d_in, d_w, d_b) = linear_backward(input, d, n0, param)
        d = d_in
        // d_in has shape (n0, in_features). We don't know in_features
        // here directly — it's the previous layer's out_features or
        // the post-Flatten feature dim. Just mark cur_h = cur_w = 1.
        cur_n = n0
        cur_h = 1
        cur_w = 1
        grads[idx] = LayerGrad::Linear(d_w, d_b)
      }
      (BatchNorm2d(bn), LayerCache::BatchNorm2d(bn_cache)) => {
        let (d_in, d_g, d_b) = batch_norm2d_backward(d, bn_cache, bn)
        d = d_in
        grads[idx] = LayerGrad::BatchNorm2d(d_g, d_b)
      }
      (LayerNorm(ln), LayerCache::LayerNorm(ln_cache)) => {
        let (d_in, d_g, d_b) = layer_norm_backward(d, ln_cache, ln)
        d = d_in
        grads[idx] = LayerGrad::LayerNorm(d_g, d_b)
      }
      // Mismatched cache / layer combos — propagate d unchanged and
      // leave the grad as Empty. (This branch is unreachable when the
      // user passes a well-formed chain.)
      _ => ()
    }
  }
  (d, grads)
}