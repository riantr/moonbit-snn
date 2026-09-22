// residual_block.mbt — Residual block (v0.20.2).
//
// Implements the basic residual block from He et al. 2015:
//
//   y = F(x, W) + W_s * x          // F = conv-bn-relu-conv-bn, W_s = projection (optional)
//   out = relu(y)
//
// `shortcut_conv` and `shortcut_bn` are `None` for identity shortcut
// (input shape must match the block output). For downsample blocks
// (stride > 1 or c changes), pass `Some(1x1 conv)` + `Some(BN)`.
//
// Cache holds everything backward needs:
//   x, sum (pre-ReLU), bn1_out (pre-ReLU of branch),
//   bn1_cache, bn2_cache, shortcut_bn_cache? (if any),
//   conv2_input (ReLU of bn1_out).
//
// Conventions:
//   - Stride is applied to the first conv (stride=2 in downsample
//     blocks halves the spatial size).
//   - The second conv uses stride=1.
//   - Both main-path convs use 3x3 kernels with pad=1 (preserves
//     spatial size at stride=1).
//   - BN follows each conv (pre-activation is NOT used; this is the
//     original ResNet v1 design).

///|
/// A basic residual block.
pub struct ResidualBlock {
  conv1 : Conv2dParam
  bn1 : BatchNorm2d
  conv2 : Conv2dParam
  bn2 : BatchNorm2d
  shortcut_conv : Conv2dParam?
  shortcut_bn : BatchNorm2d?
  stride : Int
}

///|
/// Cache returned by `residual_block_forward` and consumed by
/// `residual_block_backward`.
pub struct ResidualCache {
  // Saved input tensor x (for shortcut path).
  x : Array[Float]
  // Saved post-add sum: needed by ReLU backward.
  sum : Array[Float]
  // Saved post-BN1, pre-ReLU activation: needed by ReLU backward.
  bn1_out : Array[Float]
  // BN caches for the main path's two BatchNorm2d calls.
  bn1_cache : BatchNormCache
  bn2_cache : BatchNormCache
  // BN cache for the shortcut BN (if any).
  shortcut_bn_cache : BatchNormCache?
  // Saved shortcut output (BN output of shortcut conv, or identity x).
  shortcut_output : Array[Float]
  // Input / intermediate shapes (for conv2 backward).
  conv2_input : Array[Float]
  conv2_input_n : Int
  conv2_input_c : Int
  conv2_input_h : Int
  conv2_input_w : Int
  // Input shape.
  n : Int
  c : Int
  h : Int
  w : Int
}

///|
/// Parameter gradients returned by `residual_block_backward`.
pub struct ResidualGrads {
  conv1_d_weight : Array[Float]
  conv1_d_bias : Array[Float]
  bn1_d_gamma : Array[Float]
  bn1_d_beta : Array[Float]
  conv2_d_weight : Array[Float]
  conv2_d_bias : Array[Float]
  bn2_d_gamma : Array[Float]
  bn2_d_beta : Array[Float]
  shortcut_conv_d_weight : Array[Float]?
  shortcut_conv_d_bias : Array[Float]?
  shortcut_bn_d_gamma : Array[Float]?
  shortcut_bn_d_beta : Array[Float]?
}

///|
/// Build an identity-shortcut basic block (input shape must match
/// block output: c_in == c_out, stride == 1).
pub fn ResidualBlock::identity(c : Int, seed : UInt64) -> ResidualBlock {
  let rng = Xoshiro::new(seed)
  let w1 = he_init(rng, c * c * 3 * 3, c * 3 * 3)
  let b1 : Array[Float] = Array::make(c, 0.0F)
  let conv1 = Conv2dParam::new(w1, b1, c, c, 3, 3, stride=1, pad=1)
  let bn1 = BatchNorm2d::new(c)
  let w2 = he_init(rng, c * c * 3 * 3, c * 3 * 3)
  let b2 : Array[Float] = Array::make(c, 0.0F)
  let conv2 = Conv2dParam::new(w2, b2, c, c, 3, 3, stride=1, pad=1)
  let bn2 = BatchNorm2d::new(c)
  { conv1, bn1, conv2, bn2, shortcut_conv: None, shortcut_bn: None, stride: 1 }
}

///|
/// Build a downsample basic block with a 1x1 conv projection shortcut.
pub fn ResidualBlock::projection(
  c_in : Int,
  c_out : Int,
  stride : Int,
  seed : UInt64,
) -> ResidualBlock {
  let rng = Xoshiro::new(seed)
  let w1 = he_init(rng, c_out * c_in * 3 * 3, c_in * 3 * 3)
  let b1 : Array[Float] = Array::make(c_out, 0.0F)
  let conv1 = Conv2dParam::new(w1, b1, c_out, c_in, 3, 3, stride=stride, pad=1)
  let bn1 = BatchNorm2d::new(c_out)
  let w2 = he_init(rng, c_out * c_out * 3 * 3, c_out * 3 * 3)
  let b2 : Array[Float] = Array::make(c_out, 0.0F)
  let conv2 = Conv2dParam::new(w2, b2, c_out, c_out, 3, 3, stride=1, pad=1)
  let bn2 = BatchNorm2d::new(c_out)
  let ws = he_init(rng, c_out * c_in, c_in)
  let bs : Array[Float] = Array::make(c_out, 0.0F)
  let shortcut_conv = Conv2dParam::new(ws, bs, c_out, c_in, 1, 1, stride=stride, pad=0)
  let shortcut_bn = BatchNorm2d::new(c_out)
  {
    conv1, bn1, conv2, bn2,
    shortcut_conv: Some(shortcut_conv), shortcut_bn: Some(shortcut_bn),
    stride,
  }
}

///|
/// Forward pass for a residual block.
pub fn residual_block_forward(
  b : ResidualBlock,
  x : Array[Float],
  n : Int,
  c : Int,
  h : Int,
  w : Int,
) -> (Array[Float], ResidualCache) {
  // ---- Main path ----
  let conv1_out = conv2d_forward(x, n, c, h, w, b.conv1)
  let h1 = (h + 2 * b.conv1.pad - b.conv1.kh) / b.conv1.stride + 1
  let w1 = (w + 2 * b.conv1.pad - b.conv1.kw) / b.conv1.stride + 1
  let c1 = b.conv1.c_out
  let (bn1_out, bn1_cache) = batch_norm2d_forward(conv1_out, n, c1, h1, w1, b.bn1)
  let relu1 = relu_forward(bn1_out)
  let conv2_out = conv2d_forward(relu1, n, c1, h1, w1, b.conv2)
  let h2 = (h1 + 2 * b.conv2.pad - b.conv2.kh) / b.conv2.stride + 1
  let w2 = (w1 + 2 * b.conv2.pad - b.conv2.kw) / b.conv2.stride + 1
  let c2 = b.conv2.c_out
  let (bn2_out, bn2_cache) = batch_norm2d_forward(conv2_out, n, c2, h2, w2, b.bn2)
  // ---- Shortcut path ----
  let (shortcut_out, sbn_cache_opt) = match (b.shortcut_conv, b.shortcut_bn) {
    (Some(scv), Some(sbn)) => {
      let sout_pre = conv2d_forward(x, n, c, h, w, scv)
      let sh_pre = (h + 2 * scv.pad - scv.kh) / scv.stride + 1
      let sw_pre = (w + 2 * scv.pad - scv.kw) / scv.stride + 1
      let (sout, sbn_cache) = batch_norm2d_forward(
        sout_pre, n, scv.c_out, sh_pre, sw_pre, sbn,
      )
      (sout, Some(sbn_cache))
    }
    _ => (x, None)
  }
  // ---- Sum + ReLU ----
  let sum = add_forward(bn2_out, shortcut_out)
  let out = relu_forward(sum)
  let cache : ResidualCache = {
    x,
    sum,
    bn1_out,
    bn1_cache,
    bn2_cache,
    shortcut_bn_cache: sbn_cache_opt,
    shortcut_output: shortcut_out,
    conv2_input: relu1,
    conv2_input_n: n,
    conv2_input_c: c1,
    conv2_input_h: h1,
    conv2_input_w: w1,
    n, c, h, w,
  }
  (out, cache)
}

///|
/// Backward pass for a residual block. Returns (d_input, grads).
pub fn residual_block_backward(
  b : ResidualBlock,
  cache : ResidualCache,
  d_output : Array[Float],
) -> (Array[Float], ResidualGrads) {
  let n = cache.n
  let c = cache.c
  let h = cache.h
  let w = cache.w
  // ReLU backward at the block output.
  let d_sum = relu_backward(cache.sum, d_output)
  let (d_bn2, d_shortcut) = add_backward(d_sum)
  let (d_conv2_in, d_bn2_gamma, d_bn2_beta) = batch_norm2d_backward(
    d_bn2, cache.bn2_cache, b.bn2,
  )
  let (d_relu1, d_conv2_w, d_conv2_b) = conv2d_backward(
    cache.conv2_input, d_conv2_in, cache.conv2_input_n,
    cache.conv2_input_c, cache.conv2_input_h, cache.conv2_input_w,
    b.conv2,
  )
  let d_bn1 = relu_backward(cache.bn1_out, d_relu1)
  let (d_conv1_in, d_bn1_gamma, d_bn1_beta) = batch_norm2d_backward(
    d_bn1, cache.bn1_cache, b.bn1,
  )
  let (d_x_main, d_conv1_w, d_conv1_b) = conv2d_backward(
    cache.x, d_conv1_in, n, c, h, w, b.conv1,
  )
  match (b.shortcut_conv, b.shortcut_bn, cache.shortcut_bn_cache) {
    (Some(scv), Some(sbn), Some(sbn_cache)) => {
      // BN backward first (operating on d_shortcut of shape
      // [n, scv.c_out, h_out, w_out]) -> returns d_shortcut_bn_in
      // of the same shape.
      let (d_shortcut_bn_in, d_sbn_g, d_sbn_b) = batch_norm2d_backward(
        d_shortcut, sbn_cache, sbn,
      )
      // Then conv backward (operating on d_shortcut_bn_in of shape
      // [n, scv.c_out, h_out, w_out]) -> returns d_x_shortcut of
      // shape [n, c, h, w] matching d_x_main.
      let (d_x_shortcut, d_sc_w, d_sc_b) = conv2d_backward(
        cache.x, d_shortcut_bn_in, n, c, h, w, scv,
      )
      let merged : Array[Float] = Array::make(d_x_main.length(), 0.0F)
      for i in 0..<d_x_main.length() {
        merged[i] = d_x_main[i] + d_x_shortcut[i]
      }
      let grads : ResidualGrads = {
        conv1_d_weight: d_conv1_w, conv1_d_bias: d_conv1_b,
        bn1_d_gamma: d_bn1_gamma, bn1_d_beta: d_bn1_beta,
        conv2_d_weight: d_conv2_w, conv2_d_bias: d_conv2_b,
        bn2_d_gamma: d_bn2_gamma, bn2_d_beta: d_bn2_beta,
        shortcut_conv_d_weight: Some(d_sc_w), shortcut_conv_d_bias: Some(d_sc_b),
        shortcut_bn_d_gamma: Some(d_sbn_g), shortcut_bn_d_beta: Some(d_sbn_b),
      }
      (merged, grads)
    }
    _ => {
      let merged : Array[Float] = Array::make(d_x_main.length(), 0.0F)
      for i in 0..<d_x_main.length() {
        merged[i] = d_x_main[i] + d_shortcut[i]
      }
      let grads : ResidualGrads = {
        conv1_d_weight: d_conv1_w, conv1_d_bias: d_conv1_b,
        bn1_d_gamma: d_bn1_gamma, bn1_d_beta: d_bn1_beta,
        conv2_d_weight: d_conv2_w, conv2_d_bias: d_conv2_b,
        bn2_d_gamma: d_bn2_gamma, bn2_d_beta: d_bn2_beta,
        shortcut_conv_d_weight: None, shortcut_conv_d_bias: None,
        shortcut_bn_d_gamma: None, shortcut_bn_d_beta: None,
      }
      (merged, grads)
    }
  }
}