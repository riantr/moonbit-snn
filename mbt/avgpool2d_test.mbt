// avgpool2d_test.mbt — Average pooling tests (v0.20.1).

///|
test "avgpool2d_forward / output shape is correct" {
  let p = AvgPool2dParam::new(2, 2, stride=2, pad=0)
  let input : Array[Float] = Array::make(1 * 1 * 4 * 4, 1.0F)
  let out = avgpool2d_forward(input, 1, 1, 4, 4, p)
  // 4x4 / 2x2 stride 2 -> 2x2, total 4 elements.
  if out.length() != 4 { fail("output length should be 4, got \{out.length()}") }
}

///|
test "avgpool2d_forward / 2x2 non-overlapping average" {
  let p = AvgPool2dParam::new(2, 2, stride=2, pad=0)
  // 2x2 input with 4 distinct values: [[1, 2], [3, 4]].
  let input : Array[Float] = [1.0F, 2.0F, 3.0F, 4.0F]
  let out = avgpool2d_forward(input, 1, 1, 2, 2, p)
  // The whole input is one pool: average = (1+2+3+4)/4 = 2.5.
  if out.length() != 1 { fail("should have 1 element") }
  if (out[0] - 2.5F).abs() > 0.0001F {
    fail("avg should be 2.5, got \{out[0]}")
  }
}

///|
test "avgpool2d_forward / per-channel independence" {
  // 4x4 input, 2 channels, pool 4x4 stride 1 pad 0: one pool per channel.
  let p = AvgPool2dParam::new(4, 4, stride=1, pad=0)
  // ch=0: [[1, 2, 3, 4], [5, 6, 7, 8], ...] sum=1..16 -> avg = 8.5
  // ch=1: similar, different values -> avg = 24.5
  let mut input : Array[Float] = []
  for ch in 0..<2 {
    let base = if ch == 0 { 1 } else { 17 }
    for k in 0..<16 {
      input.push(Float::from_int(base + k))
    }
  }
  let out = avgpool2d_forward(input, 1, 2, 4, 4, p)
  if out.length() != 2 { fail("output length should be 2, got \{out.length()}") }
  // ch=0: avg of 1..16 = (1+16)*16/2 / 16 = 17/2 = 8.5
  if (out[0] - 8.5F).abs() > 0.0001F {
    fail("ch0 avg should be 8.5, got \{out[0]}")
  }
  // ch=1: avg of 17..32 = (17+32)*16/2 / 16 = 49/2 = 24.5
  if (out[1] - 24.5F).abs() > 0.0001F {
    fail("ch1 avg should be 24.5, got \{out[1]}")
  }
}

///|
test "avgpool2d_backward / distributes gradient uniformly" {
  // 2x2 input, 2x2 pool with stride 2. d_output = [2.0].
  // Backward should give every input position d_input = 2.0 / 4 = 0.5.
  let p = AvgPool2dParam::new(2, 2, stride=2, pad=0)
  let input : Array[Float] = Array::make(1 * 1 * 2 * 2, 1.0F)
  let d_output : Array[Float] = [2.0F]
  let d_input = avgpool2d_backward(d_output, 1, 1, 2, 2, p)
  for i in 0..<4 {
    if (d_input[i] - 0.5F).abs() > 0.0001F {
      fail("d_input[\{i}] should be 0.5, got \{d_input[i]}")
    }
  }
}

///|
test "avgpool2d_backward / overlapping pools accumulate gradient" {
  // 4x4 input, 2x2 pool stride 1 -> 3x3 = 9 pools. Position (0,0)
  // belongs to pool (0,0) only (1/4 = 0.25). Position (1,1) belongs
  // to all 4 corner pools (4 * 1/4 = 1.0).
  let p = AvgPool2dParam::new(2, 2, stride=1, pad=0)
  let d_output : Array[Float] = Array::make(9, 1.0F)
  let d_input = avgpool2d_backward(d_output, 1, 1, 4, 4, p)
  // Position (0,0): pool (0,0) only -> 0.25
  if (d_input[0] - 0.25F).abs() > 0.0001F {
    fail("(0,0) should be 0.25, got \{d_input[0]}")
  }
  // Position (1,1) flat index = 1*4 + 1 = 5: 4 pools contribute -> 1.0
  if (d_input[5] - 1.0F).abs() > 0.0001F {
    fail("(1,1) should be 1.0, got \{d_input[5]}")
  }
  // Position (3,3) flat index = 3*4 + 3 = 15: pool (2,2) only -> 0.25
  if (d_input[15] - 0.25F).abs() > 0.0001F {
    fail("(3,3) should be 0.25, got \{d_input[15]}")
  }
}

///|
test "global_avg_pool2d_forward / output shape = (n, c)" {
  let n = 2
  let c = 3
  let h = 4
  let w = 5
  let input : Array[Float] = Array::make(n * c * h * w, 2.0F)
  let out = global_avg_pool2d_forward(input, n, c, h, w)
  if out.length() != n * c { fail("output length should be n*c = \{n * c}") }
  // All 2.0 -> average is 2.0.
  for i in 0..<out.length() {
    if (out[i] - 2.0F).abs() > 0.0001F {
      fail("output[\{i}] should be 2.0, got \{out[i]}")
    }
  }
}

///|
test "global_avg_pool2d_backward / distributes gradient uniformly" {
  let n = 1
  let c = 1
  let h = 2
  let w = 3
  // d_output = [2.0] -> each input position gets 2.0 / 6 = 0.333.
  let d_output : Array[Float] = [2.0F]
  let d_input = global_avg_pool2d_backward(d_output, n, c, h, w)
  if d_input.length() != n * c * h * w { fail("d_input length mismatch") }
  let expected = 2.0F / Float::from_int(h * w)
  for i in 0..<d_input.length() {
    if (d_input[i] - expected).abs() > 0.0001F {
      fail("d_input[\{i}] should be \{expected}, got \{d_input[i]}")
    }
  }
}

///|
test "global_avg_pool2d / round-trip with random values" {
  let n = 2
  let c = 2
  let h = 3
  let w = 3
  let input : Array[Float] = Array::make(n * c * h * w, 0.0F)
  for i in 0..<input.length() {
    input[i] = Float::from_int(i + 1)
  }
  let out = global_avg_pool2d_forward(input, n, c, h, w)
  // Compute expected average per (n, c).
  for ni in 0..<n {
    for ci in 0..<c {
      let mut sum = 0.0F
      for hi in 0..<h {
        for wi in 0..<w {
          let idx = ni * c * h * w + ci * h * w + hi * w + wi
          sum = sum + input[idx]
        }
      }
      let expected = sum / Float::from_int(h * w)
      let out_idx = ni * c + ci
      if (out[out_idx] - expected).abs() > 0.0001F {
        fail("sample (\{ni},\{ci}) expected \{expected}, got \{out[out_idx]}")
      }
    }
  }
}