// image_test.mbt — tests for Image struct + decoder adapter (v0.14.0).
//
// Note: cannot construct upstream `Image` directly from outside the
// package (PixelFormat variants are read-only). Tests therefore go
// through `image_from_bytes` with hand-crafted BMP payloads.

///|
test "image_zeros / numel + shape" {
  let img = Image::zeros(2, 3, 4, 5)
  if img.numel() != 120 { fail("numel should be 120, got \{img.numel()}") }
  let (n, c, h, w) = img.shape()
  if n != 2 || c != 3 || h != 4 || w != 5 {
    fail("shape should be (2,3,4,5), got (\{n},\{c},\{h},\{w})")
  }
  for i in 0..<img.numel() {
    if img.data[i] != 0.0F { fail("zeros[\{i}] expected 0.0") }
  }
}

///|
/// Wrap an Int as a 1-byte Bytes.
fn int_to_bytes(b : Int) -> Bytes {
  let arr : Array[Byte] = [b.to_byte()]
  Bytes::from_array(arr)
}

///|
/// Wrap a small Array[Int] as Bytes.
fn int_arr_to_bytes(a : Array[Int]) -> Bytes {
  let buf : Array[Byte] = Array::make(a.length(), b'\x00')
  for i in 0..<a.length() { buf[i] = a[i].to_byte() }
  Bytes::from_array(buf)
}

///|
/// Build a minimal 24-bit uncompressed BMP as `Bytes`.
/// `pixels_bgr_bottom_up` is an Array[Int] of length w*h*3 with the
/// pixel data laid out bottom-up (visual bottom row first), each pixel
/// in BGR order (the byte order BMP actually stores). Upstream's BMP
/// decoder reads the height as `U32` and treats `< 0` as never true, so
/// negative heights (top-down BMPs) overflow the bounds check and fail
/// with `DecodeError::InvalidHeader`. Positive height + bottom-up
/// layout is the safe path.
///
/// BMP rows are padded to a 4-byte boundary; we therefore pad each row
/// with zero bytes to `buf_stride = ((w * 3 + 3) / 4) * 4`.
fn make_bmp_24bit(w : Int, h : Int, pixels_bgr_bottom_up : Array[Int]) -> Bytes {
  let row_bytes = w * 3
  let row_stride = (row_bytes + 3) / 4 * 4
  let pad = row_stride - row_bytes
  let pixel_data_size = h * row_stride
  let file_size = 14 + 40 + pixel_data_size
  let zero_bytes : Bytes = int_to_bytes(0)
  let mut bytes : Bytes = b""
  // File header (14 bytes).
  bytes = bytes + b"BM"
  let file_bytes = [file_size & 0xFF, (file_size >> 8) & 0xFF,
                    (file_size >> 16) & 0xFF, (file_size >> 24) & 0xFF]
  bytes = bytes + int_arr_to_bytes(file_bytes)
  for _ in 0..<4 { bytes = bytes + zero_bytes } // reserved
  let data_offset = 14 + 40
  let do_bytes = [data_offset & 0xFF, (data_offset >> 8) & 0xFF,
                  (data_offset >> 16) & 0xFF, (data_offset >> 24) & 0xFF]
  bytes = bytes + int_arr_to_bytes(do_bytes)
  // DIB header (BITMAPINFOHEADER = 40 bytes).
  bytes = bytes + int_arr_to_bytes([40, 0, 0, 0])
  let w_bytes = [w & 0xFF, (w >> 8) & 0xFF, 0, 0]
  bytes = bytes + int_arr_to_bytes(w_bytes)
  let h_bytes = [h & 0xFF, (h >> 8) & 0xFF,
                 (h >> 16) & 0xFF, (h >> 24) & 0xFF]
  bytes = bytes + int_arr_to_bytes(h_bytes)
  bytes = bytes + int_arr_to_bytes([1, 0]) // planes = 1
  bytes = bytes + int_arr_to_bytes([24, 0]) // bits per pixel = 24
  for _ in 0..<4 { bytes = bytes + zero_bytes } // compression = BI_RGB
  for _ in 0..<4 { bytes = bytes + zero_bytes } // image size
  for _ in 0..<8 { bytes = bytes + zero_bytes } // x/y pixels per meter
  for _ in 0..<8 { bytes = bytes + zero_bytes } // colors used / important
  // Pixel data (bottom-up: image y=h-1 first), each row padded to
  // 4-byte boundary.
  for r in 0..<h {
    let row_off = r * row_bytes
    for c in 0..<row_bytes {
      bytes = bytes + int_to_bytes(pixels_bgr_bottom_up[row_off + c])
    }
    for _ in 0..<pad { bytes = bytes + zero_bytes }
  }
  bytes
}

///|
test "image_from_bytes / 24-bit BMP 2x2 with 4 distinct pixels decodes correctly" {
  // 2x2 image, layout:
  //   image y=0 (top):    red, green
  //   image y=1 (bottom): blue, white
  // Bottom-up BMP file layout (file row 0 = image y=h-1):
  //   file row 0: blue, white
  //   file row 1: red, green
  let pixels : Array[Int] = [
    255, 0, 0,    // file row 0: (0,1)=blue:  B=255, G=0,   R=0
    255, 255, 255,// file row 0: (1,1)=white: B=255, G=255, R=255
    0, 0, 255,    // file row 1: (0,0)=red:   B=0,   G=0,   R=255
    0, 255, 0,    // file row 1: (1,0)=green: B=0,   G=255, R=0
  ]
  let bytes = make_bmp_24bit(2, 2, pixels)
  let img = image_from_bytes(bytes)
  let (_n, c, h, w) = img.shape()
  if c != 3 || h != 2 || w != 2 {
    fail("expected (1,3,2,2), got (?,\{c},\{h},\{w})")
  }
  let hw = h * w
  // (0,0) = red: R=1.0, G=0, B=0.
  if (img.data[0 * hw + 0] - 1.0F).abs() > 0.01F {
    fail("(0,0) R expected ~1.0, got \{img.data[0 * hw + 0]}")
  }
  if img.data[1 * hw + 0] > 0.01F {
    fail("(0,0) G should be ~0, got \{img.data[1 * hw + 0]}")
  }
  // (1,0) = green: G=1.0.
  if (img.data[1 * hw + 1] - 1.0F).abs() > 0.01F {
    fail("(1,0) G expected ~1.0, got \{img.data[1 * hw + 1]}")
  }
  // (0,1) = blue: B=1.0.
  if (img.data[2 * hw + 2] - 1.0F).abs() > 0.01F {
    fail("(0,1) B expected ~1.0, got \{img.data[2 * hw + 2]}")
  }
  // (1,1) = white: R=1.0, G=1.0, B=1.0.
  if (img.data[0 * hw + 3] - 1.0F).abs() > 0.01F {
    fail("(1,1) R expected ~1.0, got \{img.data[0 * hw + 3]}")
  }
  if (img.data[1 * hw + 3] - 1.0F).abs() > 0.01F {
    fail("(1,1) G expected ~1.0")
  }
  if (img.data[2 * hw + 3] - 1.0F).abs() > 0.01F {
    fail("(1,1) B expected ~1.0")
  }
}

///|
test "image_from_bytes / uniform 4x4 BMP decodes with all-same pixels" {
  let w = 4
  let h = 4
  // All pixels = (128, 128, 128) in BGR.
  let pixels : Array[Int] = Array::make(w * h * 3, 128)
  let bytes = make_bmp_24bit(w, h, pixels)
  let img = image_from_bytes(bytes)
  let (_n, c, h2, w2) = img.shape()
  if c != 3 || h2 != 4 || w2 != 4 {
    fail("expected (1,3,4,4), got (?,\{c},\{h2},\{w2})")
  }
  // All channels should be ~128/255 ≈ 0.502.
  let expected = 128.0F / 255.0F
  for ch in 0..<3 {
    for px in 0..<(h2 * w2) {
      let v = img.data[ch * (h2 * w2) + px]
      if (v - expected).abs() > 0.01F {
        fail("uniform px ch\{ch} px\{px} expected ~\{expected}, got \{v}")
      }
    }
  }
}

///|
test "image_from_moonbit_image_rgba / preserves alpha=255 for opaque" {
  // Build a 1x1 BMP with a single blue pixel; via the RGBA adapter the
  // alpha channel should read 1.0 (255/255).
  let pixels : Array[Int] = [255, 0, 0] // B=255, G=0, R=0 (blue)
  let bytes = make_bmp_24bit(1, 1, pixels)
  let upstream = @moonbit_image.decode(bytes)
  let rgba = image_from_moonbit_image_rgba(upstream)
  let (_, c2, h2, w2) = rgba.shape()
  if c2 != 4 || h2 != 1 || w2 != 1 {
    fail("RGBA adapter should give c=4; got c=\{c2}, h=\{h2}, w=\{w2}")
  }
  // Alpha at offset 3*h*w + 0.
  let a = rgba.data[3 * h2 * w2 + 0]
  if (a - 1.0F).abs() > 0.01F { fail("alpha should be 1.0, got \{a}") }
  // Blue channel.
  let b_v = rgba.data[2 * h2 * w2 + 0]
  if (b_v - 1.0F).abs() > 0.01F { fail("blue should be 1.0, got \{b_v}") }
}