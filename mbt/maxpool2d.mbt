// maxpool2d.mbt 鈥?2D max-pooling primitive (forward only).
//
// Layout: NCHW row-major flat `Array[Float]`, matching conv2d.mbt.
//
// Conventions:
//   - Input:  [n, c, h,  w]   length = n * c * h * w
//   - Output: [n, c, ho, wo]  length = n * c * ho * wo
//
//   ho = (h + 2*pad - kh) / stride + 1
//   wo = (w + 2*pad - kw) / stride + 1
//
// Out-of-bounds positions are treated as -infinity (so they never win
// the max). Padding is zero-padded input (negative infinity at padding
// positions).

///|
/// MaxPool2d parameter container.
pub struct MaxPool2dParam {
  kh : Int
  kw : Int
  stride : Int
  pad : Int
}

///|
/// Build a MaxPool2dParam. `stride` defaults to `kh` (non-overlapping).
pub fn MaxPool2dParam::new(
  kh : Int,
  kw : Int,
  stride? : Int,
  pad? : Int = 0,
) -> MaxPool2dParam {
  let s = if stride is Some(v) { v } else { kh }
  { kh, kw, stride: s, pad }
}

///|
/// Forward pass for a 2D max pool.
///
/// `input` : length = n * c * h * w
/// returns : length = n * c * ho * wo
pub fn maxpool2d_forward(
  input : Array[Float],
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
  let out : Array[Float] = Array::make(n * c * ho * wo, 0.0F)
  // -inf constant (Float32 lowest finite ~ -3.4e38)
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
          let mut kh_i = 0
          while kh_i < kh {
            let ih = ih_base + kh_i
            if ih >= 0 && ih < h {
              let mut kw_i = 0
              while kw_i < kw {
                let iw = iw_base + kw_i
                if iw >= 0 && iw < w {
                  let v = input[in_c_off + ih * w + iw]
                  if v > best {
                    best = v
                  }
                }
                kw_i = kw_i + 1
              }
            }
            kh_i = kh_i + 1
          }
          out[out_c_off + out_h * wo + out_w] = best
          out_w = out_w + 1
        }
        out_h = out_h + 1
      }
      ch = ch + 1
    }
  }
  out
}