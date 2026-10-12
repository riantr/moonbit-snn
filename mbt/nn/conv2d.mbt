// conv2d.mbt 鈥?2D convolution primitive (forward only).
//
// Layout: NCHW row-major flat `Array[Float]`.
//
// Conventions:
//   - Input:    [n, c_in,  h,  w]    length = n * c_in  * h  * w
//   - Weight:   [c_out, c_in, kh, kw] length = c_out * c_in * kh * kw
//   - Bias:     [c_out]              length = c_out
//   - Output:   [n, c_out, ho, wo]   length = n * c_out * ho * wo
//
//   ho = (h + 2*pad - kh) / stride + 1
//   wo = (w + 2*pad - kw) / stride + 1
//
// No external dependencies; pure Float32 loop, matches the project's
// bit-exact Float32 contract. Forward only; no backward (autograd
// not in scope).

///|
/// Conv2d parameter container. `weight` is laid out as
/// `[c_out, c_in, kh, kw]` row-major; `bias` is `[c_out]`.
/// `stride` is shared across spatial dims (no asymmetric strides yet);
/// `pad` is the half-padding applied symmetrically on both sides
/// (e.g. `pad = kh // 2` gives "same" padding for odd kernel sizes).
pub struct Conv2dParam {
  weight : Array[Float]
  bias : Array[Float]
  c_out : Int
  c_in : Int
  kh : Int
  kw : Int
  stride : Int
  pad : Int
}

///|
/// Build a Conv2dParam from raw arrays. Does NOT copy the arrays; the
/// caller retains ownership.
pub fn Conv2dParam::new(
  weight : Array[Float],
  bias : Array[Float],
  c_out : Int,
  c_in : Int,
  kh : Int,
  kw : Int,
  stride? : Int = 1,
  pad? : Int = 0,
) -> Conv2dParam {
  { weight, bias, c_out, c_in, kh, kw, stride, pad }
}

///|
/// Forward pass for a 2D convolution.
///
/// `input`  : length = n * c_in  * h  * w
/// returns   : length = n * c_out * ho * wo
///
/// Pad-region values are zero (i.e. zero-padded). Out-of-bounds reads
/// contribute nothing.
pub fn conv2d_forward(
  input : Array[Float],
  n : Int,
  c_in : Int,
  h : Int,
  w : Int,
  param : Conv2dParam,
) -> Array[Float] {
  let c_out = param.c_out
  let kh = param.kh
  let kw = param.kw
  let stride = param.stride
  let pad = param.pad
  // Output spatial dims.
  let ho = (h + 2 * pad - kh) / stride + 1
  let wo = (w + 2 * pad - kw) / stride + 1
  let out : Array[Float] = Array::make(n * c_out * ho * wo, 0.0F)
  // Precompute row strides for input / weight / output indexing.
  let input_n_stride = c_in * h * w
  let input_c_stride = h * w
  let input_h_stride = w
  let weight_co_stride = c_in * kh * kw
  let weight_ci_stride = kh * kw
  let out_n_stride = c_out * ho * wo
  let out_co_stride = ho * wo
  for batch in 0..<n {
    let in_batch_off = batch * input_n_stride
    let out_batch_off = batch * out_n_stride
    for co in 0..<c_out {
      let w_co_off = co * weight_co_stride
      let out_co_off = out_batch_off + co * out_co_stride
      // Initialise the output channel slice with the bias.
      let bias_v = param.bias[co]
      let mut out_h = 0
      while out_h < ho {
        let mut out_w = 0
        while out_w < wo {
          out[out_co_off + out_h * wo + out_w] = bias_v
          out_w = out_w + 1
        }
        out_h = out_h + 1
      }
      // Accumulate over input channels and kernel positions.
      let mut ci = 0
      while ci < c_in {
        let w_ci_off = w_co_off + ci * weight_ci_stride
        let in_c_off = in_batch_off + ci * input_c_stride
        let mut out_h = 0
        while out_h < ho {
          let ih_base = out_h * stride - pad
          let mut kh_i = 0
          while kh_i < kh {
            let ih = ih_base + kh_i
            if ih >= 0 && ih < h {
              let mut out_w = 0
              while out_w < wo {
                let iw_base = out_w * stride - pad
                let mut kw_i = 0
                while kw_i < kw {
                  let iw = iw_base + kw_i
                  if iw >= 0 && iw < w {
                    let in_v = input[in_c_off + ih * input_h_stride + iw]
                    if in_v != 0.0F {
                      let w_v = param.weight[w_ci_off + kh_i * kw + kw_i]
                      if w_v != 0.0F {
                        let acc = out[out_co_off + out_h * wo + out_w]
                        out[out_co_off + out_h * wo + out_w] = acc +
                          w_v * in_v
                      }
                    }
                  }
                  kw_i = kw_i + 1
                }
                out_w = out_w + 1
              }
            }
            kh_i = kh_i + 1
          }
          out_h = out_h + 1
        }
        ci = ci + 1
      }
    }
  }
  out
}