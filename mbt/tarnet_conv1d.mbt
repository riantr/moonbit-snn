// tarnet_conv1d.mbt -- the 1-D convolution primitives TARNet needs that this
// package did not have.
//
// Reference: Y. Terraf, Y. Iraqi, "TARNet: A Temporal-Aware Multi-Scale
// Architecture for Closed-Set Speaker Identification", arXiv:2605.07735
// (ICME 2026), and the authors' implementation
// github.com/YassinTERRAF/TARNet, `tarnet/models/tarnet.py`.
//
// Layout is `[n, c, t]` row-major flat: index `((b * c + ch) * t + ti)`.
//
// THREE THINGS ARE REUSED RATHER THAN REWRITTEN, and each reuse is worth
// naming because the shapes are unusual:
//
//   * A 1x1 Conv1d IS `linear_forward` with `n = t`. Every `kernel_size=1`
//     convolution in the paper -- the bottleneck, `res_conv`, the MFA, both
//     ASP convolutions, the SE convolutions, the final classifier -- is that
//     call with the time axis as the batch axis. So `conv1d_pointwise_*`
//     below are thin wrappers, not new maths.
//   * Global Layer Norm normalises over channels AND time jointly. This
//     package's `layer_norm_forward(input, n, LayerNorm(c, h, w), cache)`
//     normalises over `c*h*w` per sample, so `h = 1, w = t` gives exactly the
//     global statistic with no new code.
//   * BatchNorm1d over `[n, c, t]` is `batch_norm2d_forward(x, n, c, 1, t)`.
//
// What genuinely does not exist yet is the dilated depthwise convolution and
// PReLU, and those are what this file adds.

///|
/// `n` samples from N(0, std) via Box-Muller. Shared by every TARNet
/// submodule so the whole network draws from one stream, in construction
/// order -- which is what makes a seed reproduce a whole model rather than
/// one layer.
pub fn tn_randn(n : Int, std : Float, rng : Xoshiro) -> Array[Float] {
  let out : Array[Float] = Array::make(n, 0.0F)
  let mut i = 0
  while i < n {
    let (z1, z2) = box_muller(rng)
    let z = if z1 != 0.0 { z1 } else { z2 }
    out[i] = Float::from_double(z) * std
    i = i + 1
  }
  out
}

///|
/// Weight and bias of a depthwise 1-D convolution: one weight row per channel.
pub(all) struct Conv1DDepthwise {
  weight : Array[Float]
  bias : Array[Float]
  kernel_size : Int
  dilation : Int
  padding : Int
  channels : Int
}

///|
/// Build a depthwise convolution. `padding` is the PyTorch `Conv1d(padding=)`
/// value, i.e. how many zero frames sit on each side of the input; the
/// reference uses `(kernel_size - 1) * dilation // 2`, which for an odd
/// kernel is `dilation` and preserves the temporal length.
pub fn Conv1DDepthwise::new(
  channels : Int,
  kernel_size : Int,
  dilation : Int,
  padding : Int,
) -> Conv1DDepthwise {
  {
    weight: Array::make(channels * kernel_size, 0.0F),
    bias: Array::make(channels, 0.0F),
    kernel_size,
    dilation,
    padding,
    channels,
  }
}

///|
/// Depthwise dilated Conv1d forward. `x` is `[n, c, t]`, returns `[n, c, t]`.
///
/// `out[b, ch, ti] = bias[ch] + sum_j weight[ch, j] * x[b, ch, ti + j*dilation - padding]`
/// with `j` in `0..<kernel_size`. Two things are easy to get backwards here
/// and both compile:
///
///   * A DILATED convolution still holds only `kernel_size` weights per
///     channel. Dilation spaces the INPUT taps, not the kernel: indexing the
///     weight with `j * dilation` reads `channels * kernel_size * dilation`
///     entries out of a tensor that only has `channels * kernel_size`.
///   * The correlation is unflipped, matching PyTorch, so the shift is
///     `+j*dilation - padding` rather than `-j*dilation + padding`.
///
/// Reads out of bounds as zero -- PyTorch's implicit zero padding.
pub fn conv1d_depthwise_forward(
  x : Array[Float],
  n : Int,
  p : Conv1DDepthwise,
) -> Array[Float] {
  let c = p.channels
  let t = x.length() / (n * c)
  let k = p.kernel_size
  let d = p.dilation
  let pad = p.padding
  let out : Array[Float] = Array::make(n * c * t, 0.0F)
  for b in 0..<n {
    for ch in 0..<c {
      let w_off = ch * k
      let bi = p.bias[ch]
      let base = (b * c + ch) * t
      for ti in 0..<t {
        let mut acc = bi
        for j in 0..<k {
          let src = ti + j * d - pad
          if src >= 0 && src < t {
            acc = acc + p.weight[w_off + j] * x[base + src]
          }
        }
        out[base + ti] = acc
      }
    }
  }
  out
}

///|
/// Depthwise Conv1d backward. Returns `(d_input, d_weight, d_bias)`, all
/// fresh arrays.
///
/// The weight gradient and the input gradient both read the SAME shifted
/// window, so the shift is written once and used twice; the weight is indexed
/// by `j` and the input by `ti + j*dilation - padding`, matching the forward.
pub fn conv1d_depthwise_backward(
  x : Array[Float],
  d_out : Array[Float],
  n : Int,
  p : Conv1DDepthwise,
) -> (Array[Float], Array[Float], Array[Float]) {
  let c = p.channels
  let t = x.length() / (n * c)
  let k = p.kernel_size
  let d = p.dilation
  let pad = p.padding
  let d_x : Array[Float] = Array::make(n * c * t, 0.0F)
  let d_w : Array[Float] = Array::make(c * k, 0.0F)
  let d_b : Array[Float] = Array::make(c, 0.0F)
  for b in 0..<n {
    for ch in 0..<c {
      let w_off = ch * k
      let base = (b * c + ch) * t
      for ti in 0..<t {
        let g = d_out[base + ti]
        d_b[ch] = d_b[ch] + g
        for j in 0..<k {
          let src = ti + j * d - pad
          if src >= 0 && src < t {
            let xv = x[base + src]
            d_x[base + src] = d_x[base + src] + g * p.weight[w_off + j]
            d_w[w_off + j] = d_w[w_off + j] + g * xv
          }
        }
      }
    }
  }
  (d_x, d_w, d_b)
}

///|
/// PReLU with a single learnable slope, matching `nn.PReLU()`'s default
/// `num_parameters=1` -- one slope shared by every channel.
///
/// `out = x` where `x > 0`, else `slope * x`.
pub fn prelu_forward(x : Array[Float], slope : Float) -> Array[Float] {
  let n = x.length()
  let out : Array[Float] = Array::make(n, 0.0F)
  for i in 0..<n {
    let v = x[i]
    out[i] = if v > 0.0F { v } else { slope * v }
  }
  out
}

///|
/// PReLU backward. Returns `(d_input, d_slope)`.
///
/// The slope's gradient collects ONLY the negative entries. That is the whole
/// asymmetry of PReLU and it is easy to get wrong: summing `d_out * x` over
/// every element gives a slope that drifts even when the function is pure
/// ReLU on that batch.
pub fn prelu_backward(
  x : Array[Float],
  d_out : Array[Float],
  slope : Float,
) -> (Array[Float], Float) {
  let n = x.length()
  let d_x : Array[Float] = Array::make(n, 0.0F)
  let mut d_s = 0.0F
  for i in 0..<n {
    let v = x[i]
    if v > 0.0F {
      d_x[i] = d_out[i]
    } else {
      d_x[i] = d_out[i] * slope
      d_s = d_s + d_out[i] * v
    }
  }
  (d_x, d_s)
}

///|
/// 1x1 Conv1d forward: the time axis is the batch axis, so this is exactly
/// `linear_forward` with `n = t`. Named because "1x1 conv over time" reads
/// as a convolution and should not have to be re-derived at every call site.
pub fn conv1d_pointwise_forward(
  x : Array[Float],
  n : Int,
  t : Int,
  in_ch : Int,
  out_ch : Int,
  weight : Array[Float],
  bias : Array[Float],
) -> Array[Float] {
  linear_forward(x, t, LinearParam::new(weight, bias, in_ch, out_ch))
}