// conv2d_test.mbt 鈥?tests for conv2d_forward (v0.11.0).

///|
test "conv2d_forward / 1x1 conv is per-channel scalar + bias" {
  // N=1, C_in=1, H=2, W=2. Weight is a single scalar 2.0, bias 1.0.
  // Output = 2 * input + 1, same spatial shape (kh=kw=1, pad=0, stride=1).
  let input : Array[Float] = [1.0F, 2.0F, 3.0F, 4.0F]
  let weight : Array[Float] = [2.0F]
  let bias : Array[Float] = [1.0F]
  let param = Conv2dParam::new(weight, bias, 1, 1, 1, 1, stride=1, pad=0)
  let out = conv2d_forward(input, 1, 1, 2, 2, param)
  if out.length() != 4 { fail("expected length 4, got \{out.length()}") }
  // Expected: [1*2+1, 2*2+1, 3*2+1, 4*2+1] = [3, 5, 7, 9]
  let expected : Array[Float] = [3.0F, 5.0F, 7.0F, 9.0F]
  for i in 0..<4 {
    if (out[i] - expected[i]).abs() > 1.0e-6 {
      fail("out[\{i}] expected \{expected[i]}, got \{out[i]}")
    }
  }
}

///|
test "conv2d_forward / 3x3 conv on 5x5 valid -> 3x3 output (sum-of-window)" {
  // N=1, C_in=1, H=5, W=5, kh=kw=3, stride=1, pad=0 -> 3x3 output.
  // Weight: all 1s, bias 0. Each output cell is the sum of a 3x3 window.
  // With all-1 input, each window = 9.
  let input : Array[Float] = Array::make(25, 1.0F)
  let weight : Array[Float] = Array::make(9, 1.0F)
  let bias : Array[Float] = [0.0F]
  let param = Conv2dParam::new(weight, bias, 1, 1, 3, 3, stride=1, pad=0)
  let out = conv2d_forward(input, 1, 1, 5, 5, param)
  if out.length() != 9 { fail("expected length 9, got \{out.length()}") }
  for i in 0..<9 {
    if (out[i] - 9.0F).abs() > 1.0e-4 {
      fail("sum-of-window expected 9.0, got \{out[i]} at index \{i}")
    }
  }
}

///|
test "conv2d_forward / 3x3 conv on 5x5 with pad=1 (same) -> 5x5 output" {
  // Pad=1 preserves spatial dims (when stride=1).
  // Use identity-like kernel (delta) so output == input + bias.
  // 3x3 delta kernel: center = 1, rest = 0.
  let weight : Array[Float] = [
    0.0F, 0.0F, 0.0F,
    0.0F, 1.0F, 0.0F,
    0.0F, 0.0F, 0.0F,
  ]
  let bias : Array[Float] = [0.0F]
  let param = Conv2dParam::new(weight, bias, 1, 1, 3, 3, stride=1, pad=1)
  // 3x3 input (use single-channel for simplicity).
  let input : Array[Float] = [
    1.0F, 2.0F, 3.0F,
    4.0F, 5.0F, 6.0F,
    7.0F, 8.0F, 9.0F,
  ]
  let out = conv2d_forward(input, 1, 1, 3, 3, param)
  if out.length() != 9 { fail("expected length 9, got \{out.length()}") }
  // With delta kernel + same pad, output should reproduce the input.
  for i in 0..<9 {
    if (out[i] - input[i]).abs() > 1.0e-5 {
      fail("delta kernel should reproduce input; out[\{i}]=\{out[i]} vs in=\{input[i]}")
    }
  }
}

///|
test "conv2d_forward / multi-channel input + output, stride=2 downsample" {
  // N=1, C_in=2, H=4, W=4, kh=kw=2, stride=2, pad=0 -> 2x2 output.
  // C_out=3, so weight shape [3, 2, 2, 2]. Bias length 3.
  let input : Array[Float] = Array::make(1 * 2 * 4 * 4, 1.0F)
  // Weight: 3 outputs x 2 input channels x 2x2 kernel. Set bias only.
  let weight : Array[Float] = Array::make(3 * 2 * 2 * 2, 0.0F)
  let bias : Array[Float] = [10.0F, 20.0F, 30.0F]
  let param = Conv2dParam::new(weight, bias, 3, 2, 2, 2, stride=2, pad=0)
  let out = conv2d_forward(input, 1, 2, 4, 4, param)
  // N*C_out*ho*wo = 1*3*2*2 = 12.
  if out.length() != 12 { fail("expected length 12, got \{out.length()}") }
  // All-zero weight means output == bias (broadcasted across spatial).
  // Output layout: [n=0, co=0..2, ho=0..1, wo=0..1]
  for co in 0..<3 {
    for ho in 0..<2 {
      for wo in 0..<2 {
        let idx = co * 4 + ho * 2 + wo
        if (out[idx] - bias[co]).abs() > 1.0e-5 {
          fail("out[co=\{co}, ho=\{ho}, wo=\{wo}] expected \{bias[co]}, got \{out[idx]}")
        }
      }
    }
  }
}

///|
test "conv2d_forward / batch N=2, output shape correct" {
  // N=2, C_in=1, H=3, W=3, kh=2, kw=2, stride=1, pad=0 -> 2x2 output.
  // All-zero input + bias -> output = bias broadcasted.
  let input : Array[Float] = Array::make(2 * 1 * 3 * 3, 0.0F)
  let weight : Array[Float] = Array::make(1 * 1 * 2 * 2, 0.0F)
  let bias : Array[Float] = [7.0F]
  let param = Conv2dParam::new(weight, bias, 1, 1, 2, 2, stride=1, pad=0)
  let out = conv2d_forward(input, 2, 1, 3, 3, param)
  // 2*1*2*2 = 8
  if out.length() != 8 { fail("expected length 8, got \{out.length()}") }
  for i in 0..<8 {
    if (out[i] - 7.0F).abs() > 1.0e-5 {
      fail("out[\{i}] expected 7.0, got \{out[i]}")
    }
  }
}

///|
test "conv2d_forward / 1x1 conv with explicit weight * input + bias (3x3x3, 2 out-ch)" {
  // N=1, C_in=3, H=3, W=3, kh=kw=1, pad=0, stride=1, C_out=2.
  // Set input to constant 1.0, weights to known values, bias to known.
  // Output = sum_ci (weight[co, ci] * 1) + bias[co].
  let input : Array[Float] = Array::make(1 * 3 * 3 * 3, 1.0F)
  // Weight layout [c_out=2, c_in=3, kh=1, kw=1] -> 2*3 = 6 values.
  let weight : Array[Float] = [
    1.0F, 2.0F, 3.0F, // c_out=0, c_in=0..2
    4.0F, 5.0F, 6.0F, // c_out=1, c_in=0..2
  ]
  let bias : Array[Float] = [10.0F, 20.0F]
  let param = Conv2dParam::new(weight, bias, 2, 3, 1, 1, stride=1, pad=0)
  let out = conv2d_forward(input, 1, 3, 3, 3, param)
  if out.length() != 1 * 2 * 3 * 3 {
    fail("expected length 18, got \{out.length()}")
  }
  // c_out=0: sum_ci weight[0, ci] * 1 + 10 = (1+2+3) + 10 = 16
  // c_out=1: sum_ci weight[1, ci] * 1 + 20 = (4+5+6) + 20 = 35
  // For each (ho, wo), output[n=0, co, ho, wo] = bias + sum.
  for ho in 0..<3 {
    for wo in 0..<3 {
      let i0 = 0 * 9 + ho * 3 + wo
      let i1 = 1 * 9 + ho * 3 + wo
      if (out[i0] - 16.0F).abs() > 1.0e-5 {
        fail("c_out=0 [\{ho},\{wo}] expected 16.0, got \{out[i0]}")
      }
      if (out[i1] - 35.0F).abs() > 1.0e-5 {
        fail("c_out=1 [\{ho},\{wo}] expected 35.0, got \{out[i1]}")
      }
    }
  }
}

///|
test "conv2d_forward / all-zero input gives just bias" {
  // Even with non-zero weight, zero input -> zero contribution, so
  // output equals the bias broadcast.
  let n = 1
  let c_in = 2
  let h = 3
  let w = 3
  let c_out = 4
  let input : Array[Float] = Array::make(n * c_in * h * w, 0.0F)
  let weight : Array[Float] = Array::make(c_out * c_in * 2 * 2, 0.5F)
  let bias : Array[Float] = [1.0F, -1.0F, 2.5F, 0.0F]
  let param = Conv2dParam::new(weight, bias, c_out, c_in, 2, 2, stride=1, pad=0)
  let out = conv2d_forward(input, n, c_in, h, w, param)
  // Output shape: n*c_out*(h-1)*(w-1) = 1*4*2*2 = 16
  if out.length() != 16 { fail("expected length 16, got \{out.length()}") }
  for co in 0..<c_out {
    for ho in 0..<2 {
      for wo in 0..<2 {
        let idx = co * 4 + ho * 2 + wo
        if (out[idx] - bias[co]).abs() > 1.0e-5 {
          fail("out[co=\{co},ho=\{ho},wo=\{wo}] expected \{bias[co]}, got \{out[idx]}")
        }
      }
    }
  }
}

///|
test "conv2d_forward / 3x3 stride-1 on 4x4 valid -> 2x2 output (known values)" {
  // N=1, C_in=1, H=4, W=4, kh=kw=3, stride=1, pad=0 -> 2x2 output.
  // Input: 4x4 with values 1..16 row-major (row 2 col 1 = 10, not 11).
  // 3x3 weight: identity-like (center=1, others=0), bias 100.
  // Output [ho=0,wo=0] uses window rows 0..2 cols 0..2 -> center input[1,1]=6 -> 106
  // Output [ho=0,wo=1] uses window rows 0..2 cols 1..3 -> center input[1,2]=7 -> 107
  // Output [ho=1,wo=0] uses window rows 1..3 cols 0..2 -> center input[2,1]=10 -> 110
  // Output [ho=1,wo=1] uses window rows 1..3 cols 1..3 -> center input[2,2]=11 -> 111
  let input : Array[Float] = [
    1.0F, 2.0F, 3.0F, 4.0F,
    5.0F, 6.0F, 7.0F, 8.0F,
    9.0F, 10.0F, 11.0F, 12.0F,
    13.0F, 14.0F, 15.0F, 16.0F,
  ]
  let weight : Array[Float] = [
    0.0F, 0.0F, 0.0F,
    0.0F, 1.0F, 0.0F,
    0.0F, 0.0F, 0.0F,
  ]
  let bias : Array[Float] = [100.0F]
  let param = Conv2dParam::new(weight, bias, 1, 1, 3, 3, stride=1, pad=0)
  let out = conv2d_forward(input, 1, 1, 4, 4, param)
  if out.length() != 4 { fail("expected length 4, got \{out.length()}") }
  let expected : Array[Float] = [106.0F, 107.0F, 110.0F, 111.0F]
  for i in 0..<4 {
    if (out[i] - expected[i]).abs() > 1.0e-5 {
      fail("out[\{i}] expected \{expected[i]}, got \{out[i]}")
    }
  }
}

///|
test "conv2d_forward / kernel 'box-blur' sums 3x3 window" {
  // N=1, C_in=1, H=3, W=3, kh=kw=3, stride=1, pad=0 -> 1x1 output.
  // Weight: all 1s, bias 0. Output = sum of all 9 input values.
  let input : Array[Float] = [
    1.0F, 2.0F, 3.0F,
    4.0F, 5.0F, 6.0F,
    7.0F, 8.0F, 9.0F,
  ]
  let weight : Array[Float] = Array::make(9, 1.0F)
  let bias : Array[Float] = [0.0F]
  let param = Conv2dParam::new(weight, bias, 1, 1, 3, 3, stride=1, pad=0)
  let out = conv2d_forward(input, 1, 1, 3, 3, param)
  if out.length() != 1 { fail("expected length 1, got \{out.length()}") }
  if (out[0] - 45.0F).abs() > 1.0e-4 {
    fail("sum expected 45.0, got \{out[0]}")
  }
}