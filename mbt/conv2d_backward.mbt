// conv2d_backward.mbt 鈥?Conv2d backward pass.
//
// Conv2d forward:
//   out[n, co, ho, wo] = bias[co] + sum_{ci, kh, kw} W[co, ci, kh, kw]
//                        * input[n, ci, ho*stride-pad+kh, wo*stride-pad+kw]
//   (out-of-bounds input values are 0.)
//
// Conv2d backward (three gradients):
//   d_input[n, ci, ih, iw] = sum_{co, kh, kw} W[co, ci, kh, kw]
//                            * d_output[n, co, ih-kh+pad, ...] / stride
//   d_weight[co, ci, kh, kw] = sum_{n, ho, wo} d_output[n, co, ho, wo]
//                              * input[n, ci, ho*stride-pad+kh, wo*stride-pad+kw]
//   d_bias[co] = sum_{n, ho, wo} d_output[n, co, ho, wo]

///|
/// Conv2d backward pass. Returns `(d_input, d_weight, d_bias)`.
pub fn conv2d_backward(
  input : Array[Float],
  d_output : Array[Float],
  n : Int,
  c_in : Int,
  h : Int,
  w : Int,
  param : Conv2dParam,
) -> (Array[Float], Array[Float], Array[Float]) {
  let c_out = param.c_out
  let kh = param.kh
  let kw = param.kw
  let stride = param.stride
  let pad = param.pad
  let ho = (h + 2 * pad - kh) / stride + 1
  let wo = (w + 2 * pad - kw) / stride + 1
  let d_input : Array[Float] = Array::make(n * c_in * h * w, 0.0F)
  let d_weight : Array[Float] = Array::make(c_out * c_in * kh * kw, 0.0F)
  let d_bias : Array[Float] = Array::make(c_out, 0.0F)
  let input_n_stride = c_in * h * w
  let input_c_stride = h * w
  let input_h_stride = w
  let weight_co_stride = c_in * kh * kw
  let weight_ci_stride = kh * kw
  let d_out_n_stride = c_out * ho * wo
  let d_out_co_stride = ho * wo
  // ---- d_bias ----
  for batch in 0..<n {
    let d_out_batch_off = batch * d_out_n_stride
    for co in 0..<c_out {
      let d_out_co_off = d_out_batch_off + co * d_out_co_stride
      let mut acc : Float = 0.0F
      let mut out_h = 0
      while out_h < ho {
        let mut out_w = 0
        while out_w < wo {
          acc = acc + d_output[d_out_co_off + out_h * wo + out_w]
          out_w = out_w + 1
        }
        out_h = out_h + 1
      }
      d_bias[co] = d_bias[co] + acc
    }
  }
  // ---- d_weight ----
  // d_weight[co, ci, kh, kw] += d_output[n, co, ho, wo] * input[n, ci, ih, iw]
  // where ih = ho*stride - pad + kh, iw = wo*stride - pad + kw.
  for batch in 0..<n {
    let in_batch_off = batch * input_n_stride
    let d_out_batch_off = batch * d_out_n_stride
    for co in 0..<c_out {
      let w_co_off = co * weight_co_stride
      let d_out_co_off = d_out_batch_off + co * d_out_co_stride
      let mut ci = 0
      while ci < c_in {
        let w_ci_off = w_co_off + ci * weight_ci_stride
        let in_c_off = in_batch_off + ci * input_c_stride
        let mut kh_i = 0
        while kh_i < kh {
          let mut kw_i = 0
          while kw_i < kw {
            let mut acc : Float = 0.0F
            let mut out_h = 0
            while out_h < ho {
              let ih = out_h * stride - pad + kh_i
              if ih >= 0 && ih < h {
                let mut out_w = 0
                while out_w < wo {
                  let iw = out_w * stride - pad + kw_i
                  if iw >= 0 && iw < w {
                    acc = acc +
                      d_output[d_out_co_off + out_h * wo + out_w] *
                      input[in_c_off + ih * input_h_stride + iw]
                  }
                  out_w = out_w + 1
                }
              }
              out_h = out_h + 1
            }
            d_weight[w_ci_off + kh_i * kw + kw_i] = d_weight[w_ci_off + kh_i * kw + kw_i] + acc
            kw_i = kw_i + 1
          }
          kh_i = kh_i + 1
        }
        ci = ci + 1
      }
    }
  }
  // ---- d_input ----
  // d_input[n, ci, ih, iw] = sum_{co, kh, kw}
  //   W[co, ci, kh, kw] * d_output[n, co, ih+pad-kh)/stride, ...]
  for batch in 0..<n {
    let in_batch_off = batch * input_n_stride
    let d_out_batch_off = batch * d_out_n_stride
    let mut ci = 0
    while ci < c_in {
      let in_c_off = in_batch_off + ci * input_c_stride
      let mut ih = 0
      while ih < h {
        let mut iw = 0
        while iw < w {
          let mut acc : Float = 0.0F
          let mut co = 0
          while co < c_out {
            let w_co_off = co * weight_co_stride
            let w_ci_off = w_co_off + ci * weight_ci_stride
            let d_out_co_off = d_out_batch_off + co * d_out_co_stride
            let mut kh_i = 0
            while kh_i < kh {
              let ho_idx = ih + pad - kh_i
              if ho_idx >= 0 && ho_idx % stride == 0 {
                let ho_k = ho_idx / stride
                if ho_k < ho {
                  let mut kw_i = 0
                  while kw_i < kw {
                    let wo_idx = iw + pad - kw_i
                    if wo_idx >= 0 && wo_idx % stride == 0 {
                      let wo_k = wo_idx / stride
                      if wo_k < wo {
                        acc = acc +
                          param.weight[w_ci_off + kh_i * kw + kw_i] *
                          d_output[d_out_co_off + ho_k * wo + wo_k]
                      }
                    }
                    kw_i = kw_i + 1
                  }
                }
              }
              kh_i = kh_i + 1
            }
            co = co + 1
          }
          d_input[in_c_off + ih * input_h_stride + iw] = acc
          iw = iw + 1
        }
        ih = ih + 1
      }
      ci = ci + 1
    }
  }
  (d_input, d_weight, d_bias)
}