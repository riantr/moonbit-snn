// maxpool2d_backward.mbt 鈥?MaxPool2d backward pass.
//
// MaxPool forward:  out[n, c, ho, wo] = max over window of input
//                   (out-of-bounds positions treated as -inf).
// MaxPool backward: d_input[n, c, ih, iw] = d_output[n, c, ho, wo]
//                   if (ih, iw) was the argmax of window (ho, wo),
//                   else 0.
//
// Requires `argmax_idx` from `maxpool2d_forward_with_idx`. Layout of
// argmax_idx matches output: length n*c*ho*wo, value = flat input
// index (n, c, ih, iw) -> n*(c*h*w) + c*(h*w) + ih*w + iw, or -1 if
// the window was entirely OOB (impossible given -inf default).

///|
/// MaxPool2d forward + argmax recording.
///
/// `argmax_idx` is filled in with the flat input index of the argmax
/// for each output position. If the window is entirely OOB (impossible
/// when stride > 0 since at least one cell is in-bounds), the index is
/// set to -1.
///
/// returns (output, argmax_idx)
pub fn maxpool2d_forward_with_idx(
  input : Array[Float],
  n : Int,
  c : Int,
  h : Int,
  w : Int,
  param : MaxPool2dParam,
) -> (Array[Float], Array[Int]) {
  let kh = param.kh
  let kw = param.kw
  let stride = param.stride
  let pad = param.pad
  let ho = (h + 2 * pad - kh) / stride + 1
  let wo = (w + 2 * pad - kw) / stride + 1
  let out : Array[Float] = Array::make(n * c * ho * wo, 0.0F)
  let argmax_idx : Array[Int] = Array::make(n * c * ho * wo, -1)
  let neg_inf : Float = -3.4e38F
  let input_n_stride = c * h * w
  let input_c_stride = h * w
  let out_n_stride = c * ho * wo
  let out_c_stride = ho * wo
  for batch in 0..<n {
    let in_batch_off = batch * input_n_stride
    let out_batch_off = batch * out_n_stride
    let mut ch = 0
    while ch < c {
      let in_c_off = in_batch_off + ch * input_c_stride
      let out_c_off = out_batch_off + ch * out_c_stride
      let mut out_h = 0
      while out_h < ho {
        let ih_base = out_h * stride - pad
        let mut out_w = 0
        while out_w < wo {
          let iw_base = out_w * stride - pad
          let mut best = neg_inf
          let mut best_idx = -1
          let mut kh_i = 0
          while kh_i < kh {
            let ih = ih_base + kh_i
            if ih >= 0 && ih < h {
              let mut kw_i = 0
              while kw_i < kw {
                let iw = iw_base + kw_i
                if iw >= 0 && iw < w {
                  let flat_in_idx = in_c_off + ih * w + iw
                  let v = input[flat_in_idx]
                  if v > best {
                    best = v
                    best_idx = flat_in_idx
                  }
                }
                kw_i = kw_i + 1
              }
            }
            kh_i = kh_i + 1
          }
          out[out_c_off + out_h * wo + out_w] = best
          argmax_idx[out_c_off + out_h * wo + out_w] = best_idx
          out_w = out_w + 1
        }
        out_h = out_h + 1
      }
      ch = ch + 1
    }
  }
  (out, argmax_idx)
}

///|
/// MaxPool2d backward pass.
///
/// `d_output` is the upstream gradient of shape [n, c, ho, wo].
/// `argmax_idx` is the recorded argmax from
/// `maxpool2d_forward_with_idx`.
///
/// returns `d_input` of shape [n, c, h, w].
pub fn maxpool2d_backward(
  d_output : Array[Float],
  argmax_idx : Array[Int],
  n : Int,
  c : Int,
  h : Int,
  w : Int,
  param : MaxPool2dParam,
) -> Array[Float] {
  let kh = param.kh
  let kw = param.kw
  let stride = param.stride
  let pad = param.pad
  let ho = (h + 2 * pad - kh) / stride + 1
  let wo = (w + 2 * pad - kw) / stride + 1
  let d_input : Array[Float] = Array::make(n * c * h * w, 0.0F)
  let out_n_stride = c * ho * wo
  let out_c_stride = ho * wo
  for batch in 0..<n {
    let out_batch_off = batch * out_n_stride
    let mut ch = 0
    while ch < c {
      let out_c_off = out_batch_off + ch * out_c_stride
      let mut out_h = 0
      while out_h < ho {
        let mut out_w = 0
        while out_w < wo {
          let out_idx = out_c_off + out_h * wo + out_w
          let arg_in = argmax_idx[out_idx]
          if arg_in >= 0 {
            d_input[arg_in] = d_input[arg_in] + d_output[out_idx]
          }
          out_w = out_w + 1
        }
        out_h = out_h + 1
      }
      ch = ch + 1
    }
  }
  d_input
}