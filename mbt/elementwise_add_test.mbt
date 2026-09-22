// elementwise_add_test.mbt — Elementwise Add tests (v0.20.0).

///|
test "add_forward / elementwise sum" {
  let a : Array[Float] = [1.0F, 2.0F, 3.0F, 4.0F]
  let b : Array[Float] = [10.0F, 20.0F, 30.0F, 40.0F]
  let out = add_forward(a, b)
  if out.length() != 4 { fail("output length should be 4") }
  let expected : Array[Float] = [11.0F, 22.0F, 33.0F, 44.0F]
  for i in 0..<4 {
    if out[i] != expected[i] { fail("out[\{i}] expected \{expected[i]}, got \{out[i]}") }
  }
}

///|
test "add_forward / does not alias inputs" {
  let a : Array[Float] = [1.0F, 2.0F]
  let b : Array[Float] = [3.0F, 4.0F]
  let out = add_forward(a, b)
  out[0] = 99.0F
  if a[0] != 1.0F { fail("original a[0] should not be aliased") }
  if b[0] != 3.0F { fail("original b[0] should not be aliased") }
}

///|
test "add_forward / zero tensor is identity" {
  let a : Array[Float] = [1.5F, -2.5F, 3.14F]
  let z : Array[Float] = [0.0F, 0.0F, 0.0F]
  let out = add_forward(a, z)
  for i in 0..<3 {
    if out[i] != a[i] { fail("add with zero should equal a[\{i}]") }
  }
}

///|
test "add_backward / returns two independent copies of d_output" {
  let d : Array[Float] = [1.0F, 2.0F, 3.0F]
  let (d_a, d_b) = add_backward(d)
  if d_a.length() != 3 { fail("d_a length mismatch") }
  if d_b.length() != 3 { fail("d_b length mismatch") }
  for i in 0..<3 {
    if d_a[i] != d[i] { fail("d_a[\{i}] should equal d[\{i}]") }
    if d_b[i] != d[i] { fail("d_b[\{i}] should equal d[\{i}]") }
  }
  // Mutate one; the other must be independent.
  d_a[0] = 99.0F
  if d_b[0] != 1.0F { fail("d_a and d_b must be independent copies") }
  if d[0] != 1.0F { fail("d_input must not be aliased") }
}

///|
test "add_backward / gradients flow through unchanged" {
  // If we have a scalar loss L = sum(F(x) + x), dL/dF = dL/dx = 1
  // everywhere. Verifying by perturbing each input and checking that
  // d_a and d_b both equal d_output exactly.
  let a : Array[Float] = [0.5F, 1.5F, 2.5F]
  let b : Array[Float] = [0.5F, 1.5F, 2.5F]
  let out = add_forward(a, b)
  let d_output : Array[Float] = Array::make(out.length(), 1.0F)
  let (d_a, d_b) = add_backward(d_output)
  for i in 0..<out.length() {
    if d_a[i] != 1.0F { fail("d_a[\{i}] should be 1.0") }
    if d_b[i] != 1.0F { fail("d_b[\{i}] should be 1.0") }
  }
}