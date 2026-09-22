// linear_test.mbt 鈥?tests for linear_forward (v0.11.1).

///|
test "linear_forward / single-batch matvec + bias" {
  // N=1, in=3, out=2. y = W @ x + b.
  //   W = [[1, 2, 3],   x = [10, 20, 30]   b = [100, 200]
  //        [4, 5, 6]]
  //   y[0] = 1*10 + 2*20 + 3*30 + 100 = 10 + 40 + 90 + 100 = 240
  //   y[1] = 4*10 + 5*20 + 6*30 + 200 = 40 + 100 + 180 + 200 = 520
  let weight : Array[Float] = [1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F]
  let bias : Array[Float] = [100.0F, 200.0F]
  let param = LinearParam::new(weight, bias, 3, 2)
  let input : Array[Float] = [10.0F, 20.0F, 30.0F]
  let out = linear_forward(input, 1, param)
  if out.length() != 2 { fail("expected length 2, got \{out.length()}") }
  if (out[0] - 240.0F).abs() > 1.0e-5 { fail("out[0] expected 240, got \{out[0]}") }
  if (out[1] - 520.0F).abs() > 1.0e-5 { fail("out[1] expected 520, got \{out[1]}") }
}

///|
test "linear_forward / batch N=3 each row independent" {
  // N=3, in=2, out=2. Weight is identity-like.
  //   W = [[1, 0], [0, 1]], b = [10, 20].
  // Batch 0 x=[1,2] -> y=[11, 22]
  // Batch 1 x=[3,4] -> y=[13, 24]
  // Batch 2 x=[5,6] -> y=[15, 26]
  let weight : Array[Float] = [1.0F, 0.0F, 0.0F, 1.0F]
  let bias : Array[Float] = [10.0F, 20.0F]
  let param = LinearParam::new(weight, bias, 2, 2)
  let input : Array[Float] = [
    1.0F, 2.0F,
    3.0F, 4.0F,
    5.0F, 6.0F,
  ]
  let out = linear_forward(input, 3, param)
  if out.length() != 6 { fail("expected length 6, got \{out.length()}") }
  let expected : Array[Float] = [
    11.0F, 22.0F,
    13.0F, 24.0F,
    15.0F, 26.0F,
  ]
  for i in 0..<6 {
    if (out[i] - expected[i]).abs() > 1.0e-5 {
      fail("out[\{i}] expected \{expected[i]}, got \{out[i]}")
    }
  }
}

///|
test "linear_forward / zero input gives just bias" {
  // Even with non-zero weight, zero input -> output = bias.
  let n = 4
  let in_f = 3
  let out_f = 5
  let input : Array[Float] = Array::make(n * in_f, 0.0F)
  let weight : Array[Float] = Array::make(out_f * in_f, 0.5F)
  let bias : Array[Float] = [1.0F, -1.0F, 2.5F, 0.0F, 100.0F]
  let param = LinearParam::new(weight, bias, in_f, out_f)
  let out = linear_forward(input, n, param)
  if out.length() != n * out_f {
    fail("expected length \{n * out_f}, got \{out.length()}")
  }
  for batch in 0..<n {
    for o in 0..<out_f {
      let idx = batch * out_f + o
      if (out[idx] - bias[o]).abs() > 1.0e-5 {
        fail("out[batch=\{batch},o=\{o}] expected \{bias[o]}, got \{out[idx]}")
      }
    }
  }
}

///|
test "linear_forward / zero weight gives just bias" {
  // N=1, in=4, out=3. Zero weight, all-input-ones, bias [10,20,30].
  // Output = bias for any input.
  let n = 1
  let in_f = 4
  let out_f = 3
  let weight : Array[Float] = Array::make(out_f * in_f, 0.0F)
  let bias : Array[Float] = [10.0F, 20.0F, 30.0F]
  let param = LinearParam::new(weight, bias, in_f, out_f)
  let input : Array[Float] = [1.0F, 2.0F, 3.0F, 4.0F]
  let out = linear_forward(input, n, param)
  for o in 0..<out_f {
    if (out[o] - bias[o]).abs() > 1.0e-5 {
      fail("out[\{o}] expected \{bias[o]}, got \{out[o]}")
    }
  }
}

///|
test "linear_forward / single-output scalar projection" {
  // N=2, in=3, out=1. Equivalent to: y[n] = sum_i W[0, i] * x[n, i] + b[0].
  let weight : Array[Float] = [2.0F, 3.0F, 5.0F]  // [out=0, in=0..2]
  let bias : Array[Float] = [7.0F]
  let param = LinearParam::new(weight, bias, 3, 1)
  let input : Array[Float] = [
    1.0F, 1.0F, 1.0F,   // batch 0: y = 2+3+5 + 7 = 17
    2.0F, 0.0F, 1.0F,   // batch 1: y = 4+0+5 + 7 = 16
  ]
  let out = linear_forward(input, 2, param)
  if out.length() != 2 { fail("expected length 2, got \{out.length()}") }
  if (out[0] - 17.0F).abs() > 1.0e-5 { fail("out[0] expected 17, got \{out[0]}") }
  if (out[1] - 16.0F).abs() > 1.0e-5 { fail("out[1] expected 16, got \{out[1]}") }
}