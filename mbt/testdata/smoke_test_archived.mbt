// smoke_test.mbt 鈥?minimal smoke test of riantr/moonbit_image API.

///|
test "smoke / upstream Image::new + get_pixel round-trip" {
  // 2x1 image with two opaque red pixels.
  let bytes : Bytes = b"\xff\x00\x00\xff\xff\x00\x00\xff"
  // Try different PixelFormat syntaxes.
  let fmt = @moonbit_image.RGBA8
  let img = @moonbit_image.Image::new(2, 1, fmt, bytes)
  let c0 = img.get_pixel(0, 0)
  if c0.r != 255 { fail("(0,0).r should be 255, got \{c0.r}") }
  if c0.a != 255 { fail("(0,0).a should be 255, got \{c0.a}") }
  let c1 = img.get_pixel(1, 0)
  if c1.g != 0 { fail("(1,0).g should be 0, got \{c1.g}") }
}