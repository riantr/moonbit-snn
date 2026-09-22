// maxpool2d_test.mbt 鈥?tests for maxpool2d_forward (v0.11.0).

///|
test "maxpool2d_forward / 2x2 non-overlapping on 4x4 -> 2x2" {
  // N=1, C=1, H=4, W=4, kh=kw=2, stride=2, pad=0 -> 2x2 output.
  // Input: 4x4 with each 2x2 block having a distinct max.
  let input : Array[Float] = [
    1.0F, 2.0F, 3.0F, 4.0F,
    5.0F, 6.0F, 7.0F, 8.0F,
    9.0F, 10.0F, 11.0F, 12.0F,
    13.0F, 14.0F, 15.0F, 16.0F,
  ]
  let param = MaxPool2dParam::new(2, 2, stride=2, pad=0)
  let out = maxpool2d_forward(input, 1, 1, 4, 4, param)
  if out.length() != 4 { fail("expected length 4, got \{out.length()}") }
  // Block 0 (rows 0-1, cols 0-1): max=6
  // Block 1 (rows 0-1, cols 2-3): max=8
  // Block 2 (rows 2-3, cols 0-1): max=14
  // Block 3 (rows 2-3, cols 2-3): max=16
  let expected : Array[Float] = [6.0F, 8.0F, 14.0F, 16.0F]
  for i in 0..<4 {
    if (out[i] - expected[i]).abs() > 1.0e-5 {
      fail("out[\{i}] expected \{expected[i]}, got \{out[i]}")
    }
  }
}

///|
test "maxpool2d_forward / stride=1 overlapping 2x2 on 3x3 -> 2x2 output" {
  // N=1, C=1, H=3, W=3, kh=kw=2, stride=1, pad=0 -> 2x2 output.
  // Input:
  //   1 2 3
  //   4 5 6
  //   7 8 9
  // Out[0,0] = max(1,2,4,5) = 5
  // Out[0,1] = max(2,3,5,6) = 6
  // Out[1,0] = max(4,5,7,8) = 8
  // Out[1,1] = max(5,6,8,9) = 9
  let input : Array[Float] = [
    1.0F, 2.0F, 3.0F,
    4.0F, 5.0F, 6.0F,
    7.0F, 8.0F, 9.0F,
  ]
  let param = MaxPool2dParam::new(2, 2, stride=1, pad=0)
  let out = maxpool2d_forward(input, 1, 1, 3, 3, param)
  if out.length() != 4 { fail("expected length 4, got \{out.length()}") }
  let expected : Array[Float] = [5.0F, 6.0F, 8.0F, 9.0F]
  for i in 0..<4 {
    if (out[i] - expected[i]).abs() > 1.0e-5 {
      fail("out[\{i}] expected \{expected[i]}, got \{out[i]}")
    }
  }
}

///|
test "maxpool2d_forward / multi-channel per-channel max" {
  // N=1, C=3, H=2, W=2, kh=kw=2, stride=2, pad=0 -> 1x1 per channel.
  // Each channel has a different max value.
  let input : Array[Float] = [
    // channel 0: max = 7
    1.0F, 2.0F,
    3.0F, 7.0F,
    // channel 1: max = -1
    -2.0F, -3.0F,
    -1.0F, -4.0F,
    // channel 2: max = 100
    50.0F, 60.0F,
    100.0F, 70.0F,
  ]
  let param = MaxPool2dParam::new(2, 2, stride=2, pad=0)
  let out = maxpool2d_forward(input, 1, 3, 2, 2, param)
  if out.length() != 3 { fail("expected length 3, got \{out.length()}") }
  let expected : Array[Float] = [7.0F, -1.0F, 100.0F]
  for i in 0..<3 {
    if (out[i] - expected[i]).abs() > 1.0e-5 {
      fail("out[c=\{i}] expected \{expected[i]}, got \{out[i]}")
    }
  }
}

///|
test "maxpool2d_forward / batch N=2" {
  // N=2, C=1, H=2, W=2, kh=kw=2, stride=2, pad=0 -> 1 per batch.
  let input : Array[Float] = [
    // batch 0
    1.0F, 2.0F,
    3.0F, 4.0F,
    // batch 1
    5.0F, 6.0F,
    7.0F, 8.0F,
  ]
  let param = MaxPool2dParam::new(2, 2, stride=2, pad=0)
  let out = maxpool2d_forward(input, 2, 1, 2, 2, param)
  if out.length() != 2 { fail("expected length 2, got \{out.length()}") }
  if (out[0] - 4.0F).abs() > 1.0e-5 { fail("batch 0 expected 4, got \{out[0]}") }
  if (out[1] - 8.0F).abs() > 1.0e-5 { fail("batch 1 expected 8, got \{out[1]}") }
}

///|
test "maxpool2d_forward / padding=1 on 4x3 -> 3x2 with stride 2" {
  // H=4, W=3, kh=kw=2, stride=2, pad=1.
  //   ho = (4+2-2)/2 + 1 = 3
  //   wo = (3+2-2)/2 + 1 = 2
  // Output shape 3x2 = 6 elements.
  // Pad-region values are -inf, so windows touching the border pick
  // fewer input cells.
  // Windows (in (ih, iw) input coords):
  //   ho=0: ih in {-1, 0}; wo=0: iw in {-1, 0} -> only input[0,0]=1
  //                            wo=1: iw in {1, 2} -> max(input[0,1], input[0,2]) = max(2,3) = 3
  //   ho=1: ih in {1, 2};  wo=0: iw in {-1, 0} -> max(input[1,0], input[2,0]) = max(4,7) = 7
  //                            wo=1: iw in {1, 2} -> max(input[1..2, 1..2]) = max(5,6,8,9) = 9
  //   ho=2: ih in {3, 4};  wo=0: iw in {-1, 0} -> only input[3,0]=10
  //                            wo=1: iw in {1, 2} -> max(input[3,1], input[3,2]) = max(11,12) = 12
  let input : Array[Float] = [
    1.0F, 2.0F, 3.0F,
    4.0F, 5.0F, 6.0F,
    7.0F, 8.0F, 9.0F,
    10.0F, 11.0F, 12.0F,
  ]
  let param = MaxPool2dParam::new(2, 2, stride=2, pad=1)
  let out = maxpool2d_forward(input, 1, 1, 4, 3, param)
  if out.length() != 6 { fail("expected length 6, got \{out.length()}") }
  let expected : Array[Float] = [1.0F, 3.0F, 7.0F, 9.0F, 10.0F, 12.0F]
  for i in 0..<6 {
    if (out[i] - expected[i]).abs() > 1.0e-5 {
      fail("out[\{i}] expected \{expected[i]}, got \{out[i]}")
    }
  }
}

///|
test "maxpool2d_forward / all-negative values picks least-negative" {
  // All values are negative; max should be the largest (least negative).
  let input : Array[Float] = [-5.0F, -2.0F, -8.0F, -1.0F]
  let param = MaxPool2dParam::new(2, 2, stride=2, pad=0)
  let out = maxpool2d_forward(input, 1, 1, 2, 2, param)
  if out.length() != 1 { fail("expected length 1, got \{out.length()}") }
  if (out[0] - (-1.0F)).abs() > 1.0e-5 {
    fail("expected -1.0, got \{out[0]}")
  }
}

///|
test "maxpool2d_forward / 3x3 pool on 5x5 valid -> 3x3 output" {
  // N=1, C=1, H=5, W=5, kh=kw=3, stride=1, pad=0 -> 3x3 output.
  // Input: identity matrix (5x5, all zeros except 1.0 on the diagonal).
  let input : Array[Float] = Array::make(25, 0.0F)
  input[0 * 5 + 0] = 1.0F
  input[1 * 5 + 1] = 1.0F
  input[2 * 5 + 2] = 1.0F
  input[3 * 5 + 3] = 1.0F
  input[4 * 5 + 4] = 1.0F
  let param = MaxPool2dParam::new(3, 3, stride=1, pad=0)
  let out = maxpool2d_forward(input, 1, 1, 5, 5, param)
  if out.length() != 9 { fail("expected length 9, got \{out.length()}") }
  // The diagonal of the input has 1.0; any 3x3 window that contains a
  // diagonal pixel should have max=1.0. All 3x3 windows in a 5x5 grid
  // (3x3 of them) include a diagonal pixel (since the diagonal spans
  // all rows/cols). So all outputs should be 1.0.
  for i in 0..<9 {
    if (out[i] - 1.0F).abs() > 1.0e-5 {
      fail("out[\{i}] expected 1.0, got \{out[i]}")
    }
  }
}

///|
test "maxpool2d_forward / 3x3 pool on 5x5 with 0/1 corners only" {
  // N=1, C=1, H=5, W=5, kh=kw=3, stride=1, pad=0 -> 3x3 output.
  // All inputs are 0 except top-left corner (input[0,0]=1.0).
  // 3x3 windows starting at (0,0) cover input[0..2, 0..2]. The top-left
  // window contains the 1.0; subsequent windows don't.
  let input : Array[Float] = Array::make(25, 0.0F)
  input[0] = 1.0F
  let param = MaxPool2dParam::new(3, 3, stride=1, pad=0)
  let out = maxpool2d_forward(input, 1, 1, 5, 5, param)
  if out.length() != 9 { fail("expected length 9, got \{out.length()}") }
  // Only out[ho=0, wo=0] contains the 1.0; the rest are 0.
  if (out[0] - 1.0F).abs() > 1.0e-5 { fail("out[0] expected 1.0, got \{out[0]}") }
  for i in 1..<9 {
    if (out[i] - 0.0F).abs() > 1.0e-5 {
      fail("out[\{i}] expected 0.0, got \{out[i]}")
    }
  }
}