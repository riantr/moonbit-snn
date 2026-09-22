// avgpool2d.mbt — Average pooling 2D (v0.20.1).
//
// Two pooling modes are supported:
//   - `avgpool2d_forward` / `avgpool2d_backward` — general average
//     pooling with arbitrary kernel/stride/pad.
//   - `global_avg_pool2d_forward` / `global_avg_pool2d_backward` —
//     pools the entire spatial dimension to [n, c, 1, 1] (ResNet's
//     `nn.AdaptiveAvgPool2d((1, 1))` equivalent).
//
// Conventions:
//   - NCHW row-major flat `Array[Float]`.
//   - For padded cells, the divisor is `kh * kw` (NOT adjusted for the
//     actual valid window). This matches PyTorch's
//     `count_include_pad=True` default.
//   - Backward distributes the upstream gradient uniformly across all
//     positions in the kernel window (each position gets `d / (kh*kw)`).

///|
/// AvgPool2d parameter container.
pub struct AvgPool2dParam {
  kh : Int
  kw : Int
  stride : Int
  pad : Int
}

///|
/// Build an AvgPool2dParam. `stride` defaults to `kh`, `pad` to 0.
pub fn AvgPool2dParam::new(
  kh : Int,
  kw : Int,
  stride? : Int = 0,
  pad? : Int = 0,
) -> AvgPool2dParam {
  let s = if stride == 0 { kh } else { stride }
  { kh, kw, stride: s, pad }
}

///|
/// Forward pass.
pub fn avgpool2d_forward(
  input : Array[Float],
  n : Int,
  c : Int,
  h : Int,
  w : Int,
  param : AvgPool2dParam,
) -> Array[Float] {
  let kh = param.kh
  let kw = param.kw
  let stride = param.stride
  let pad = param.pad
  let ho = (h + 2 * pad - kh) / stride + 1
  let wo = (w + 2 * pad - kw) / stride + 1
  let pool_size = Float::from_int(kh * kw)
  let out : Array[Float] = Array::make(n * c * ho * wo, 0.0F)
  for ni in 0..<n {
    for ci in 0..<c {
      for ohi in 0..<ho {
        for owi in 0..<wo {
          let mut sum = 0.0F
          let ih_base = ohi * stride - pad
          let iw_base = owi * stride - pad
          for khi in 0..<kh {
            let ih = ih_base + khi
            if ih >= 0 && ih < h {
              for kwi in 0..<kw {
                let iw = iw_base + kwi
                if iw >= 0 && iw < w {
                  let idx = ni * c * h * w + ci * h * w + ih * w + iw
                  sum = sum + input[idx]
                }
              }
            }
          }
          let out_idx = ni * c * ho * wo + ci * ho * wo + ohi * wo + owi
          out[out_idx] = sum / pool_size
        }
      }
    }
  }
  out
}

///|
/// Backward pass. Distributes the upstream gradient uniformly across
/// all positions in each kernel window.
pub fn avgpool2d_backward(
  d_output : Array[Float],
  n : Int,
  c : Int,
  h : Int,
  w : Int,
  param : AvgPool2dParam,
) -> Array[Float] {
  let kh = param.kh
  let kw = param.kw
  let stride = param.stride
  let pad = param.pad
  let ho = (h + 2 * pad - kh) / stride + 1
  let wo = (w + 2 * pad - kw) / stride + 1
  let pool_size = 1.0F / Float::from_int(kh * kw)
  let d_input : Array[Float] = Array::make(n * c * h * w, 0.0F)
  for ni in 0..<n {
    for ci in 0..<c {
      for ohi in 0..<ho {
        for owi in 0..<wo {
          let out_idx = ni * c * ho * wo + ci * ho * wo + ohi * wo + owi
          let grad = d_output[out_idx] * pool_size
          let ih_base = ohi * stride - pad
          let iw_base = owi * stride - pad
          for khi in 0..<kh {
            let ih = ih_base + khi
            if ih >= 0 && ih < h {
              for kwi in 0..<kw {
                let iw = iw_base + kwi
                if iw >= 0 && iw < w {
                  let idx = ni * c * h * w + ci * h * w + ih * w + iw
                  d_input[idx] = d_input[idx] + grad
                }
              }
            }
          }
        }
      }
    }
  }
  d_input
}

// ---------------------------------------------------------------------------
// Global Average Pooling (ResNet's terminal pooling to [n, c, 1, 1]).
// ---------------------------------------------------------------------------

///|
/// Global Average Pooling. Pools the entire spatial dimension to a
/// single value per (n, c). Result shape: `[n, c, 1, 1]`.
pub fn global_avg_pool2d_forward(
  input : Array[Float],
  n : Int,
  c : Int,
  h : Int,
  w : Int,
) -> Array[Float] {
  let pool_size = 1.0F / Float::from_int(h * w)
  let out : Array[Float] = Array::make(n * c, 0.0F)
  for ni in 0..<n {
    for ci in 0..<c {
      let mut sum = 0.0F
      for hi in 0..<h {
        for wi in 0..<w {
          let idx = ni * c * h * w + ci * h * w + hi * w + wi
          sum = sum + input[idx]
        }
      }
      out[ni * c + ci] = sum * pool_size
    }
  }
  out
}

///|
/// Backward for Global Average Pooling. Distributes the upstream
/// gradient uniformly across all (h, w) positions in each channel.
pub fn global_avg_pool2d_backward(
  d_output : Array[Float],
  n : Int,
  c : Int,
  h : Int,
  w : Int,
) -> Array[Float] {
  let pool_size = 1.0F / Float::from_int(h * w)
  let d_input : Array[Float] = Array::make(n * c * h * w, 0.0F)
  for ni in 0..<n {
    for ci in 0..<c {
      let grad = d_output[ni * c + ci] * pool_size
      for hi in 0..<h {
        for wi in 0..<w {
          let idx = ni * c * h * w + ci * h * w + hi * w + wi
          d_input[idx] = grad
        }
      }
    }
  }
  d_input
}