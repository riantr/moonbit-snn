// tensor_ops_test.mbt 鈥?tests for matmul + softmax + log_softmax (v0.13.1).

///|
test "tensor_matmul / 2x3 times 3x2 -> 2x2" {
  // a = [[1, 2, 3], [4, 5, 6]]
  // b = [[7, 8], [9, 10], [11, 12]]
  // c[0,0] = 1*7 + 2*9 + 3*11 = 58
  // c[0,1] = 1*8 + 2*10 + 3*12 = 64
  // c[1,0] = 4*7 + 5*9 + 6*11 = 139
  // c[1,1] = 4*8 + 5*10 + 6*12 = 154
  let a = Tensor::from([1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F], [2, 3])
  let b = Tensor::from([7.0F, 8.0F, 9.0F, 10.0F, 11.0F, 12.0F], [3, 2])
  let c = tensor_matmul(a, b)
  if c.shape.length() != 2 || c.shape[0] != 2 || c.shape[1] != 2 {
    fail("matmul shape wrong")
  }
  let expected : Array[Float] = [58.0F, 64.0F, 139.0F, 154.0F]
  for i in 0..<4 {
    if (c.data[i] - expected[i]).abs() > 1.0e-4 {
      fail("matmul[\{i}] expected \{expected[i]}, got \{c.data[i]}")
    }
  }
}

///|
test "tensor_matmul / identity matrix" {
  // a (3x3) * I (3x3) == a.
  let a = Tensor::from(
    [1.0F, 2.0F, 3.0F,
     4.0F, 5.0F, 6.0F,
     7.0F, 8.0F, 9.0F],
    [3, 3],
  )
  let i_mat = Tensor::from(
    [1.0F, 0.0F, 0.0F,
     0.0F, 1.0F, 0.0F,
     0.0F, 0.0F, 1.0F],
    [3, 3],
  )
  let c = tensor_matmul(a, i_mat)
  for i in 0..<9 {
    if (c.data[i] - a.data[i]).abs() > 1.0e-5 {
      fail("a*I[\{i}] expected \{a.data[i]}, got \{c.data[i]}")
    }
  }
}

///|
test "tensor_matmul / 1x1 x 1x1 scalar" {
  let a = Tensor::from([3.0F], [1, 1])
  let b = Tensor::from([4.0F], [1, 1])
  let c = tensor_matmul(a, b)
  if c.data[0] != 12.0F { fail("3*4 expected 12, got \{c.data[0]}") }
}

///|
test "tensor_softmax / uniform input gives uniform output" {
  // All inputs equal -> softmax = 1/n each.
  let x = Tensor::from([1.0F, 1.0F, 1.0F, 1.0F], [1, 4])
  let s = tensor_softmax(x)
  for i in 0..<4 {
    if (s.data[i] - 0.25F).abs() > 1.0e-5 {
      fail("softmax[\{i}] expected 0.25, got \{s.data[i]}")
    }
  }
  // Sums to 1.
  let mut sum : Float = 0.0F
  for i in 0..<4 { sum = sum + s.data[i] }
  if (sum - 1.0F).abs() > 1.0e-5 {
    fail("softmax sum expected 1, got \{sum}")
  }
}

///|
test "tensor_softmax / large inputs stable (subtract max)" {
  // Adding a large constant to all inputs must not change softmax.
  // Without subtracting the max, exp(100) would overflow.
  let x1 = Tensor::from([1.0F, 2.0F, 3.0F], [1, 3])
  let x2 = Tensor::from([101.0F, 102.0F, 103.0F], [1, 3])
  let s1 = tensor_softmax(x1)
  let s2 = tensor_softmax(x2)
  for i in 0..<3 {
    if (s1.data[i] - s2.data[i]).abs() > 1.0e-5 {
      fail("softmax not translation-invariant: s1[\{i}]=\{s1.data[i]} vs s2[\{i}]=\{s2.data[i]}")
    }
  }
}

///|
test "tensor_softmax / batched (2 batches, 3 classes each)" {
  // Each row sums to 1.
  let x = Tensor::from(
    [1.0F, 2.0F, 3.0F,
     -1.0F, 0.0F, 1.0F],
    [2, 3],
  )
  let s = tensor_softmax(x)
  for batch in 0..<2 {
    let mut sum : Float = 0.0F
    for j in 0..<3 {
      sum = sum + s.data[batch * 3 + j]
    }
    if (sum - 1.0F).abs() > 1.0e-5 {
      fail("batch \{batch} softmax sum expected 1, got \{sum}")
    }
  }
}

///|
test "tensor_log_softmax / exp(log_softmax(x)) == softmax(x)" {
  // Verify roundtrip.
  let x = Tensor::from([0.5F, 1.0F, -0.2F, 0.8F], [1, 4])
  let ls = tensor_log_softmax(x)
  // exp each entry.
  let s_expected = tensor_softmax(x)
  for i in 0..<4 {
    let got = math_exp_f32(ls.data[i])
    if (got - s_expected.data[i]).abs() > 1.0e-5 {
      fail("exp(log_softmax)[\{i}] expected \{s_expected.data[i]}, got \{got}")
    }
  }
}

///|
test "tensor_log_softmax / sum of exp(log_softmax) == 1" {
  let x = Tensor::from([2.0F, 0.5F, -1.0F, 3.0F], [1, 4])
  let ls = tensor_log_softmax(x)
  let mut sum : Float = 0.0F
  for i in 0..<4 {
    sum = sum + math_exp_f32(ls.data[i])
  }
  if (sum - 1.0F).abs() > 1.0e-5 {
    fail("sum of exp(log_softmax) expected 1, got \{sum}")
  }
}

///|
test "tensor_matmul / shape (5x3) * (3x4) -> (5x4) verify values" {
  // First row of a is all 1s, first row of b is [1,2,3,4] -> sum [10,20,30,40].
  let a = Tensor::from(Array::make(15, 1.0F), [5, 3])
  let b_data : Array[Float] = [
    1.0F, 2.0F, 3.0F, 4.0F,
    5.0F, 6.0F, 7.0F, 8.0F,
    9.0F, 10.0F, 11.0F, 12.0F,
  ]
  let b = Tensor::from(b_data, [3, 4])
  let c = tensor_matmul(a, b)
  // Every row should give [1+5+9, 2+6+10, 3+7+11, 4+8+12] = [15, 18, 21, 24].
  for i in 0..<5 {
    for j in 0..<4 {
      let v = c.data[i * 4 + j]
      let expected : Array[Float] = [15.0F, 18.0F, 21.0F, 24.0F]
      if (v - expected[j]).abs() > 1.0e-5 {
        fail("c[\{i},\{j}] expected \{expected[j]}, got \{v}")
      }
    }
  }
}