// image.mbt 鈥?Image struct + decoder adapter over
// `riantr/moonbit_image@0.3.4` (v0.14.0).
//
// This module bridges between the upstream decoder API
// (`@moonbit_image.decode` returning an `Image` with private
// `.data` field, accessed via `get_pixel`) and the snn_mbt
// project's Float32 4D NCHW tensor convention.
//
// Conventions:
//   - `Image { data, n, c, h, w }` is a 4D NCHW Float32 image.
//     `data.length() == n * c * h * w`.
//   - Pixel values are normalised to [0.0, 1.0] Float32.
//   - Decode supports all upstream formats (BMP/QOI/TGA/PNG/GIF/
//     JPEG/ICO/TIFF, auto-detected).
//
// **Note on encode**: encoding via `@moonbit_image.encode` requires
// constructing an upstream `Image` with `Image::new(w, h, fmt, ...)`,
// but the `PixelFormat` enum variants are not publicly constructable
// from outside the upstream package. Encoding is therefore not part
// of this adapter. If you need to write an image file, write the
// raw bytes from `image_from_bytes` output and call upstream directly
// via a thin shim.

///|
/// 4D NCHW Float32 image tensor.
pub struct Image {
  data : Array[Float]
  n : Int
  c : Int
  h : Int
  w : Int
}

///|
/// Build an empty NCHW image filled with zeros.
pub fn Image::zeros(n : Int, c : Int, h : Int, w : Int) -> Image {
  { data: Array::make(n * c * h * w, 0.0F), n, c, h, w }
}

///|
/// Total element count.
pub fn Image::numel(self : Image) -> Int {
  self.data.length()
}

///|
/// Image dimensions as (n, c, h, w).
pub fn Image::shape(self : Image) -> (Int, Int, Int, Int) {
  (self.n, self.c, self.h, self.w)
}

///|
/// Normalise 8-bit channel value to Float32 in [0, 1].
fn norm8(b : Int) -> Float {
  Float::from_int(b) / 255.0F
}

///|
/// Wrap an upstream `Image` as our 4D NCHW Float Image (3 channels,
/// RGB). Alpha is dropped. To preserve alpha, use
/// `image_from_moonbit_image_rgba` instead.
pub fn image_from_moonbit_image(img : @moonbit_image.Image) -> Image {
  let h = img.height
  let w = img.width
  let data : Array[Float] = Array::make(3 * h * w, 0.0F)
  for y in 0..<h {
    for x in 0..<w {
      let px = y * w + x
      let c = img.get_pixel(x, y)
      data[0 * h * w + px] = norm8(c.r)
      data[1 * h * w + px] = norm8(c.g)
      data[2 * h * w + px] = norm8(c.b)
    }
  }
  { data, n: 1, c: 3, h, w }
}

///|
/// Wrap an upstream `Image` as RGBA (4 channels).
pub fn image_from_moonbit_image_rgba(img : @moonbit_image.Image) -> Image {
  let h = img.height
  let w = img.width
  let data : Array[Float] = Array::make(4 * h * w, 0.0F)
  for y in 0..<h {
    for x in 0..<w {
      let px = y * w + x
      let c = img.get_pixel(x, y)
      data[0 * h * w + px] = norm8(c.r)
      data[1 * h * w + px] = norm8(c.g)
      data[2 * h * w + px] = norm8(c.b)
      data[3 * h * w + px] = norm8(c.a)
    }
  }
  { data, n: 1, c: 4, h, w }
}

///|
/// Decode a byte buffer into our 4D NCHW Float Image. Auto-detects
/// the format (BMP/QOI/TGA/PNG/GIF/JPEG/ICO/TIFF). Raises
/// `@moonbit_image.DecodeError` on failure.
pub fn image_from_bytes(bytes : Bytes) -> Image raise @moonbit_image.DecodeError {
  let upstream = @moonbit_image.decode(bytes)
  image_from_moonbit_image(upstream)
}