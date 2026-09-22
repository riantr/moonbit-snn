// tensor_test.mbt 鈥?tests for Tensor struct + element-wise ops (v0.13.0).

///|

///|
test "tensor_zeros / numel matches total_size(shape)" {
  let t = Tensor::zeros([2, 3, 4])
  if t.numel() != 24 { fail("expected 24, got \{t.numel()}") }
  for i in 0..<t.numel() {
    if t.data[i] != 0.0F { fail("zeros[\{i}] should be 0.0, got \{t.data[i]}") }
  }
}

///|
test "tensor_ones / numel and shape" {
  let t = Tensor::ones([3, 5])
  if t.numel() != 15 { fail("expected 15, got \{t.numel()}") }
  for i in 0..<t.numel() {
    if t.data[i] != 1.0F { fail("ones[\{i}] should be 1.0, got \{t.data[i]}") }
  }
}

///|
test "tensor_reshape / total size preserved, data shared" {
  let t = Tensor::from([1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F], [2, 3])
  let r = t.reshape([3, 2])
  if r.numel() != 6 { fail("expected 6, got \{r.numel()}") }
  if r.shape[0] != 3 || r.shape[1] != 2 {
    fail("reshape shape wrong: \{r.shape[0]}, \{r.shape[1]}")
  }
  if r.data[0] != t.data[0] || r.data[5] != t.data[5] {
    fail("reshape should share data")
  }
}

///|
test "tensor_add / same shape: element-wise add" {
  let a = Tensor::from([1.0F, 2.0F, 3.0F, 4.0F], [2, 2])
  let b = Tensor::from([10.0F, 20.0F, 30.0F, 40.0F], [2, 2])
  let c = tensor_add(a, b)
  let expected : Array[Float] = [11.0F, 22.0F, 33.0F, 44.0F]
  for i in 0..<4 {
    if (c.data[i] - expected[i]).abs() > 1.0e-7 {
      fail("add[\{i}] expected \{expected[i]}, got \{c.data[i]}")
    }
  }
  // Output shape same as inputs.
  if c.shape.length() != 2 || c.shape[0] != 2 || c.shape[1] != 2 {
    fail("add shape should be [2,2], got \{c.shape.length()}")
  }
}

///|
test "tensor_add / broadcasting (scalar)" {
  let a = Tensor::from([1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F], [2, 3])
  let b = Tensor::from([10.0F], [1])
  let c = tensor_add(a, b)
  if c.shape.length() != 2 || c.shape[0] != 2 || c.shape[1] != 3 {
    fail("broadcast add shape should be [2,3], got \{c.shape}")
  }
  let expected : Array[Float] = [11.0F, 12.0F, 13.0F, 14.0F, 15.0F, 16.0F]
  for i in 0..<6 {
    if (c.data[i] - expected[i]).abs() > 1.0e-7 {
      fail("broadcast add[\{i}] expected \{expected[i]}, got \{c.data[i]}")
    }
  }
}

///|
test "tensor_add / broadcasting (row vector across rows)" {
  let a = Tensor::from(
    [1.0F, 2.0F, 3.0F, 4.0F,
     5.0F, 6.0F, 7.0F, 8.0F,
     9.0F, 10.0F, 11.0F, 12.0F],
    [3, 4],
  )
  let b = Tensor::from([100.0F, 200.0F, 300.0F, 400.0F], [4])
  let c = tensor_add(a, b)
  if c.shape.length() != 2 || c.shape[0] != 3 || c.shape[1] != 4 {
    fail("row-broadcast add shape should be [3,4]")
  }
  let expected : Array[Float] = [
    101.0F, 202.0F, 303.0F, 404.0F,
    105.0F, 206.0F, 307.0F, 408.0F,
    109.0F, 210.0F, 311.0F, 412.0F,
  ]
  for i in 0..<12 {
    if (c.data[i] - expected[i]).abs() > 1.0e-7 {
      fail("add[\{i}] expected \{expected[i]}, got \{c.data[i]}")
    }
  }
}

///|
test "tensor_sub / mul / div / basic" {
  let a = Tensor::from([10.0F, 20.0F, 30.0F, 40.0F], [4])
  let b = Tensor::from([2.0F, 4.0F, 5.0F, 8.0F], [4])
  let s = tensor_sub(a, b)
  if (s.data[0] - 8.0F).abs() > 1.0e-7 { fail("sub[0] expected 8") }
  if (s.data[3] - 32.0F).abs() > 1.0e-7 { fail("sub[3] expected 32") }
  let m = tensor_mul(a, b)
  if (m.data[0] - 20.0F).abs() > 1.0e-7 { fail("mul[0] expected 20") }
  if (m.data[3] - 320.0F).abs() > 1.0e-7 { fail("mul[3] expected 320") }
  let d = tensor_div(a, b)
  if (d.data[0] - 5.0F).abs() > 1.0e-7 { fail("div[0] expected 5") }
  if (d.data[2] - 6.0F).abs() > 1.0e-7 { fail("div[2] expected 6") }
}

///|
test "tensor_add / does not alias inputs" {
  let a = Tensor::from([1.0F, 2.0F], [2])
  let b = Tensor::from([10.0F, 20.0F], [2])
  let a_orig = a.data.copy()
  let b_orig = b.data.copy()
  let c = tensor_add(a, b)
  c.data[0] = -1.0F
  if a.data[0] != a_orig[0] || a.data[1] != a_orig[1] {
    fail("tensor_add should not alias a")
  }
  if b.data[0] != b_orig[0] || b.data[1] != b_orig[1] {
    fail("tensor_add should not alias b")
  }
}

///|
test "tensor_add / broadcasting (column vector)" {
  let a = Tensor::from(
    [1.0F, 2.0F, 3.0F, 4.0F,
     5.0F, 6.0F, 7.0F, 8.0F,
     9.0F, 10.0F, 11.0F, 12.0F],
    [3, 4],
  )
  let b = Tensor::from([100.0F, 200.0F, 300.0F], [3, 1])
  let c = tensor_add(a, b)
  let expected : Array[Float] = [
    101.0F, 102.0F, 103.0F, 104.0F,
    205.0F, 206.0F, 207.0F, 208.0F,
    309.0F, 310.0F, 311.0F, 312.0F,
  ]
  for i in 0..<12 {
    if (c.data[i] - expected[i]).abs() > 1.0e-7 {
      fail("col-broadcast add[\{i}] expected \{expected[i]}, got \{c.data[i]}")
    }
  }
}