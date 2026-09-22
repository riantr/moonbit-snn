// spatio_temporal_test.mbt 鈥?tests for 5D [T, B, C, H, W] STImage
// (v0.14.1).

///|
test "st_zeros / shape + numel" {
  let s = STImage::zeros(3, 2, 4, 5, 6)
  if s.numel() != 720 { fail("numel should be 720, got \{s.numel()}") }
  let (t, b, c, h, w) = s.shape()
  if t != 3 || b != 2 || c != 4 || h != 5 || w != 6 {
    fail("shape should be (3,2,4,5,6), got (\{t},\{b},\{c},\{h},\{w})")
  }
  for i in 0..<s.numel() {
    if s.data[i] != 0.0F { fail("zeros[\{i}] should be 0.0") }
  }
}

///|
test "st_offset / flat layout is row-major [T,B,C,H,W]" {
  // 2x2x2x3x4 tensor: T=2, B=2, C=2, H=3, W=4.
  // stride B*C*H*W = 2*2*3*4 = 48
  // stride C*H*W   = 2*3*4  = 24
  // stride H*W     = 3*4    = 12
  // stride W       = 4
  let s = STImage::zeros(2, 2, 2, 3, 4)
  // [t=1, b=0, c=1, h=2, w=3] = 1*48 + 0*24 + 1*12 + 2*4 + 3 = 71.
  if s.offset(1, 0, 1, 2, 3) != 71 {
    fail("offset(1,0,1,2,3) should be 71, got \{s.offset(1, 0, 1, 2, 3)}")
  }
  // [t=0, b=0, c=0, h=0, w=0] = 0.
  if s.offset(0, 0, 0, 0, 0) != 0 { fail("offset(0,0,0,0,0) should be 0") }
  // [t=1, b=1, c=1, h=2, w=3] = 1*48 + 1*24 + 1*12 + 2*4 + 3 = 95.
  if s.offset(1, 1, 1, 2, 3) != 95 {
    fail("offset(1,1,1,2,3) should be 95, got \{s.offset(1, 1, 1, 2, 3)}")
  }
}

///|
test "st_set + st_get / round-trip" {
  let s = STImage::zeros(2, 3, 1, 2, 2)
  s.set(1, 2, 0, 1, 1, 42.5F)
  if (s.get(1, 2, 0, 1, 1) - 42.5F).abs() > 0.000001F {
    fail("set/get round-trip failed, got \{s.get(1, 2, 0, 1, 1)}")
  }
  // Other positions untouched.
  if s.get(0, 0, 0, 0, 0) != 0.0F { fail("neighbour should be 0") }
  if s.get(1, 2, 0, 1, 0) != 0.0F { fail("neighbour should be 0") }
}

///|
test "st_get_frame / extracts one time slice as 4D Image" {
  let s = STImage::zeros(3, 1, 2, 2, 2)
  // Fill each frame with a distinct constant so we can verify it.
  for t_idx in 0..<3 {
    for i in 0..<s.batch_slice() {
      s.data[t_idx * s.batch_slice() + i] = Float::from_int(t_idx + 1)
    }
  }
  // Frame 0 should be all 1.0s.
  let f0 = s.get_frame(0)
  let (n0, c0, h0, w0) = f0.shape()
  if n0 != 1 || c0 != 2 || h0 != 2 || w0 != 2 {
    fail("frame 0 shape should be (1,2,2,2), got (\{n0},\{c0},\{h0},\{w0})")
  }
  for i in 0..<f0.numel() {
    if (f0.data[i] - 1.0F).abs() > 0.000001F {
      fail("frame 0 pixel \{i} should be 1.0, got \{f0.data[i]}")
    }
  }
  // Frame 2 should be all 3.0s.
  let f2 = s.get_frame(2)
  for i in 0..<f2.numel() {
    if (f2.data[i] - 3.0F).abs() > 0.000001F {
      fail("frame 2 pixel \{i} should be 3.0, got \{f2.data[i]}")
    }
  }
  // get_frame must NOT alias: mutating f0 doesn't affect s.
  f0.data[0] = 99.0F
  if s.get(0, 0, 0, 0, 0) == 99.0F {
    fail("get_frame should produce an independent copy, not alias")
  }
}

///|
test "st_get_batch / flattens time into the N axis" {
  // T=3, B=2, C=1, H=1, W=1.
  let s = STImage::zeros(3, 2, 1, 1, 1)
  // Set distinct values so we can verify ordering.
  s.set(0, 0, 0, 0, 0, 1.0F)
  s.set(0, 1, 0, 0, 0, 2.0F)
  s.set(1, 0, 0, 0, 0, 3.0F)
  s.set(1, 1, 0, 0, 0, 4.0F)
  s.set(2, 0, 0, 0, 0, 5.0F)
  s.set(2, 1, 0, 0, 0, 6.0F)
  // get_batch(0) should give Image { n=3, c=1, h=1, w=1 } with [1, 3, 5].
  let b0 = s.get_batch(0)
  let (n0, c0, h0, w0) = b0.shape()
  if n0 != 3 || c0 != 1 || h0 != 1 || w0 != 1 {
    fail("batch 0 shape should be (3,1,1,1), got (\{n0},\{c0},\{h0},\{w0})")
  }
  if (b0.data[0] - 1.0F).abs() > 0.000001F { fail("b0[0] should be 1.0") }
  if (b0.data[1] - 3.0F).abs() > 0.000001F { fail("b0[1] should be 3.0") }
  if (b0.data[2] - 5.0F).abs() > 0.000001F { fail("b0[2] should be 5.0") }
  // get_batch(1) should give [2, 4, 6].
  let b1 = s.get_batch(1)
  if (b1.data[0] - 2.0F).abs() > 0.000001F { fail("b1[0] should be 2.0") }
  if (b1.data[1] - 4.0F).abs() > 0.000001F { fail("b1[1] should be 4.0") }
  if (b1.data[2] - 6.0F).abs() > 0.000001F { fail("b1[2] should be 6.0") }
}

///|
test "st_from_frames / stacks N=1 images along time axis" {
  let f0 = Image::zeros(1, 3, 2, 2)
  // Distinguish the two frames so we can verify ordering.
  for i in 0..<f0.numel() {
    f0.data[i] = 1.0F
  }
  let f1 = Image::zeros(1, 3, 2, 2)
  for i in 0..<f1.numel() {
    f1.data[i] = 2.0F
  }
  let s = STImage::from_frames([f0, f1])
  let (t, b, c, h, w) = s.shape()
  if t != 2 || b != 1 || c != 3 || h != 2 || w != 2 {
    fail("stacked shape should be (2,1,3,2,2), got (\{t},\{b},\{c},\{h},\{w})")
  }
  let g0 = s.get_frame(0)
  let g1 = s.get_frame(1)
  for i in 0..<g0.numel() {
    if (g0.data[i] - 1.0F).abs() > 0.000001F {
      fail("frame 0 pixel \{i} should be 1.0, got \{g0.data[i]}")
    }
  }
  for i in 0..<g1.numel() {
    if (g1.data[i] - 2.0F).abs() > 0.000001F {
      fail("frame 1 pixel \{i} should be 2.0, got \{g1.data[i]}")
    }
  }
}

///|
test "st_concat_t / merges two STImages along T" {
  // a: T=2, B=1, C=1, H=1, W=1, values [10, 20]
  let a = STImage::zeros(2, 1, 1, 1, 1)
  a.set(0, 0, 0, 0, 0, 10.0F)
  a.set(1, 0, 0, 0, 0, 20.0F)
  // b: T=3, B=1, C=1, H=1, W=1, values [30, 40, 50]
  let b = STImage::zeros(3, 1, 1, 1, 1)
  b.set(0, 0, 0, 0, 0, 30.0F)
  b.set(1, 0, 0, 0, 0, 40.0F)
  b.set(2, 0, 0, 0, 0, 50.0F)
  let c = st_image_concat_t(a, b)
  let (t, _b, _c, _h, _w) = c.shape()
  if t != 5 { fail("concat T should be 5, got \{t}") }
  if (c.get(0, 0, 0, 0, 0) - 10.0F).abs() > 0.000001F { fail("c[0] should be 10") }
  if (c.get(1, 0, 0, 0, 0) - 20.0F).abs() > 0.000001F { fail("c[1] should be 20") }
  if (c.get(2, 0, 0, 0, 0) - 30.0F).abs() > 0.000001F { fail("c[2] should be 30") }
  if (c.get(3, 0, 0, 0, 0) - 40.0F).abs() > 0.000001F { fail("c[3] should be 40") }
  if (c.get(4, 0, 0, 0, 0) - 50.0F).abs() > 0.000001F { fail("c[4] should be 50") }
}

///|
test "st_repeat_t / duplicates a single image T times" {
  let img = Image::zeros(1, 2, 2, 2)
  // Set a recognisable pattern.
  for i in 0..<img.numel() {
    img.data[i] = Float::from_int(i + 1)
  }
  let s = st_image_repeat_t(img, 4)
  let (t, b, c, h, w) = s.shape()
  if t != 4 || b != 1 || c != 2 || h != 2 || w != 2 {
    fail("repeat shape should be (4,1,2,2,2), got (\{t},\{b},\{c},\{h},\{w})")
  }
  for t_idx in 0..<4 {
    let frame = s.get_frame(t_idx)
    for i in 0..<frame.numel() {
      if (frame.data[i] - Float::from_int(i + 1)).abs() > 0.000001F {
        fail("repeat frame \{t_idx} pixel \{i} mismatch, got \{frame.data[i]}")
      }
    }
  }
}

///|
test "st_get_frame / preserves exact Float32 values across a small STImage" {
  // 1x1x2x2x2: write 8 distinct Float32 values, retrieve each frame,
  // bit-exact check (no broadcasting / reshape involved).
  let s = STImage::zeros(1, 1, 2, 2, 2)
  let pattern : Array[Float] = [0.1F, 0.2F, 0.3F, 0.4F, 0.5F, 0.6F, 0.7F, 0.8F]
  for i in 0..<pattern.length() {
    s.data[i] = pattern[i]
  }
  let f = s.get_frame(0)
  for i in 0..<pattern.length() {
    if f.data[i] != pattern[i] {
      fail("round-trip pixel \{i}: expected \{pattern[i]}, got \{f.data[i]}")
    }
  }
}