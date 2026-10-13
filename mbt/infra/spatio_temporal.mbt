// spatio_temporal.mbt — 5D [T, B, C, H, W] tensor for spiking-CNN
// time-series input (v0.14.1).
//
// Layout convention (matching SNN.jl / Conv-STDP / most time-major
// spiking CNN literature):
//   - `t` (outermost): time step index, 0..<T
//   - `b` (next):      batch index, 0..<B
//   - `c`:             channel, 0..<C
//   - `h`, `w`:        spatial y / x within each [C, H, W] plane
//
// `data` is a flat `Array[Float]` of length `T * B * C * H * W`,
// row-major. Flat offset for `[t, b, c, h, w]` is:
//   `t * (B*C*H*W) + b * (C*H*W) + c * (H*W) + h * W + w`.
//
// Use `get_frame(t)` to extract a single time step as a 4D
// `Image { n: 1, c, h, w }` (ready to feed into v0.11.x Conv2d /
// MaxPool2d forward / v0.12.x backward). Use `get_batch(b)` to
// flatten the time axis into the batch axis (giving a 4D image
// with `n: T`, useful for vSTDP convolution across time).

///|
/// 5D tensor [T, B, C, H, W] in flat row-major Float32 layout.
pub struct STImage {
  data : Array[Float]
  t : Int
  b : Int
  c : Int
  h : Int
  w : Int
}

///|
/// Empty [T, B, C, H, W] tensor filled with zeros.
pub fn STImage::zeros(
  t : Int,
  b : Int,
  c : Int,
  h : Int,
  w : Int,
) -> STImage {
  { data: Array::make(t * b * c * h * w, 0.0F), t, b, c, h, w }
}

///|
/// Total element count (T * B * C * H * W).
pub fn STImage::numel(self : STImage) -> Int {
  self.data.length()
}

///|
/// Shape as `(t, b, c, h, w)`.
pub fn STImage::shape(self : STImage) -> (Int, Int, Int, Int, Int) {
  (self.t, self.b, self.c, self.h, self.w)
}

///|
/// Stride helper: number of elements per [B, C, H, W] "frame block".
pub fn STImage::frame_block(self : STImage) -> Int {
  self.b * self.c * self.h * self.w
}

///|
/// Stride helper: number of elements per [C, H, W] "batch slice".
pub fn STImage::batch_slice(self : STImage) -> Int {
  self.c * self.h * self.w
}

///|
/// Flat offset of `[t, b, c, h, w]` in `data`.
pub fn STImage::offset(
  self : STImage,
  t : Int,
  b : Int,
  c : Int,
  h : Int,
  w : Int,
) -> Int {
  t * self.frame_block() + b * self.batch_slice() + c * (self.h * self.w) +
  h * self.w +
  w
}

///|
/// Read `[t, b, c, h, w]`.
pub fn STImage::get(
  self : STImage,
  t : Int,
  b : Int,
  c : Int,
  h : Int,
  w : Int,
) -> Float {
  self.data[self.offset(t, b, c, h, w)]
}

///|
/// Write `[t, b, c, h, w]`.
pub fn STImage::set(
  self : STImage,
  t : Int,
  b : Int,
  c : Int,
  h : Int,
  w : Int,
  v : Float,
) -> Unit {
  self.data[self.offset(t, b, c, h, w)] = v
}

///|
/// Extract a single time step `t` as a 4D `Image { n: 1, c, h, w }`.
/// Returns a fresh `Image` (data is copied, not aliased).
pub fn STImage::get_frame(self : STImage, t : Int) -> Image {
  let block = self.frame_block()
  let plane = self.c * self.h * self.w
  let out : Array[Float] = Array::make(plane, 0.0F)
  let src_off = t * block
  for i in 0..<plane {
    out[i] = self.data[src_off + i]
  }
  { data: out, n: 1, c: self.c, h: self.h, w: self.w }
}

///|
/// Extract a single batch `b` as a 4D `Image { n: T, c, h, w }`.
/// Useful when the time axis should be flattened into the batch axis
/// (e.g. vSTDP conv across time).
pub fn STImage::get_batch(self : STImage, b : Int) -> Image {
  let slice = self.batch_slice()
  let out : Array[Float] = Array::make(self.t * slice, 0.0F)
  for t_idx in 0..<self.t {
    let src_off = t_idx * self.frame_block() + b * slice
    let dst_off = t_idx * slice
    for i in 0..<slice {
      out[dst_off + i] = self.data[src_off + i]
    }
  }
  { data: out, n: self.t, c: self.c, h: self.h, w: self.w }
}

///|
/// Stack `T` 4D `[N=1, C, H, W]` images along a new time axis.
/// All input images must share the same (c, h, w) shape. Result is
/// `[T, 1, C, H, W]`.
pub fn STImage::from_frames(frames : Array[Image]) -> STImage raise Failure {
  if frames.length() == 0 { raise Failure::Failure("from_frames: empty array") }
  let f0 = frames[0]
  let (_, c, h, w) = f0.shape()
  let t = frames.length()
  let plane = c * h * w
  let out : Array[Float] = Array::make(t * plane, 0.0F)
  for t_idx in 0..<t {
    let f = frames[t_idx]
    let (n_i, c_i, h_i, w_i) = f.shape()
    if n_i != 1 || c_i != c || h_i != h || w_i != w {
      raise Failure::Failure(
        "from_frames: frame \{t_idx} has shape (\{n_i},\{c_i},\{h_i},\{w_i}), expected (1,\{c},\{h},\{w})",
      )
    }
    for i in 0..<plane {
      out[t_idx * plane + i] = f.data[i]
    }
  }
  { data: out, t, b: 1, c, h, w }
}

///|
/// Concatenate two STImages along the time axis. Shapes must agree on
/// (B, C, H, W).
pub fn st_image_concat_t(
  a : STImage,
  b : STImage,
) -> STImage raise Failure {
  if a.b != b.b || a.c != b.c || a.h != b.h || a.w != b.w {
    raise Failure::Failure(
      "st_image_concat_t: shape mismatch (\{a.b},\{a.c},\{a.h},\{a.w}) vs (\{b.b},\{b.c},\{b.h},\{b.w})",
    )
  }
  let t = a.t + b.t
  let slice = a.batch_slice()
  let out : Array[Float] = Array::make(t * a.b * slice, 0.0F)
  let frame_block_a = a.frame_block()
  let frame_block_b = b.frame_block()
  for t_idx in 0..<a.t {
    for i in 0..<(a.b * slice) {
      out[t_idx * a.b * slice + i] = a.data[t_idx * frame_block_a + i]
    }
  }
  for t_idx in 0..<b.t {
    for i in 0..<(b.b * slice) {
      out[(a.t + t_idx) * a.b * slice + i] = b.data[t_idx * frame_block_b + i]
    }
  }
  { data: out, t, b: a.b, c: a.c, h: a.h, w: a.w }
}

///|
/// Repeat a single 4D `[N=1, C, H, W]` image `t` times along a new
/// time axis. Result is `[T, 1, C, H, W]` with identical frames.
pub fn st_image_repeat_t(img : Image, t : Int) -> STImage {
  let (_, c, h, w) = img.shape()
  let plane = c * h * w
  let out : Array[Float] = Array::make(t * plane, 0.0F)
  for t_idx in 0..<t {
    for i in 0..<plane {
      out[t_idx * plane + i] = img.data[i]
    }
  }
  { data: out, t, b: 1, c, h, w }
}